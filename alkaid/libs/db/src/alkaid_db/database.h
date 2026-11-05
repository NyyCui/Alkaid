/**
 * @file database.h
 * @brief 数据库访问层：封装连接池与事务，提供查询（query）、参数化写操作（command）
 *        与 SQL 脚本执行（execute）三类入口
 */
#ifndef ALKAID_DATABASE_H
#define ALKAID_DATABASE_H
#include "alkaid_log/logging.h"
#include "ddl.h"
#include "dml.h"
#include "sql_val_bind.h"
#include "sqnice/sqnice.hh"
#include <concepts>
#include <array>
#include <unordered_map>
#include <variant>
#include <fmt/ranges.h>
#include <tl/expected.hpp>

namespace alkaid {

/**
 * @brief SQLite 数据库访问层
 *
 * 内部以 variant 持有两种连接来源：
 * - 池模式：按文件路径打开，读写时向连接池借用，操作结束后归还；
 * - 事务视图模式：持有固定连接，由 transaction() 创建，保证事务期间连接独占。
 */
class Database {

  /// 连接来源：shared_ptr<database> 为固定连接（事务视图），
  /// shared_ptr<pool> 为连接池（普通读写场景）
  std::variant<std::shared_ptr<sqnice::database>, std::shared_ptr<sqnice::pool>> connection_;

  /**
   * @brief 获取数据库连接句柄
   *
   * - 事务视图：直接返回固定连接，保证事务内所有语句复用同一连接；
   * - 池模式：根据 writeable 借用只读或可写连接，归还时机与 shared_ptr
   *   引用计数绑定——最后一个引用释放时由自定义 deleter 归还池子，
   *   避免提前归还导致的并发竞争。
   *
   * @param writeable 是否借用可写连接；只读连接供 query 使用，
   *                  可写连接供 command/execute/transaction 使用
   * @return 持有连接的 shared_ptr
   */
  std::shared_ptr<sqnice::database> acquire(bool writeable = false) const {
    if (std::holds_alternative<std::shared_ptr<sqnice::database>>(connection_)) {
      return std::get<std::shared_ptr<sqnice::database>>(connection_);
    }

    const auto pool = std::get<std::shared_ptr<sqnice::pool>>(connection_);
    if (writeable) {
      auto borrowed = pool->borrow_writeable();
      // 把借用句柄的所有权转移给 shared_ptr，不再提前归还；
      // pool 的归还函数是私有的，需借助临时 borrowed 句柄析构时完成归还
      return std::shared_ptr<sqnice::database>(borrowed.release(),
                                               [pool](sqnice::database *db) {
                                                 sqnice::borrowed_writeable_database(db, *pool);
                                               }
                                              );
    }
    // 只读句柄指向 const database，池内实际存放的是非 const 对象，去 const 安全
    auto borrowed = pool->borrow();
    return std::shared_ptr<sqnice::database>(
                                             const_cast<sqnice::database *>(borrowed.release()),
                                             [pool](sqnice::database *db) {
                                               sqnice::borrowed_database(db, *pool);
                                             }
                                            );
  }


public:
  /**
   * @brief 以连接池模式打开数据库
   * @param filepath 数据库文件路径
   * @param capacity 连接池容量
   */
  Database(const std::string &filepath, const uint32_t capacity = 5)
    : connection_(std::make_shared<sqnice::pool>(filepath)) {
    const auto pool = std::get<std::shared_ptr<sqnice::pool>>(connection_);
    pool->set_capacity(capacity);
    pool->on_open([](sqnice::database &db) {
                    db.exceptions(false);
                    db.setup();
                  }
                 );
  }

  /**
   * @brief 构造事务视图（仅内部使用，供 transaction() 调用）
   * @param connection 事务期间独占的固定连接
   */
  Database(std::shared_ptr<sqnice::database> connection) : connection_(connection) {}

  /**
   * @brief 以回调形式组织事务：借用连接并开启事务，回调全程复用同一连接；
   *        回调返回有值时提交，返回错误时回滚
   *
   * @param fn 事务体，入参为持有固定连接的事务视图；
   *           返回 tl::expected，携带错误信息时回滚
   * @return 成功返回有值 expected；回调失败透传其错误信息；
   *         提交失败返回"事务提交失败"
   * @note 回调内必须使用参数 db 执行语句；使用捕获的外层池模式对象
   *       执行写操作会因连接被事务独占而死锁。
   *       回调内对 db 再调 transaction() 是安全的（savepoint 嵌套）。
   */
  template<typename T>
  tl::expected<T, std::string> transaction(const std::function<tl::expected<T, std::string>(std::shared_ptr<Database> db)> &fn) const {
    const auto conn = acquire(true);
    sqnice::transaction txnGuard;
    if (!sqnice::ok(txnGuard.begin(*conn))) {
      return tl::unexpected("事务开启失败");
    }
    // 以 conn 构造事务视图：回调内所有 acquire 均返回同一个连接
    try {
      const auto result = fn(std::make_shared<Database>(conn));
      if (!result.has_value()) {
        txnGuard.rollback();
        return result;
      }
      if (!sqnice::ok(txnGuard.commit())) {
        txnGuard.rollback();
        return tl::unexpected("事务提交失败");
      }
      return result;
    } catch (...) {
      txnGuard.rollback();
      return tl::unexpected("执行过程中发生异常");
    }
  }

  template<typename T>
  tl::expected<T, std::string> query(const SQL::SelectSQL &sql, std::function<T(sqnice::query &)> fn, const std::unordered_map<std::string, SQL::Val> &params) const {
    if (!sql) {
      LOGGER_DEBUG("执行失败：语句错误");
      return tl::unexpected("执行失败：语句错误");
    }
    try {
      const auto &sqlStr = sql.build();
      LOGGER_DEBUG("SQL语句： {}", sqlStr);
      const auto conn = acquire();
      auto q = conn->query(sqlStr);
      if (!params.empty()) {
        const auto &slots = sql.slots();
        for (const auto &slot: slots) {
          if (!params.contains(slot)) {
            LOGGER_ERROR("参数不存在： {}", slot);
            continue;
          }
          q.bind((":" + slot).c_str(), params.at(slot));
        }
      }
      return fn(q);
    } catch (...) {
      LOGGER_DEBUG("执行失败：请检查语句是否正确，以及数据库连接是否正常");
      return tl::unexpected("执行失败：请检查语句是否正确，以及数据库连接是否正常");
    }
  }

  template<typename T>
    requires(std::constructible_from<T, const sqnice::query::row &>)
  tl::expected<std::vector<T>, std::string> query(const SQL::SelectSQL &sql, const std::unordered_map<std::string, SQL::Val> &params) const {
    if (!sql) {
      LOGGER_DEBUG("执行失败：语句错误");
      return tl::unexpected("执行失败：语句错误");
    }
    try {
      return query<std::vector<T>>(sql, [](sqnice::query &q) {
                                     std::vector<T> result;
                                     for (const auto &row: q) {
                                       result.emplace_back(row);
                                     }
                                     return result;
                                   }, params
                                  );
    } catch (...) {
      LOGGER_DEBUG("执行失败：请检查语句是否正确，以及数据库连接是否正常");
      return tl::unexpected("执行失败：请检查语句是否正确，以及数据库连接是否正常");
    }
  }

  template<typename T>
  tl::expected<T, std::string> query(const SQL::SelectSQL &sql, std::function<T(sqnice::query &)> fn) const {
    if (!sql) {
      LOGGER_DEBUG("执行失败：语句错误");
      return tl::unexpected("执行失败：语句错误");
    }
    try {
      const auto &sqlStr = sql.build();
      LOGGER_DEBUG("SQL语句： {}", sqlStr);
      const auto conn = acquire();
      auto q = conn->query(sqlStr);
      return fn(q);
    } catch (...) {
      LOGGER_DEBUG("执行失败：请检查语句是否正确，以及数据库连接是否正常");
      return tl::unexpected("执行失败：请检查语句是否正确，以及数据库连接是否正常");
    }
  }

  template<typename T>
    requires(std::constructible_from<T, const sqnice::query::row &>)
  tl::expected<std::vector<T>, std::string> query(const SQL::SelectSQL &sql) const {
    if (!sql) {
      LOGGER_DEBUG("执行失败：语句错误");
      return tl::unexpected("执行失败：语句错误");
    }
    try {
      return query<std::vector<T>>(sql, [](sqnice::query &q) {
                                     std::vector<T> result;
                                     for (const auto &row: q) {
                                       result.emplace_back(row);
                                     }
                                     return result;
                                   }
                                  );
    } catch (...) {
      LOGGER_DEBUG("执行失败：请检查语句是否正确，以及数据库连接是否正常");
      return tl::unexpected("执行失败：请检查语句是否正确，以及数据库连接是否正常");
    }
  }

  template<typename T>
    requires(std::same_as<T, SQL::InsertSQL> ||
             std::same_as<T, SQL::DeleteSQL> ||
             std::same_as<T, SQL::UpdateSQL>)
  tl::expected<int32_t, std::string> command(const T &sql, const std::unordered_map<std::string, SQL::Val> &params) const {
    if (!sql) {
      LOGGER_DEBUG("执行失败：语句错误");
      return tl::unexpected("执行失败：语句错误");
    }
    const auto &sqlStr = sql.build();
    LOGGER_DEBUG("SQL语句：{}", sqlStr);
    const auto conn = acquire(true);
    sqnice::command command = conn->command(sqlStr);
    if (!params.empty()) {
      const auto &slots = sql.slots();
      for (const auto &slot: slots) {
        if (!params.contains(slot)) {
          LOGGER_ERROR("参数不存在： {}", slot);
          continue;
        }
        command.bind((":" + slot).c_str(), params.at(slot));
      }
    }
    auto status = command.execute();
    if (sqnice::ok(status)) return command.changes();
    return tl::unexpected(errMsg(status));
  }

  template<typename T>
    requires(std::same_as<T, SQL::InsertSQL> ||
             std::same_as<T, SQL::DeleteSQL> ||
             std::same_as<T, SQL::UpdateSQL>)
  tl::expected<int32_t, std::string> command(const T &sql) const {
    if (!sql) {
      LOGGER_DEBUG("执行失败：语句错误");
      return tl::unexpected("执行失败：语句错误");
    }
    const auto &sqlStr = sql.build();
    LOGGER_DEBUG("SQL语句：{}", sqlStr);
    const auto conn = acquire(true);
    sqnice::command command = conn->command(sqlStr);
    auto status = command.execute();
    if (sqnice::ok(status)) return command.changes();
    return tl::unexpected(errMsg(status));
  }

  /**
   * @brief SQL 脚本执行入口：接受一个或多个非 SELECT 构建器，
   *        每条语句自带分号，以换行连接后一次性执行
   *
   * 对应 sqnice 的 execute（non-SELECT），适合建表、批量语句等
   * 无需参数绑定与行数统计的场景；SELECT 必须走 query 入口。
   *
   * @tparam T 构建器类型包，限 DML/DDL 六种非 SELECT 构建器
   * @param sql 一个或多个 SQL 构建器（至少一个）
   * @return 全部语句执行成功返回 true，任一失败返回 false
   */
  template<typename... T>
    requires(sizeof...(T) > 0 &&
             ((std::same_as<T, SQL::InsertSQL> ||
               std::same_as<T, SQL::DeleteSQL> ||
               std::same_as<T, SQL::UpdateSQL> ||
               std::same_as<T, SQL::DropTableSQL> ||
               std::same_as<T, SQL::CreateTableSQL> ||
               std::same_as<T, SQL::AlterTableSQL>) && ...))
  tl::expected<bool, std::string> execute(const T &... sql) const {
    const bool allValid = [&]<std::size_t... I>(std::index_sequence<I...>) {
      bool valid = true;
      (([&] {
        if (!static_cast<bool>(sql)) {
          LOGGER_DEBUG("执行失败：第 {} 条语句非法", I + 1);
          valid = false;
        }
      }()), ...);
      return valid;
    }(std::index_sequence_for<T...>{});

    if (!allValid) {
      return tl::unexpected("执行失败：语句错误");
    }
    const auto sqlStr = fmt::format("{}", fmt::join(std::array<std::string, sizeof...(T)>{sql.build(true)...}, "\n"));
    LOGGER_DEBUG("SQL语句：{}", sqlStr);
    return sqnice::ok(acquire(true)->execute(sqlStr));
  }

  /**
   * @brief 将 sqnice::status 状态码转换为中文描述
   * @param s SQLite 状态码
   * @return 对应的中文字符串
   */
  static std::string errMsg(const sqnice::status s) {
    using enum sqnice::status;
    switch (s) {
      case ok: return "操作成功";
      case error: return "SQL 错误";
      case perm: return "权限被拒绝";
      case abort: return "操作中止";
      case busy: return "数据库忙";
      case locked: return "数据库锁定";
      case readonly: return "数据库只读";
      case interrupt: return "操作被中断";
      case ioerr: return "I/O 错误";
      case corrupt: return "数据库损坏";
      case cantopen: return "无法打开数据库";
      case constraint: return "约束冲突";
      case mismatch: return "类型不匹配";
      case misuse: return "接口误用";
      case auth: return "认证失败";
      case range: return "参数越界";
      case not_a_db: return "无效数据库";
      case notice: return "通知";
      case warning: return "警告";
      case row: return "有数据行";
      case done: return "执行完成";
      default: return "未知错误";
    }
  }

};

} // namespace alkaid
#endif // ALKAID_DATABASE_H

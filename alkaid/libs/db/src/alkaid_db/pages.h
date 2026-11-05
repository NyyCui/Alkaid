#ifndef ALKAID_PAGES_H
#define ALKAID_PAGES_H
#include "alkaid_utils/str_util.h"
#include "alkaid_log/logging.h"
#include <algorithm>
#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "alkaid_db/dql.h"

/**
 * @file pages.h
 * @brief 分页工具，提供分页参数解析、排序参数解析与分页结果构建功能
 *
 * 排序参数格式：字段名[:asc|desc]，多个排序项以逗号分隔，如 "createTime:desc,id:asc"。
 * 字段名默认经驼峰转下划线处理（如 createTime → create_time），
 * 也可通过自定义字段转换器将前端字段名映射为表字段名，转换结果为空白则跳过该项。
 */
namespace alkaid::Pages {

/// 排序项
struct OrderItem {
  std::string column; ///< 排序列名（表字段名）
  bool asc = true;    ///< true 为升序（ASC），false 为降序（DESC）
};

/**
 * @brief 分页参数（对应前端传入的可空参数）
 *
 * 兼具分页对象职责（对应 MyBatis-Plus 的 Page）：
 * 通过 pageNum() / pageSize() / offset() 获取归一化后的取值，
 * 通过 parseOrders() 解析排序参数。
 *
 * @param size 每页显示条数，默认 500
 * @param current 当前页，默认 1
 * @param orders 排序字段信息，格式：a:desc,b:asc
 */
struct PageParam {
private:
  std::optional<uint32_t> size_;    ///< 每页显示条数，缺省时按 500 处理
  std::optional<uint32_t> current_; ///< 当前页，缺省时按 1 处理
  std::string orders_;              ///< 排序字段信息，格式：a:desc,b:asc
  std::optional<std::function<std::string(const std::string &)>> fieldMap_;

public:
  PageParam(uint32_t size = 500,
            uint32_t current = 1,
            const std::string &orders = ""
  )
    : size_(size)
    , current_(current)
    , orders_(orders)
    , fieldMap_(std::nullopt) {}

  uint32_t size() const {
    const uint32_t val = size_.value_or(500);
    return val < 1 ? 500 : val;
  }

  uint32_t current() const {
    const uint32_t val = current_.value_or(1);
    return val < 1 ? 1 : val;
  }

  std::vector<OrderItem> orders() const {
    if (StrUtil::isBlank(orders_)) return {};
    std::vector<OrderItem> orderItems;
    for (const auto &orderItem: StrUtil::split(orders_, ',')) {
      const auto item = StrUtil::split(orderItem, ':');
      if (item.size() == 1) {
        const auto field = fieldMap_.has_value() ? fieldMap_->operator()(item[0]) : item[0];
        if (StrUtil::isBlank(field)) {
          LOGGER_WARN("跳过无效排序参数：{}", orderItem);
          continue;
        }
        orderItems.push_back({std::move(field), true});
      } else if (item.size() == 2) {
        const auto field = fieldMap_.has_value() ? fieldMap_->operator()(item[0]) : item[0];
        if (StrUtil::isBlank(field)) {
          LOGGER_WARN("跳过无效排序参数：{}", orderItem);
          continue;
        }
        if (StrUtil::equalsIgnoreCase(item[1], "asc")) {
          orderItems.push_back({std::move(field), true});
        } else if (StrUtil::equalsIgnoreCase(item[1], "desc")) {
          orderItems.push_back({std::move(field), false});
        } else {
          LOGGER_WARN("跳过无效排序参数：{}", orderItem);
        }
      } else {
        LOGGER_WARN("跳过无效排序参数：{}", orderItem);
      }
    }
    return orderItems;
  }

  PageParam &setFieldMap(const std::function<std::string(const std::string &)> &fieldMap) {
    fieldMap_ = fieldMap;
    return *this;
  }

};

/**
 * @brief 分页结果
 *
 * @param total 总条数
 * @param pages 总页数
 * @param current 当前页
 * @param size 每页显示条数
 */
template<typename E>
struct PageResult {
  std::vector<E> records; ///< 数据列表
  uint32_t total;         ///< 总条数
  uint32_t pages;         ///< 总页数
  uint32_t current;       ///< 当前页
  uint32_t size;          ///< 每页显示条数

  /**
   * @brief 转换数据列表的元素类型，分页元信息保持不变
   * @tparam F 转换函数类型，受 std::invocable 约束，签名为 R(const E &)
   * @param mapper 转换函数
   * @return 转换后的分页结果
   *
   * @code
   * auto dtoResult = entityResult.convert([](const Entity &e) { return e.toDTO(); });
   * @endcode
   */
  template<typename F>
    requires std::invocable<F &, const E &>
  auto convert(F &&mapper) const {
    using R = std::decay_t<std::invoke_result_t<F &, const E &>>;
    std::vector<R> newRecords;
    newRecords.reserve(records.size());
    for (const auto &row: records) newRecords.emplace_back(mapper(row));
    return PageResult<R>{std::move(newRecords), total, pages, current, size};
  }

  static PageResult of(const PageParam &param, std::vector<E> records, const int64_t total) {
    const uint32_t size = param.size();
    uint32_t current = param.current();
    return PageResult{std::move(records), static_cast<uint32_t>(total), static_cast<uint32_t>((total + size - 1) / size), current, size};
  }

  static const PageResult &empty(const PageParam &param) {
    static PageResult pageResult{std::vector<E>(), 0, 0, param.current(), param.size()};
    return pageResult;
  }

  friend void to_json(nlohmann::json &j, const PageResult &p) {
    j = nlohmann::json{
      {"records", p.records},
      {"total", p.total},
      {"pages", p.pages},
      {"current", p.current},
      {"size", p.size}
    };
  }
};

} // namespace alkaid::Pages

#endif // ALKAID_PAGES_H

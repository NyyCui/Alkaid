#ifndef ALKAID_DQL_H
#define ALKAID_DQL_H
#include "where.h"
#include <concepts>
#include <cstdint>
#include <format>
#include <numeric>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace alkaid::SQL {

namespace _ {

template<typename T>
concept LimitParam =
  std::same_as<std::remove_cvref_t<T>, int8_t> ||
  std::same_as<std::remove_cvref_t<T>, int16_t> ||
  std::same_as<std::remove_cvref_t<T>, int32_t> ||
  std::same_as<std::remove_cvref_t<T>, int64_t> ||
  std::same_as<std::remove_cvref_t<T>, uint8_t> ||
  std::same_as<std::remove_cvref_t<T>, uint16_t> ||
  std::same_as<std::remove_cvref_t<T>, uint32_t> ||
  std::same_as<std::remove_cvref_t<T>, uint64_t> ||
  std::same_as<std::remove_cvref_t<T>, Slot>;

} // namespace _

class SelectSQL {
  std::string columnNames_;
  std::string tableName_;
  std::vector<std::string> joins_;
  std::string whereStatement_;
  std::vector<std::string> groupByColumns_;
  std::string havingStatement_;
  std::vector<std::string> orderBys_;
  std::string limitOffset_;
  std::vector<std::string> unions_;
  std::set<std::string> slot_;

  std::string visitOperand(const Operand &value) {
    return std::visit(VariantUtil::Overloaded{
                        [](const Val &v) {
                          return v.toString();
                        },
                        [this](const Slot &v) {
                          slot_.insert(v.name());
                          return v.toString();
                        }
                      }, value);
  }

  template<_::LimitParam T>
  std::string limitFragment(const T &value) {
    if constexpr (std::same_as<std::remove_cvref_t<T>, Slot>) {
      return visitOperand(value);
    } else {
      return std::to_string(value);
    }
  }

  void join_(std::string joinType, std::string table, std::string alias, const On &on) {
    slot_.insert(on.slots().begin(), on.slots().end());
    joins_.emplace_back(std::format("{} JOIN {} {} ON {}", joinType, table, alias, on.build()));
  }

public:
  SelectSQL(const std::vector<std::string> &columns, const std::string &table, const std::string &alias = "") : columnNames_("*")
                                                                                                              , tableName_(table)
                                                                                                              , joins_({})
                                                                                                              , whereStatement_("")
                                                                                                              , groupByColumns_({})
                                                                                                              , havingStatement_("")
                                                                                                              , orderBys_({})
                                                                                                              , limitOffset_("") {
    if (!columns.empty()) {
      columnNames_ = std::accumulate(std::next(columns.begin()), columns.end(), *columns.begin(),
                                     [](const std::string &a, const std::string &b) {
                                       return std::format(R"({}, {})", a, b);
                                     }
                                    );
    }
    if (!alias.empty()) tableName_ += " " + alias;
  }

  explicit SelectSQL(const std::string &table, const std::string &alias = "") : SelectSQL({}, table, alias) {}

  SelectSQL(const std::vector<std::string> &columns, const SelectSQL &table, const std::string &alias) : SelectSQL(columns, "(" + table.build() + ")", alias) {
    slot_.insert(table.slots().begin(), table.slots().end());
  }

  SelectSQL(const SelectSQL &table, const std::string &alias) : SelectSQL({}, table, alias) {
    slot_.insert(table.slots().begin(), table.slots().end());
  }

  SelectSQL &leftJoin_(const std::string &table, const std::string &alias, const On &on) {
    join_("LEFT", table, alias, on);
    return *this;
  }

  SelectSQL &leftJoin_(const SelectSQL &table, const std::string &alias, const On &on) {
    join_("LEFT", "(" + table.build() + ")", alias, on);
    return *this;
  }

  SelectSQL &rightJoin_(const std::string &table, const std::string &alias, const On &on) {
    join_("RIGHT", table, alias, on);
    return *this;
  }

  SelectSQL &rightJoin_(const SelectSQL &table, const std::string &alias, const On &on) {
    join_("RIGHT", "(" + table.build() + ")", alias, on);
    return *this;
  }

  SelectSQL &innerJoin_(const std::string &table, const std::string &alias, const On &on) {
    join_("INNER", table, alias, on);
    return *this;
  }

  SelectSQL &innerJoin_(const SelectSQL &table, const std::string &alias, const On &on) {
    join_("INNER", "(" + table.build() + ")", alias, on);
    return *this;
  }

  SelectSQL &where_(const Where &where) {
    slot_.insert(where.slots().begin(), where.slots().end());
    whereStatement_ = where.build();
    return *this;
  }

  template<std::convertible_to<std::string>... Args>
  SelectSQL &groupBy_(const Args &... columns) {
    static_assert(sizeof...(columns) > 0, "groupBy_ 至少需要一列");
    groupByColumns_.reserve(groupByColumns_.size() + sizeof...(columns));
    (groupByColumns_.emplace_back(columns), ...);
    return *this;
  }

  SelectSQL &having_(const Where &having) {
    slot_.insert(having.slots().begin(), having.slots().end());
    havingStatement_ = having.build();
    return *this;
  }

  SelectSQL &orderBy_(const std::string &column, const bool asc = true) {
    orderBys_.push_back(std::format(R"({} {})", column, asc ? "ASC" : "DESC"));
    return *this;
  }

  SelectSQL &orderBy_(const bool condition, const std::string &column, const bool asc = true) {
    if (condition) orderBy_(column, asc);
    return *this;
  }

  template<_::LimitParam T, _::LimitParam U>
  SelectSQL &limit_(const T &count, const U &offset) {
    if constexpr (!std::same_as<std::remove_cvref_t<U>, Slot>) {
      if (offset == 0) {
        return limit_(count);
      }
    }
    limitOffset_ = std::format(R"(LIMIT {} OFFSET {})", limitFragment(count), limitFragment(offset));
    return *this;
  }

  template<_::LimitParam T>
  SelectSQL &limit_(const T &count) {
    limitOffset_ = std::format(R"(LIMIT {})", limitFragment(count));
    return *this;
  }

  SelectSQL &union_(const SelectSQL &sql, const bool unionAll) {
    unions_.push_back(unionAll ? std::format(R"(UNION ALL ({}))", sql.build()) : std::format(R"(UNION ({}))", sql.build()));
    return *this;
  }

  explicit operator bool() const {
    return !columnNames_.empty()
           && !tableName_.empty();
  }

  SelectSQL toCountSql() const {
    SelectSQL sql = *this;
    sql.columnNames_ = "COUNT(*)";
    return sql;
  }

  SelectSQL toExistsSql() const {
    SelectSQL sql = *this;
    sql.columnNames_ = "1";
    return sql;
  }

  const std::set<std::string> &slots() const {
    return slot_;
  }

  std::string build(const bool semicolon = false) const {
    if (!operator bool()) return {};
    auto sql = std::format(R"(SELECT {} FROM {})", columnNames_, tableName_);
    if (!joins_.empty()) sql += " " + StrUtil::join(joins_, " ");
    if (!whereStatement_.empty()) sql += " WHERE " + whereStatement_;
    if (!orderBys_.empty()) {
      sql += " ORDER BY " + std::accumulate(
                                            std::next(orderBys_.begin()),
                                            orderBys_.end(),
                                            *orderBys_.begin(),
                                            [](const std::string &a, const std::string &b) {
                                              return std::format(R"({}, {})", a, b);
                                            }
                                           );
    }
    if (!havingStatement_.empty()) sql += " HAVING " + havingStatement_;
    if (!limitOffset_.empty()) sql += " " + limitOffset_;
    if (!unions_.empty()) sql += " " + StrUtil::join(unions_, " ");
    return sql + (semicolon ? ";" : "");
  }
};

} // namespace alkaid::SQL

#endif // ALKAID_DQL_H

#ifndef ALKAID_SQL_H
#define ALKAID_SQL_H

#include "alkaid_utils/str_util.h"
#include "alkaid_db/sql_val.h"

#include <algorithm>
#include <format>
#include <functional>
#include <numeric>
#include <string>
#include <vector>

namespace alkaid::SQL {

class Where {
  struct WhereStatement {
    std::string connector;
    std::string fragment;
  };

  std::vector<WhereStatement> statements_;

  std::vector<std::string> slot_;

  void addStatement(const bool useAnd, const std::string &fragment) {
    statements_.emplace_back(useAnd ? "AND" : "OR", fragment);
  }

  std::string visitOperand(const Operand &value) {
    return std::visit(VariantUtil::Overloaded{
                        [](const Val &v) {
                          return v.toString();
                        },
                        [this](const Slot &v) {
                          slot_.push_back(v.name());
                          return v.toString();
                        }
                      }, value);
  }

  void inFragment(const std::string &column, const std::vector<Operand> &values, const bool reverse, const bool useAnd = true) {
    std::vector<std::string> dst(values.size());
    std::ranges::transform(values, dst.begin(),
                           [this](const Operand &x) {
                             return visitOperand(x);
                           }
                          );
    addStatement(useAnd, std::format(R"({} {}IN ({}))", reverse ? "NOT " : "", column,
                                     std::accumulate(
                                                     std::next(dst.begin()),
                                                     dst.end(),
                                                     *dst.begin(),
                                                     [](const std::string &a, const std::string &b) {
                                                       return a + ", " + b;
                                                     }
                                                    )
                                    )
                );
  }

public:
  Where() = default;

  bool empty() const {
    return statements_.empty();
  }

  Where &exists_(const Operand &value, const bool useAnd = true) {
    addStatement(useAnd, std::format(R"(EXISTS {})", visitOperand(value)));
    return *this;
  }

  Where &notExists_(const Operand &value, const bool useAnd = true) {
    addStatement(useAnd, std::format(R"(NOT EXISTS {})", visitOperand(value)));
    return *this;
  }

  Where &eq_(const std::string &column, const Operand &value, const bool useAnd = true) {
    addStatement(useAnd, std::format(R"({} = {})", column, visitOperand(value)));
    return *this;
  }

  Where &ne_(const std::string &column, const Operand &value, const bool useAnd = true) {
    addStatement(useAnd, std::format(R"({} != {})", column, visitOperand(value)));
    return *this;
  }

  Where &lt_(const std::string &column, const Operand &value, const bool useAnd = true) {
    addStatement(useAnd, std::format(R"({} < {})", column, visitOperand(value)));
    return *this;
  }

  Where &gt_(const std::string &column, const Operand &value, const bool useAnd = true) {
    addStatement(useAnd, std::format(R"({} > {})", column, visitOperand(value)));
    return *this;
  }

  Where &le_(const std::string &column, const Operand &value, const bool useAnd = true) {
    addStatement(useAnd, std::format(R"({} <= {})", column, visitOperand(value)));
    return *this;
  }

  Where &ge_(const std::string &column, const Operand &value, const bool useAnd = true) {
    addStatement(useAnd, std::format(R"({} >= {})", column, visitOperand(value)));
    return *this;
  }

  Where &between_(const std::string &column, const Operand &value1, const Operand &value2, const bool useAnd = true) {
    addStatement(useAnd, std::format(R"({} BETWEEN {} AND {})", column, visitOperand(value1), visitOperand(value2)));
    return *this;
  }

  Where &notBetween_(const std::string &column, const Operand &value1, const Operand &value2, const bool useAnd = true) {
    addStatement(useAnd, std::format(R"({} NOT BETWEEN {} AND {})", column, visitOperand(value1), visitOperand(value2)));
    return *this;
  }

  Where &in_(const std::string &column, const std::vector<Operand> &values, const bool useAnd = true) {
    if (values.empty()) return *this;
    inFragment(column, values, false, useAnd);
    return *this;
  }

  Where &notIn_(const std::string &column, const std::vector<Operand> &values, const bool useAnd = true) {
    if (values.empty()) return *this;
    inFragment(column, values, true, useAnd);
    return *this;
  }

  Where &like_(const std::string &column, const Operand &value, const bool useAnd = true) {
    addStatement(useAnd, std::format(R"({} LIKE {})", column, visitOperand(value)));
    return *this;
  }

  Where &notLike_(const std::string &column, const Operand &value, const bool useAnd = true) {
    addStatement(useAnd, std::format(R"({} NOT LIKE {})", column, visitOperand(value)));
    return *this;
  }

  Where &isNull_(const std::string &column, const bool useAnd = true) {
    addStatement(useAnd, std::format(R"({} IS NULL)", column));
    return *this;
  }

  Where &isNotNull_(const std::string &column, const bool useAnd = true) {
    addStatement(useAnd, std::format(R"({} IS NOT NULL)", column));
    return *this;
  }

  Where &group_(const std::function<void(Where &where)> &fn, const bool useAnd = true) {
    Where where;
    fn(where);
    this->slot_.assign(where.slot_.begin(), where.slot_.end());
    addStatement(useAnd, std::format(R"(({}))", where.build()));
    return *this;
  }

  Where &if_(const bool condition, const std::function<void(Where &where)> &fn) {
    if (condition) fn(*this);
    return *this;
  }

  const std::vector<std::string> &slots() const {
    return slot_;
  }

  std::string build() const {
    if (statements_.empty()) return "";
    std::string where = std::accumulate(std::next(statements_.begin()),
                                        statements_.end(),
                                        statements_.front().fragment,
                                        [](std::string a, const WhereStatement &b) {
                                          return std::move(a) + " " + b.connector + " " + b.fragment;
                                        }
                                       );
    where = StrUtil::trim(where);
    if (where.starts_with("AND")) return StrUtil::trim(where.substr(3));
    if (where.starts_with("OR")) return StrUtil::trim(where.substr(2));
    return where;
  }
};

using On = Where;
} // namespace alkaid::SQL

#endif // ALKAID_SQL_H

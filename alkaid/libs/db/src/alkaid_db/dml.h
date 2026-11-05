#ifndef ALKAID_DML_H
#define ALKAID_DML_H
#include "dql.h"
#include "where.h"

#include <format>
#include <functional>
#include <numeric>
#include <set>
#include <string>
#include <vector>

namespace alkaid::SQL {

class DeleteSQL {
  std::string table_;
  std::string where_;
  std::set<std::string> slot_;

public:
  DeleteSQL(const std::string &table, const Where &where) : table_(table)
                                                          , where_(where.build()) {
    slot_.insert(where.slots().begin(), where.slots().end());
  }

  const std::set<std::string> &slots() const {
    return slot_;
  }

  explicit operator bool() const {
    return !table_.empty()
           && !where_.empty();
  }

  std::string build(const bool semicolon = false) const {
    return std::format(R"(DELETE FROM {} WHERE {})", table_, where_) + (semicolon ? ";" : "");
  }
};

class UpdateSQL {
  std::string table_;
  std::vector<std::string> sets_;
  std::string whereStatement_;
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
                      }, value
                     );
  }

public:
  explicit UpdateSQL(const std::string &table) : table_(table) {}

  UpdateSQL &set_(std::string column, const Operand &value) {
    sets_.push_back(std::format(R"({} = {})", column, visitOperand(value)));
    return *this;
  }

  UpdateSQL &where_(const Where &where) {
    slot_.insert(where.slots().begin(), where.slots().end());
    whereStatement_ = where.build();
    return *this;
  }

  UpdateSQL &if_(const bool condition, const std::function<void(UpdateSQL &sql)> &fn) {
    if (condition) fn(*this);
    return *this;
  }

  const std::set<std::string> &slots() const {
    return slot_;
  }

  explicit operator bool() const {
    return !table_.empty()
           && !sets_.empty()
           && !whereStatement_.empty();
  }

  std::string build(const bool semicolon = false) const {
    if (!operator bool()) return {};
    return std::format(R"(UPDATE {} SET {} WHERE {})",
                       table_,
                       std::accumulate(std::next(sets_.begin()),
                                       sets_.end(),
                                       *sets_.begin(),
                                       [](const std::string &a, const std::string &b) {
                                         return a + ", " + b;
                                       }
                                      ),
                       whereStatement_
                      ) +
           (semicolon ? ";" : "");
  }
};

class InsertSQL {
  std::string table_;
  std::string columns_;
  std::vector<std::string> valuesStatement_;
  std::string select_;
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
                      }, value
                     );
  }

public:
  InsertSQL(const std::string &table, const std::vector<std::string> &columns) : table_(table) {
    if (columns.empty()) return;
    columns_ = std::accumulate(
                               std::next(columns.begin()),
                               columns.end(),
                               *columns.begin(),
                               [](const std::string &a, const std::string &b) {
                                 return a + ", " + b;
                               }
                              );
  }

  InsertSQL &values_(const std::vector<Operand> &values) {
    if (values.empty()) return *this;
    std::vector<std::string> dst(values.size());
    std::ranges::transform(values, dst.begin(),
                           [this](const Operand &x) {
                             return visitOperand(x);
                           }
                          );

    valuesStatement_.push_back(std::format(R"(({}))",
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
    return *this;
  }

  InsertSQL &values_(const SelectSQL &select) {
    slot_.insert(select.slots().begin(), select.slots().end());
    select_ = select.build();
    return *this;
  }

  explicit operator bool() const {
    return !table_.empty()
           && !columns_.empty()
           && (!valuesStatement_.empty() || !select_.empty());
  }

  const std::set<std::string> &slots() const {
    return slot_;
  }

  std::string build(const bool semicolon = false) const {
    if (!operator bool()) return {};
    if (!valuesStatement_.empty()) {
      return std::format(R"(INSERT INTO {} ({}) VALUES {})",
                         table_,
                         columns_,
                         std::accumulate(
                                         std::next(valuesStatement_.begin()),
                                         valuesStatement_.end(),
                                         *valuesStatement_.begin(),
                                         [](const std::string &a, const std::string &b) {
                                           return a + ", " + b;
                                         }
                                        )
                        ) +
             (semicolon ? ";" : "");
    }
    if (!select_.empty()) {
      return std::format(R"(INSERT INTO {} ({}) {})",
                         table_,
                         columns_,
                         select_
                        ) +
             (semicolon ? ";" : "");
    }
    return {};
  }
};

} // namespace alkaid::SQL

#endif // ALKAID_DML_H

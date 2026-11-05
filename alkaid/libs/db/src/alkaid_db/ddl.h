#ifndef ALKAID_DDL_H
#define ALKAID_DDL_H

#include <format>
#include <numeric>
#include <string>
#include <vector>

#include "alkaid_db/sql_val.h"

namespace alkaid::SQL {
namespace _ {
template<typename T>
concept DefaultVal =
  std::same_as<std::remove_cvref_t<T>, std::nullptr_t> ||
  std::same_as<std::remove_cvref_t<T>, int8_t> ||
  std::same_as<std::remove_cvref_t<T>, int16_t> ||
  std::same_as<std::remove_cvref_t<T>, int32_t> ||
  std::same_as<std::remove_cvref_t<T>, int64_t> ||
  std::same_as<std::remove_cvref_t<T>, uint8_t> ||
  std::same_as<std::remove_cvref_t<T>, uint16_t> ||
  std::same_as<std::remove_cvref_t<T>, uint32_t> ||
  std::same_as<std::remove_cvref_t<T>, uint64_t> ||
  std::same_as<std::remove_cvref_t<T>, float> ||
  std::same_as<std::remove_cvref_t<T>, double> ||
  std::same_as<std::remove_cvref_t<T>, std::string> ||
  std::same_as<std::remove_cvref_t<T>, std::vector<uint8_t>>;

} // namespace _

class DropTableSQL {
  std::string table_;
  bool ifExists_;

public:
  explicit DropTableSQL(const std::string &table, const bool ifExists = true) : table_(table)
                                                                              , ifExists_(ifExists) {}

  explicit operator bool() const {
    return !table_.empty();
  }

  std::string build(const bool semicolon = false) const {
    if (!operator bool()) return {};
    return std::format(R"(DROP TABLE {}{})", ifExists_ ? "IF EXISTS " : "", table_) + (semicolon ? ";" : "");
  }
};

class CreateTableSQL {
  bool ifNotExists_;
  std::string table_;
  std::vector<std::string> columns_;
  std::vector<std::string> primaryKeyStatement_;

public:
  explicit CreateTableSQL(const std::string &table, const bool ifNotExists = true) : ifNotExists_(ifNotExists)
                                                                                   , table_(table) {}

  template<_::DefaultVal T = std::nullptr_t>
  CreateTableSQL &column_(const std::string &column, const ColumnType &type, const bool notNull = true, const T &defaultVal = nullptr) {
    std::string type_ = "";
    switch (type) {
      case ColumnType::INTEGER: type_ = "INTEGER";
        break;
      case ColumnType::REAL: type_ = "REAL";
        break;
      case ColumnType::BLOB: type_ = "BLOB";
        break;
      case ColumnType::TEXT: type_ = "TEXT";
        break;
    }
    if constexpr (std::same_as<std::remove_cvref_t<T>, std::nullptr_t>) {
      columns_.push_back(std::format(R"({} {}{})", column, type_, notNull ? " NOT NULL" : ""));
    } else if (std::same_as<std::remove_cvref_t<T>, std::string>) {
      columns_.push_back(std::format(R"({} {}{} DEFAULT {})", column, type_, notNull ? " NOT NULL" : "", "'" + defaultVal + "'"));
    } else if (std::same_as<std::remove_cvref_t<T>, std::vector<uint8_t>>) {
      columns_.push_back(std::format(R"({} {}{} DEFAULT {})", column, type_, notNull ? " NOT NULL" : "", "X'" + StrUtil::toHex(defaultVal) + "'"));
    } else {
      columns_.push_back(std::format(R"({} {}{} DEFAULT {})", column, type_, notNull ? " NOT NULL" : "", defaultVal));
    }
    return *this;
  }

  CreateTableSQL &primaryKey_(const std::string &column) {
    primaryKeyStatement_.push_back(column);
    return *this;
  }

  explicit operator bool() const {
    return !table_.empty()
           && !columns_.empty();
  }

  std::string build(const bool semicolon = false) const {
    if (!operator bool()) return {};
    auto cols = std::accumulate(
                                std::next(columns_.begin()),
                                columns_.end(),
                                *columns_.begin(),
                                [](const std::string &a, const std::string &b) {
                                  return a + ", " + b;
                                }
                               );
    if (!primaryKeyStatement_.empty()) {
      const auto pks = std::accumulate(
                                       std::next(primaryKeyStatement_.begin()),
                                       primaryKeyStatement_.end(),
                                       *primaryKeyStatement_.begin(),
                                       [](const std::string &a, const std::string &b) {
                                         return a + ", " + b;
                                       }
                                      );
      cols += ", PRIMARY KEY (" + pks + ")";
    }
    return std::format(R"(CREATE TABLE{} {} ({}))", ifNotExists_ ? " IF NOT EXISTS" : "", table_, cols) + (semicolon ? ";" : "");
  }
};

class AlterTableSQL {
public:
  enum Operation {
    ADD_COLUMN,
    DROP_COLUMN,
    RENAME_COLUMN,
    RENAME_TABLE,
  };

private:
  std::string table_;
  Operation operation_;
  std::string column_;
  std::string type_;
  bool notNull_;
  std::string defaultValue_;
  std::string newColumn_;
  std::string newTable_;

public:
  AlterTableSQL(const std::string &table) :
                                          table_(table)
                                          , operation_(ADD_COLUMN)
                                          , column_("")
                                          , type_("")
                                          , notNull_(true)
                                          , defaultValue_("")
                                          , newColumn_("")
                                          , newTable_("") {}

  template<_::DefaultVal T = std::nullptr_t>
  AlterTableSQL &addColumn_(const std::string &column, const ColumnType &type, const bool notNull, const T &defaultVal = nullptr) {
    operation_ = ADD_COLUMN;
    column_ = column;
    switch (type) {
      case ColumnType::INTEGER: type_ = "INTEGER";
        break;
      case ColumnType::REAL: type_ = "REAL";
        break;
      case ColumnType::BLOB: type_ = "BLOB";
        break;
      case ColumnType::TEXT: type_ = "TEXT";
        break;
    }
    notNull_ = notNull;
    if constexpr (std::same_as<std::remove_cvref_t<T>, std::nullptr_t>) {
      return *this;
    } else if (std::same_as<std::remove_cvref_t<T>, std::string>) {
      defaultValue_ = " DEFAULT " + ("'" + defaultVal + "'");
    } else if (std::same_as<std::remove_cvref_t<T>, std::vector<uint8_t>>) {
      defaultValue_ = " DEFAULT " + ("X'" + StrUtil::toHex(defaultVal) + "'");
    } else {
      defaultValue_ = " DEFAULT " + std::to_string(defaultVal);
    }
    return *this;
  }

  AlterTableSQL &dropColumn_(const std::string &column) {
    operation_ = DROP_COLUMN;
    column_ = column;
    return *this;
  }

  AlterTableSQL &renameColumn_(const std::string &column, const std::string &newColumn) {
    operation_ = RENAME_COLUMN;
    column_ = column;
    newColumn_ = newColumn;
    return *this;
  }

  AlterTableSQL &renameTable_(const std::string &newTable) {
    operation_ = RENAME_TABLE;
    newTable_ = newTable;
    return *this;
  }

  explicit operator bool() const {
    switch (operation_) {
      case ADD_COLUMN: return !table_.empty() && !column_.empty() && !type_.empty();
      case DROP_COLUMN: return !table_.empty() && !column_.empty();
      case RENAME_COLUMN: return !table_.empty() && !column_.empty() && !newColumn_.empty();
      case RENAME_TABLE: return !table_.empty() && !newTable_.empty();
      default: return false;
    }
  }

  std::string build(const bool semicolon = false) const {
    if (!operator bool()) return {};
    switch (operation_) {
      case ADD_COLUMN: return std::format(R"(ALTER TABLE {} ADD COLUMN {} {}{}{})", table_, column_, type_, notNull_ ? " NOT NULL" : "", defaultValue_) + (semicolon ? ";" : "");
      case DROP_COLUMN: return std::format(R"(ALTER TABLE {} DROP COLUMN {})", table_, column_) + (semicolon ? ";" : "");
      case RENAME_COLUMN: return std::format(R"(ALTER TABLE {} RENAME COLUMN {} TO {})", table_, column_, newColumn_) + (semicolon ? ";" : "");
      case RENAME_TABLE: return std::format(R"(ALTER TABLE {} RENAME TO {})", table_, newTable_) + (semicolon ? ";" : "");
      default: return {};
    }
  }
};

} // namespace alkaid::SQL

#endif // ALKAID_DDL_H

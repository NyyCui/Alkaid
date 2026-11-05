#ifndef ALKAID_SQL_VALUE_H
#define ALKAID_SQL_VALUE_H

#include <concepts>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>
#include <nlohmann/json.hpp>

#include "alkaid_utils/local_date_time.h"
#include "alkaid_utils/str_util.h"
#include "alkaid_utils/variant_util.h"

namespace alkaid::SQL {

enum class ColumnType {
  INTEGER,
  REAL,
  BLOB,
  TEXT
};

class Val {
public:
  enum class Like : int32_t {
    NONE_LIKE  = 0,
    LEFT_LIKE  = 2,
    RIGHT_LIKE = 1,
    ALL_LIKE   = 3
  };

private:
  using ValType = std::variant<
    std::nullptr_t,                        ///< NULL
    int8_t, int16_t, int32_t, int64_t,     ///< 有符号整数
    uint8_t, uint16_t, uint32_t, uint64_t, ///< 无符号整数
    float,                                 ///< 单精度浮点
    double,                                ///< 双精度浮点
    std::string,                           ///< TEXT
    std::vector<uint8_t>                   ///< BLOB
  >;

  ValType val_;

  Like like_;

  bool quote_;

  Val(const ValType &val, const Like like, const bool quote = false) : val_(val)
                                                                     , like_(like)
                                                                     , quote_(quote) {}

public:
  Val() : Val(ValType(nullptr), Like::NONE_LIKE) {}

  Val(const Val &val) : Val(val.val_, val.like_, val.quote_) {}

  Val(const Val &&val) noexcept : Val(std::move(val.val_), val.like_, val.quote_) {}

  Val &operator=(const Val &val) {
    val_ = val.val_;
    like_ = val.like_;
    quote_ = val.quote_;
    return *this;
  }

  template<typename T>
    requires std::is_same_v<T, std::nullopt_t> ||
             std::is_same_v<T, std::nullptr_t>
  Val(T) : Val(ValType(nullptr), Like::NONE_LIKE) {}

  Val(const bool v) : Val(ValType(v ? 1 : 0), Like::NONE_LIKE) {}

  template<typename T>
    requires std::is_integral_v<T> ||
             std::is_floating_point_v<T>
  Val(const T v) : Val(ValType(v), Like::NONE_LIKE) {}

  Val(const char *v, const Like like = Like::NONE_LIKE, const bool quote = true) : Val(ValType(nullptr), Like::NONE_LIKE, false) {
    if (v != nullptr) {
      val_ = std::string(v);
      like_ = like;
      quote_ = quote;
    }
  }

  Val(const std::string &v, const Like like = Like::NONE_LIKE, const bool quote = true) : Val(ValType(v), like, quote) {}

  Val(const LocalDateTime &v, const bool quote = true) : Val(v.format(), Like::NONE_LIKE, quote) {}

  Val(const std::vector<uint8_t> &v) : Val(v, Like::NONE_LIKE) {}

  template<typename T>
    requires std::is_same_v<T, std::nullopt_t> ||
             std::is_same_v<T, std::nullptr_t> ||
             std::is_integral_v<T> ||
             std::is_floating_point_v<T> ||
             std::is_same_v<T, std::vector<uint8_t>>
  Val(const std::optional<T> &v) : Val(ValType(nullptr), Like::NONE_LIKE) {
    if (v.has_value()) {
      val_ = *v;
    }
  }

  Val(const std::optional<bool> &v, const Like like = Like::NONE_LIKE, const bool quote = true) : Val(ValType(nullptr), Like::NONE_LIKE, false) {
    if (v.has_value()) {
      val_ = *v ? 1 : 0;
      like_ = like;
      quote_ = quote;
    }
  }

  Val(const std::optional<const char *> &v, const Like like = Like::NONE_LIKE, const bool quote = true) : Val(ValType(nullptr), Like::NONE_LIKE, false) {
    if (v.has_value() && v.value() != nullptr) {
      val_ = std::string(v.value());
      like_ = like;
      quote_ = quote;
    }
  }

  Val(const std::optional<std::string> &v, const Like like = Like::NONE_LIKE, const bool quote = true) : Val(ValType(nullptr), Like::NONE_LIKE, false) {
    if (v.has_value()) {
      val_ = *v;
      like_ = like;
      quote_ = quote;
    }
  }

  Val(const std::optional<LocalDateTime> &v, const bool quote = true) : Val(ValType(nullptr), Like::NONE_LIKE, false) {
    if (v.has_value()) {
      val_ = v->format();
      quote_ = quote;
    }
  }

  Val(const std::optional<std::vector<uint8_t>> &v) : Val(ValType(nullptr), Like::NONE_LIKE) {
    if (v.has_value()) {
      val_ = *v;
    }
  }

  static Val colVal(const std::string &columnName) {
    return Val(ValType(columnName), Like::NONE_LIKE, false);
  }

  template<typename F>
  decltype(auto) visit(F &&f) const {
    return std::visit(std::forward<F>(f), val_);
  }

  bool isNull() const {
    return std::holds_alternative<std::nullptr_t>(val_);
  }

  Like like() const {
    return like_;
  }

  std::string toString() const {
    return std::visit(VariantUtil::Overloaded{
                        [](const std::nullptr_t) {
                          return std::string("NULL");
                        },
                        [](const std::integral auto v) {
                          return std::to_string(v);
                        },
                        [](const std::floating_point auto v) {
                          return std::to_string(v);
                        },
                        [like = this->like_, quote = this->quote_](const std::string &v) {
                          std::string res = "";
                          switch (like) {
                            case Like::LEFT_LIKE: res = std::string("%" + v);
                              break;
                            case Like::RIGHT_LIKE: res = std::string(v + "%");
                              break;
                            case Like::ALL_LIKE: res = std::string("%" + v + "%");
                              break;
                            default: res = v;
                          }
                          return quote ? "'" + res + "'" : res;
                        },
                        [](const std::vector<uint8_t> &v) {
                          return "X'" + StrUtil::toHex(v) + "'";
                        }
                      }, val_
                     );
  }
};

class Slot {
  std::string m_name;
  std::string m_val;

  explicit Slot(const std::string &name, const std::string &val) : m_name(name)
                                                                 , m_val(val) {}

public:
  static Slot $(const std::string &name) {
    return Slot(name, ":" + name);
  }

  static Slot _(const std::string &name, const bool quote = false) {
    if (quote) return Slot(name, "'" + ("{" + name + "}") + "'");
    return Slot(name, "{" + name + "}");
  }

  const std::string &name() const {
    return m_name;
  }

  std::string toString() const {
    return m_val;
  }
};

using Operand = std::variant<Val, Slot>;

using Params = std::unordered_map<std::string, Val>;

} // namespace alkaid::SQL
#endif

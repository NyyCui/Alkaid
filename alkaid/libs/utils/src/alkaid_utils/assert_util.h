#ifndef ALKAID_ASSERT_H
#define ALKAID_ASSERT_H
#include "concepts.h"
#include "exceptions.h"
#include "str_util.h"

namespace alkaid::Assert {

using Concept::Boolean;
using Concept::ContainerLike;
using Concept::MapLike;
using Concept::StringLike;

/**
 * @brief 断言条件为真
 *
 * @param condition 布尔条件，受 Boolean 概念约束
 * @param fmt 异常格式字符串
 * @param args 格式化参数
 * @throws std::runtime_error 当条件为假时
 *
 * @code
 * Assert::isTrue(value > 0, "值 {} 必须大于 0", value);
 * @endcode
 */
template<Boolean B, typename... Args>
void isTrue(const B &condition, const std::string &tpl, const Args &... args) {
  if (!condition) {
    throw Exceptions::of(tpl, args...);
  }
}

/**
 * @brief 断言条件为假
 *
 * @param condition 布尔条件，受 Boolean 概念约束
 * @param fmt 异常格式字符串
 * @param args 格式化参数
 * @throws std::runtime_error 当条件为真时
 *
 * @code
 * Assert::isFalse(hasError, "不应出现错误：{}", errorMsg);
 * @endcode
 */
template<Boolean B, typename... Args>
void isFalse(const B &condition, const std::string &tpl, const Args &... args) {
  if (condition) {
    throw Exceptions::of(tpl, args...);
  }
}

/**
 * @brief 断言指针为空
 *
 * @param data 待检查的指针
 * @param fmt 异常格式字符串
 * @param args 格式化参数
 * @throws std::runtime_error 当指针不为空指针时
 *
 * @code
 * Assert::isNull(result, "预期返回空指针，实际得到 {}", result);
 * @endcode
 */
template<typename... Args>
void isNull(const void *data, const std::string &tpl, const Args &... args) {
  if (data != nullptr) {
    throw Exceptions::of(tpl, args...);
  }
}

/**
 * @brief 断言指针不为空
 *
 * @param data 待检查的指针
 * @param fmt 异常格式字符串
 * @param args 格式化参数
 * @throws std::runtime_error 当指针为空指针时
 *
 * @code
 * Assert::notNull(ptr, "指针 {} 不能为空", "ptr");
 * @endcode
 */
template<typename... Args>
void notNull(const void *data, const std::string &tpl, const Args &... args) {
  if (data == nullptr) {
    throw Exceptions::of(tpl, args...);
  }
}

/**
 * @brief 断言两个指针相等
 *
 * @param data 第一个指针
 * @param other 第二个指针
 * @param fmt 异常格式字符串
 * @param args 格式化参数
 * @throws std::runtime_error 当两个指针不相等时
 *
 * @code
 * Assert::equals(actual, expected, "期望指针 {}，实际为 {}", expected, actual);
 * @endcode
 */
template<typename... Args>
void equals(const void *data, const void *other, const std::string &tpl, const Args &... args) {
  if (data != other) {
    throw Exceptions::of(tpl, args...);
  }
}

/**
 * @brief 断言两个指针不相等
 *
 * @param data 第一个指针
 * @param other 第二个指针
 * @param fmt 异常格式字符串
 * @param args 格式化参数
 * @throws std::runtime_error 当两个指针相等时
 *
 * @code
 * Assert::notEquals(ptr1, ptr2, "两个指针不应指向同一地址");
 * @endcode
 */
template<typename... Args>
void notEquals(const void *data, const void *other, const std::string &tpl, const Args &... args) {
  if (data == other) {
    throw Exceptions::of(tpl, args...);
  }
}

/**
 * @brief 断言字符串为空白（非空指针、长度不为 0、全为空白字符）
 *
 * @param data 字符串数据，支持 std::string / std::string_view / 字符指针类型
 * @param fmt 异常格式字符串
 * @param args 格式化参数
 * @throws std::runtime_error 当字符串不为空白时（即非空指针、长度不为 0 且包含非空白字符）
 *
 * @code
 * Assert::isBlank(name, "名称应为空白");
 * Assert::isBlank(input, "输入 {} 应为空白", "field");
 * @endcode
 */
template<StringLike S, typename... Args>
void isBlank(const S &data, const std::string &tpl, const Args &... args) {
  if (StrUtil::notBlank(data)) throw Exceptions::of(tpl, args...);
}

/**
 * @brief 断言字符串不是空白（非空指针、长度不为 0、不全为空白字符）
 *
 * @param data 字符串数据，支持 std::string / std::string_view / 字符指针类型
 * @param fmt 异常格式字符串
 * @param args 格式化参数
 * @throws std::runtime_error 当字符串为空白时（即空指针、长度为 0 或全为空白字符）
 *
 * @code
 * Assert::notBlank(name, "名称不能为空白");
 * Assert::notBlank(input, "输入 {} 不能为空白", "field");
 * @endcode
 */
template<StringLike S, typename... Args>
void notBlank(const S &data, const std::string &tpl, const Args &... args) {
  if (StrUtil::isBlank(data)) throw Exceptions::of(tpl, args...);
}

/**
 * @brief 断言字符串为空（空指针或长度为 0）
 *
 * @param data 字符串数据，支持 std::string / std::string_view / 字符指针类型
 * @param fmt 异常格式字符串
 * @param args 格式化参数
 * @throws std::runtime_error 当字符串不为空时（即非空指针且长度大于 0）
 *
 * @code
 * Assert::isEmpty(str, "字符串应为空");
 * Assert::isEmpty(path, "路径 {} 应为空", "configPath");
 * @endcode
 */
template<StringLike S, typename... Args>
void isEmpty(const S &data, const std::string &tpl, const Args &... args) {
  if (StrUtil::notEmpty(data)) throw Exceptions::of(tpl, args...);
}

/**
 * @brief 断言字符串不为空（非空指针且长度大于 0）
 *
 * @param data 字符串数据，支持 std::string / std::string_view / 字符指针类型
 * @param fmt 异常格式字符串
 * @param args 格式化参数
 * @throws std::runtime_error 当字符串为空时（即空指针或长度为 0）
 *
 * @code
 * Assert::notEmpty(str, "字符串不能为空");
 * Assert::notEmpty(path, "路径 {} 不能为空", "configPath");
 * @endcode
 */
template<StringLike S, typename... Args>
void notEmpty(const S &data, const std::string &tpl, const Args &... args) {
  if (StrUtil::isEmpty(data)) throw Exceptions::of(tpl, args...);
}

/**
 * @brief 断言容器为空（长度为 0）
 *
 * @param data 容器数据，如 std::vector、std::array 等
 * @param fmt 异常格式字符串
 * @param args 格式化参数
 * @throws std::runtime_error 当容器不为空时（即长度大于 0）
 *
 * @code
 * Assert::isEmpty(vec, "列表应为空");
 * Assert::isEmpty(items, "项目列表 {} 应为空", "items");
 * @endcode
 */
template<ContainerLike C, typename... Args>
void isEmpty(const C &data, const std::string &tpl, const Args &... args) {
  if (data.size() != 0) {
    throw Exceptions::of(tpl, args...);
  }
}

/**
 * @brief 断言容器不为空（长度大于 0）
 *
 * @param data 容器数据，如 std::vector、std::array 等
 * @param fmt 异常格式字符串
 * @param args 格式化参数
 * @throws std::runtime_error 当容器为空时（即长度为 0）
 *
 * @code
 * Assert::notEmpty(vec, "列表不能为空");
 * Assert::notEmpty(items, "项目列表 {} 不能为空", "items");
 * @endcode
 */
template<ContainerLike C, typename... Args>
void notEmpty(const C &data, const std::string &tpl, const Args &... args) {
  if (data.size() == 0) {
    throw Exceptions::of(tpl, args...);
  }
}

/**
 * @brief 断言键值对容器为空（长度为 0）
 *
 * @param data 键值对容器，如 std::map、std::unordered_map 等
 * @param fmt 异常格式字符串
 * @param args 格式化参数
 * @throws std::runtime_error 当容器不为空时（即长度大于 0）
 *
 * @code
 * Assert::isEmpty(config, "配置应为空");
 * Assert::isEmpty(headers, "请求头 {} 应为空", "headers");
 * @endcode
 */
template<MapLike M, typename... Args>
void isEmpty(const M &data, const std::string &tpl, const Args &... args) {
  if (data.size() != 0) {
    throw Exceptions::of(tpl, args...);
  }
}

/**
 * @brief 断言键值对容器不为空（长度大于 0）
 *
 * @param data 键值对容器，如 std::map、std::unordered_map 等
 * @param fmt 异常格式字符串
 * @param args 格式化参数
 * @throws std::runtime_error 当容器为空时（即长度为 0）
 *
 * @code
 * Assert::notEmpty(config, "配置不能为空");
 * Assert::notEmpty(headers, "请求头 {} 不能为空", "headers");
 * @endcode
 */
template<MapLike M, typename... Args>
void notEmpty(const M &data, const std::string &tpl, const Args &... args) {
  if (data.size() == 0) {
    throw Exceptions::of(tpl, args...);
  }
}
} // namespace alkaid::Assert
#endif // ALKAID_ASSERT_H

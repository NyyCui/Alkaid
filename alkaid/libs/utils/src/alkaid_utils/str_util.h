#ifndef ALKAID_STR_UTIL_H
#define ALKAID_STR_UTIL_H
#include "concepts.h"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

/**
 * @file str_util.h
 * @brief 字符串工具函数，提供 format 模板格式化、isEmpty / isBlank 检查功能
 *
 * 提供类似 Python str.format() 的字符串格式化能力：
 * - 位置占位符 {} 按顺序填充普通参数
 * - 命名占位符 {name} 通过 makeArg("name", value) 填充
 * - {{ 和 }} 转义为字面量 { 和 }
 */

namespace alkaid::StrUtil {

// 替换【第一个】匹配项，返回新串
constexpr std::string replaceFirst(
  std::string_view src,
  std::string_view from,
  std::string_view to
) {
  if (from.empty()) return std::string{src};
  const auto pos = src.find(from);
  if (pos == std::string_view::npos) return std::string{src};

  const auto tail = src.substr(pos + from.size());
  std::string result;
  result.reserve(pos + to.size() + tail.size());
  result.append(src.data(), pos);
  result.append(to.data(), to.size());
  result.append(tail.data(), tail.size());
  return result;
}

// 替换【最后一个】匹配项，返回新串
constexpr std::string replaceLast(
  std::string_view src,
  std::string_view from,
  std::string_view to
) {
  if (from.empty()) return std::string{src};
  const auto pos = src.rfind(from);
  if (pos == std::string_view::npos) return std::string{src};

  const auto tail = src.substr(pos + from.size());
  std::string result;
  result.reserve(pos + to.size() + tail.size());
  result.append(src.data(), pos);
  result.append(to.data(), to.size());
  result.append(tail.data(), tail.size());
  return result;
}

// 全部替换
constexpr std::string replaceAll(
  std::string_view src,
  std::string_view from,
  std::string_view to
) {
  if (from.empty()) return std::string{src};

  const auto fromLen = from.size();
  const auto toLen = to.size();

  size_t count = 0;
  size_t prev = 0;
  while (src.find(from, prev) != std::string_view::npos) {
    ++count;
    prev = src.find(from, prev) + fromLen;
  }

  if (count == 0) return std::string{src};

  std::string result;
  result.reserve(src.size() + count * (toLen - fromLen));

  prev = 0;
  size_t pos;
  while ((pos = src.find(from, prev)) != std::string_view::npos) {
    result.append(src.data() + prev, pos - prev);
    result.append(to.data(), toLen);
    prev = pos + fromLen;
  }
  result.append(src.data() + prev, src.size() - prev);
  return result;
}

/**
 * @brief 判断字符串是否为空（空指针、长度为 0）
 *
 * 支持 std::string、std::string_view、字符指针类型。
 * 对于指针类型，额外检查 nullptr。
 *
 * @param data 字符串数据，受 StringLike 概念约束
 * @return 当字符串为空指针或长度为 0 时返回 true
 *
 * @code
 * isEmpty(nullptr)       // true
 * isEmpty("")            // true
 * isEmpty("hello")       // false
 * isEmpty(std::string{}) // true
 * @endcode
 */
template<Concept::StringLike S>
constexpr bool isEmpty(const S &data) {
  if constexpr (std::is_pointer_v<std::remove_cvref_t<S>>) {
    if (data == nullptr) {
      return true;
    }
  }
  return data.empty();
}

/**
 * @brief 判断字符串是否不为空（非空指针且长度大于 0）
 *
 * 支持 std::string、std::string_view、字符指针类型。
 * 对于指针类型，额外检查 nullptr。
 *
 * @param data 字符串数据，受 StringLike 概念约束
 * @return 当字符串非空（非空指针且长度大于 0）时返回 true
 *
 * @code
 * notEmpty(nullptr)       // false
 * notEmpty("")            // false
 * notEmpty("hello")       // true
 * notEmpty(std::string{}) // false
 * @endcode
 */
template<Concept::StringLike S>
constexpr bool notEmpty(const S &data) {
  return !isEmpty(data);
}

/**
 * @brief 判断字符串是否为空白（空指针、长度为 0、全为空白字符）
 *
 * 支持 std::string、std::string_view、字符指针类型。
 * 对于指针类型，额外检查 nullptr。
 *
 * @param data 字符串数据，受 StringLike 概念约束
 * @return 当字符串为空指针、长度为 0 或全为空白字符时返回 true
 *
 * @code
 * isBlank(nullptr)         // true
 * isBlank("")              // true
 * isBlank("   ")           // true
 * isBlank("  \t\n  ")      // true
 * isBlank("hello")         // false
 * @endcode
 */
template<Concept::StringLike S>
bool isBlank(const S &data) {
  if constexpr (std::is_pointer_v<std::remove_cvref_t<S>>) {
    if (data == nullptr) {
      return true;
    }
  }
  std::string_view view = data;
  if (view.empty()) {
    return true;
  }
  return std::ranges::all_of(view, [](const unsigned char ch) {
    return std::isspace(ch) != 0;
  });
}

/**
 * @brief 判断字符串是否不为空白（非空指针、长度大于 0 且包含非空白字符）
 *
 * 支持 std::string、std::string_view、字符指针类型。
 * 对于指针类型，额外检查 nullptr。
 *
 * @param data 字符串数据，受 StringLike 概念约束
 * @return 当字符串非空且包含非空白字符时返回 true
 *
 * @code
 * notBlank(nullptr)         // false
 * notBlank("")              // false
 * notBlank("   ")           // false
 * notBlank("  \t\n  ")      // false
 * notBlank("hello")         // true
 * @endcode
 */
template<Concept::StringLike S>
bool notBlank(const S &data) {
  return !isBlank(data);
}

enum HexFlag : uint8_t {
  None     = 0,
  Prefix0x = 1 << 0,
  SuffixH  = 1 << 1,
};

/**
 * @brief 将字节数组转换为十六进制字符串表示
 *
 * 每个字节转换为两个大写十六进制字符（0-9A-F）。
 * 可通过参数控制是否添加 "0x" 前缀或 "H" 后缀。
 *
 * @param data 输入的字节数组
 * @param hexFlag 十六进制标志，控制是否添加 "0x" 前缀或 "H" 后缀，默认 None
 * @return 十六进制字符串表示
 *
 * @code
 * toHex({0xDE, 0xAD, 0xBE, 0xEF})                                    // "DEADBEEF"
 * toHex({0xDE, 0xAD}, Prefix0x)                                      // "0xDEAD"
 * toHex({0xDE, 0xAD}, _::HexFlag::SuffixH)                           // "DEADH"
 * toHex({0xDE, 0xAD}, _::HexFlag::Prefix0x | _::HexFlag::SuffixH)    // "0xDEADH"
 * toHex({})                                                          // ""
 * @endcode
 */
inline std::string toHex(const std::vector<uint8_t> &data, const HexFlag hexFlag = None) {
  static constexpr std::string_view HEX_CHARS = "0123456789ABCDEF";
  std::string result;
  result.reserve((hexFlag & Prefix0x ? 2 : 0) + data.size() * 2 + (hexFlag & SuffixH ? 1 : 0));
  if (hexFlag & Prefix0x) {
    result += "0x";
  }
  for (const auto byte: data) {
    result += HEX_CHARS[(byte >> 4) & 0x0F];
    result += HEX_CHARS[byte & 0x0F];
  }
  if (hexFlag & SuffixH) {
    result += 'H';
  }
  return result;
}

/**
 * @brief 将十六进制字符串转换为字节数组
 *
 * 每两个十六进制字符转换为一个字节，支持大小写混合。
 * 自动剥离 "0x" 前缀和 "H" 后缀（与 toHex 的输出格式兼容）。
 *
 * @param hex 十六进制字符串
 * @return 转换后的字节数组
 * @throws std::invalid_argument 输入长度为奇数或包含非十六进制字符时抛出
 *
 * @code
 * fromHex("DEADBEEF")       // {0xDE, 0xAD, 0xBE, 0xEF}
 * fromHex("0xDEAD")         // {0xDE, 0xAD}
 * fromHex("DEADH")          // {0xDE, 0xAD}
 * fromHex("0xDEADH")        // {0xDE, 0xAD}
 * fromHex("deadbeef")       // {0xDE, 0xAD, 0xBE, 0xEF}
 * fromHex("")               // {}
 * @endcode
 */
inline std::vector<uint8_t> fromHex(const std::string &hex) {
  std::string_view view(hex);

  if (view.starts_with("0x") || view.starts_with("0X")) {
    view.remove_prefix(2);
  }
  if (view.ends_with('H') || view.ends_with('h')) {
    view.remove_suffix(1);
  }

  if (view.size() % 2 != 0) {
    throw std::invalid_argument("fromHex: 十六进制字符串长度为奇数: \"" + hex + "\"");
  }

  static constexpr auto hexCharToValue = [](const char ch) -> uint8_t {
    if (ch >= '0' && ch <= '9') {
      return static_cast<uint8_t>(ch - '0');
    }
    if (ch >= 'A' && ch <= 'F') {
      return static_cast<uint8_t>(ch - 'A' + 10);
    }
    if (ch >= 'a' && ch <= 'f') {
      return static_cast<uint8_t>(ch - 'a' + 10);
    }
    throw std::invalid_argument(
      std::string("fromHex: 非法十六进制字符 '") + ch + "'");
  };

  std::vector<uint8_t> result;
  result.reserve(view.size() / 2);
  for (std::size_t i = 0; i < view.size(); i += 2) {
    const auto high = hexCharToValue(view[i]);
    const auto low = hexCharToValue(view[i + 1]);
    result.push_back(static_cast<uint8_t>((high << 4) | low));
  }
  return result;
}

/**
 * @brief 去除字符串首尾空白字符
 *
 * 空白字符由 std::isspace 判定，包括空格、制表符、换行符等。
 *
 * @param src 输入字符串
 * @return 去除首尾空白后的新字符串
 *
 * @code
 * trim("  hello  ")   // "hello"
 * trim("\t\n hi \n")  // "hi"
 * trim("   ")         // ""
 * trim("")            // ""
 * @endcode
 */
inline std::string trim(std::string_view src) {
  const auto isSpace = [](const unsigned char ch) {
    return std::isspace(ch) != 0;
  };

  auto start = src.begin();
  while (start != src.end() && isSpace(static_cast<unsigned char>(*start))) {
    ++start;
  }

  auto end = src.end();
  while (end != start && isSpace(static_cast<unsigned char>(*(end - 1)))) {
    --end;
  }

  return std::string{start, end};
}

/// 按分隔符切分字符串，忽略不存在分隔符的情况
inline std::vector<std::string> split(const std::string &src, const char delimiter) {
  std::vector<std::string> result;
  std::string::size_type begin = 0;
  while (true) {
    const auto pos = src.find(delimiter, begin);
    result.emplace_back(src.substr(begin, pos - begin));
    if (pos == std::string::npos) break;
    begin = pos + 1;
  }
  return result;
}

/// 忽略大小写比较两个字符串是否相等
inline bool equalsIgnoreCase(const std::string &a, const std::string &b) {
  return a.size() == b.size()
         && std::equal(a.begin(), a.end(), b.begin(),
                       [](const unsigned char x, const unsigned char y) {
                         return std::tolower(x) == std::tolower(y);
                       });
}

/**
 * @brief 驼峰命名转下划线命名（如 createTime → create_time）
 *
 * 大写字母转小写；仅当前一个字符为小写字母或数字时插入下划线，
 * 连续大写（如 userID → user_id）不会在下划线中间重复插入。
 *
 * @param str 驼峰命名字符串
 * @return 下划线命名字符串
 */
inline std::string toUnderlineCase(const std::string &str) {
  std::string result;
  result.reserve(str.size());
  for (std::size_t i = 0; i < str.size(); ++i) {
    const auto ch = static_cast<unsigned char>(str[i]);
    if (std::isupper(ch)) {
      if (i > 0) {
        const auto prev = static_cast<unsigned char>(str[i - 1]);
        if (std::islower(prev) || std::isdigit(prev)) result += '_';
      }
      result += static_cast<char>(std::tolower(ch));
    } else {
      result += str[i];
    }
  }
  return result;
}

/**
 * @brief 使用分隔符连接字符串序列
 *
 * @tparam S 序列元素类型，受 StringLike 概念约束，
 *            且须可隐式转换为 std::string_view（std::string、std::string_view、const char* 等）
 * @param parts 待连接的字符串序列
 * @param delimiter 分隔符
 * @return 连接后的字符串，parts 为空时返回空串
 *
 * @code
 * join({"a", "b", "c"}, "-")   // "a-b-c"
 * join({}, "-")                // ""
 * join({"x"}, ",")             // "x"
 * @endcode
 */
template<Concept::StringLike S>
  requires std::convertible_to<const S &, std::string_view>
inline std::string join(const std::vector<S> &parts, const std::string_view delimiter) {
  if (parts.empty()) {
    return {};
  }

  // 精确统计总长度：各元素长度之和 + 分隔符长度之和，避免重复扩容
  std::size_t totalLen = (parts.size() - 1) * delimiter.size();
  for (const auto &part: parts) {
    totalLen += std::string_view(part).size();
  }

  std::string result;
  result.reserve(totalLen);
  result.append(parts.front());
  for (std::size_t i = 1; i < parts.size(); ++i) {
    result.append(delimiter);
    result.append(parts[i]);
  }
  return result;
}

template<typename T>
struct NamedArg;

namespace _ {

/**
 * @brief 类型 trait：判断 T 是否为 NamedArg 实例
 *
 * 主模板：所有非 NamedArg 类型为 false_type
 */
template<typename T>
struct isNamedArg: std::false_type {
};

/**
 * @brief 偏特化：NamedArg<T> 为 true_type
 */
template<typename T>
struct isNamedArg<NamedArg<T>>: std::true_type {
};

/**
 * @brief isNamedArg 的便捷变量模板
 * @tparam T 待检查的类型
 */
template<typename T>
inline constexpr bool isNamedArgV = isNamedArg<T>::value;

} // namespace _

/**
 * @brief 将任意可打印类型转换为 std::string
 *
 * 按优先级依次尝试以下转换方式：
 * 1. 可隐式转换为 std::string_view 的类型（const char*, std::string 等）
 * 2. bool 类型（输出 "true" / "false"）
 * 3. 支持 std::to_string 的类型（int, double 等算术类型）
 * 4. 支持 operator<<(ostream&) 的类型（通过 std::ostringstream）
 *
 * @param val 待转换的值
 * @return 转换后的字符串
 */
std::string toString(const auto &val) {
  if constexpr (std::is_convertible_v<decltype(val), std::string_view>) {
    return std::string(val);
  } else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(val)>,
                                      bool>) {
    return val ? "true" : "false";
  } else if constexpr (requires { std::to_string(val); }) {
    return std::to_string(val);
  } else {
    std::ostringstream oss;
    oss << val;
    return oss.str();
  }
}

/**
 * @struct NamedArg
 * @brief 命名参数，用于填充 {name} 命名占位符
 * @tparam T 值类型，由构造函数参数自动推导
 *
 * 通过 makeArg("key", value) 工厂函数创建，
 * 在 format 调用中与普通位置参数混合传递，命名占位符 {key} 会匹配对应的
 * NamedArg。 NamedArg 不会被位置占位符 {} 消费。
 *
 * @code
 * format("{name} is {age}", makeArg("name", "Alice"), makeArg("age", 30))
 * // => "Alice is 30"
 * @endcode
 */
template<typename T>
struct NamedArg {
  std::string key; ///< 字段名称，对应模板中的 {key}
  T value;         ///< 字段值，会被 toString 转换后填入

  /**
   * @brief 构造命名参数
   * @param key 字段名称
   * @param value 字段值
   */
  NamedArg(std::string key, T value)
    : key(std::move(key))
    , value(std::move(value)) {
  }
};

/**
 * @brief 类模板参数推导指南（CTAD），允许省略模板参数
 *
 * 例如：NamedArg("name", "Alice") 自动推导为 NamedArg<const char*>
 */
template<typename T>
NamedArg(std::string, T) -> NamedArg<T>;

/**
 * @brief 创建命名参数，用于 format 中的 {name} 占位符
 * @param key 字段名称，对应模板中的 {name}
 * @param value 字段值，支持任意可打印类型
 * @return NamedArg 实例
 *
 * @code
 * auto arg = makeArg("count", 42);
 * format("Count: {count}", arg); // => "Count: 42"
 * @endcode
 */
template<typename T>
constexpr NamedArg<T> makeArg(std::string key, T value) {
  return NamedArg<T>(std::move(key), std::move(value));
}

/**
 * @brief 格式化字符串
 *
 * 遍历格式模板 fmt，识别以下占位符并进行替换：
 *
 * | 语法       | 含义                                                   |
 * |------------|--------------------------------------------------------|
 * | {}         | 位置占位符，按顺序消费非 NamedArg 的参数                |
 * | {name}     | 命名占位符，匹配 NamedArg 的 key                       |
 * | {{         | 转义为字面量 {                                         |
 * | }}         | 转义为字面量 }                                         |
 *
 * 位置参数和命名参数可以混合使用，位置参数仅消费普通值（跳过 NamedArg），
 * 命名参数仅匹配 NamedArg（按 key 查找）。
 *
 * 当占位符无法匹配到对应参数时（位置参数不足、命名参数未找到），
 * 占位符将原样输出，不会抛出异常。
 *
 * @param fmt 格式模板字符串
 * @param args 可变参数包，可包含普通值和 NamedArg
 * @return 格式化后的字符串
 *
 * @throws std::invalid_argument 格式模板中存在未匹配的 '{' 或 '}'
 *
 * @code
 * // 位置占位符
 * format("Hello, {}!", "world");                        // "Hello, world!"
 * format("{} + {} = {}", 1, 2, 3);                      // "1 + 2 = 3"
 *
 * // 命名占位符
 * format("{name} is {age}",
 *        makeArg("name", "Alice"),
 *        makeArg("age", 30));                            // "Alice is 30"
 *
 * // 混合使用
 * format("{}: {msg}", "Info", makeArg("msg", "ok"));    // "Info: ok"
 *
 * // 转义
 * format("{{literal}}");                                 // "{literal}"
 *
 * // 参数不足时原样输出
 * format("{} {}", 1);                                    // "1 {}"
 * format("{name}", makeArg("other", 1));                 // "{name}"
 * @endcode
 */
template<typename... Args>
std::string format(const std::string_view fmt, const Args &... args) {
  std::string result;
  result.reserve(fmt.size() * 2);

  std::size_t pos = 0;    // 当前解析位置
  std::size_t posIdx = 0; // 位置占位符计数器，记录当前是第几个 {}

  while (pos < fmt.size()) {

    if (const char ch = fmt[pos]; ch == '{') {
      // 处理 {{ 转义：输出字面量 '{'
      if (pos + 1 < fmt.size() && fmt[pos + 1] == '{') {
        result += '{';
        pos += 2;
        continue;
      }

      // 查找配对的 '}'
      auto end = fmt.find('}', pos + 1);
      if (end == std::string_view::npos) {
        throw std::invalid_argument("format: 位置 " + std::to_string(pos) +
                                    " 处的 '{' 未找到配对的 '}'，模板: \"" +
                                    std::string(fmt) + "\"");
      }

      // 提取占位符内容（{} 内部的部分）
      std::string_view key = fmt.substr(pos + 1, end - pos - 1);

      if (key.empty()) {
        // ---- 位置占位符 {} ----
        // 遍历参数包，跳过 NamedArg，按顺序匹配第 posIdx 个普通参数
        if constexpr (sizeof...(Args) == 0) {
          result.append(fmt.data() + pos, end - pos + 1);
        } else {
          std::size_t currentPos = 0; // 普通参数计数器
          bool filled = false;

          auto tryFill = [&]<typename T0>(const T0 &arg) {
            if (filled) return;
            using ArgT = std::remove_cvref_t<T0>;
            // 跳过 NamedArg，只对普通值计数
            if constexpr (!_::isNamedArgV<ArgT>) {
              if (currentPos++ == posIdx) {
                result += toString(arg);
                filled = true;
              }
            }
          };

          (tryFill(args), ...);

          if (!filled) {
            result.append(fmt.data() + pos, end - pos + 1);
          }
          ++posIdx;
        }
      } else {
        // ---- 命名占位符 {name} ----
        // 遍历参数包，查找 key 匹配的 NamedArg
        bool found = false;

        if constexpr (sizeof...(Args) > 0) {
          auto tryMatch = [&]<typename T0>(const T0 &arg) {
            if (found) return;
            using ArgT = std::remove_cvref_t<T0>;
            if constexpr (_::isNamedArgV<ArgT>) {
              if (arg.key == key) {
                result += toString(arg.value);
                found = true;
              }
            }
          };

          (tryMatch(args), ...);
        }

        if (!found) {
          result.append(fmt.data() + pos, end - pos + 1);
        }
      }

      pos = end + 1;
    } else if (ch == '}') {
      // 处理 }} 转义：输出字面量 '}'
      if (pos + 1 < fmt.size() && fmt[pos + 1] == '}') {
        result += '}';
        pos += 2;
      } else {
        throw std::invalid_argument("format: 位置 " + std::to_string(pos) +
                                    " 处的 '}' 未找到配对的 '{'，模板: \"" +
                                    std::string(fmt) + "\"");
      }
    } else {
      // 普通字符段，批量追加以减少函数调用次数
      const auto start = pos;
      while (pos < fmt.size() && fmt[pos] != '{' && fmt[pos] != '}') {
        ++pos;
      }
      result.append(fmt.data() + start, pos - start);
    }
  }

  return result;
}

} // namespace common::StrUtil

#endif // ALKAID_STR_UTIL_H

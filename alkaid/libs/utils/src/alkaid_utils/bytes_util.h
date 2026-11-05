#ifndef ALKAID_BYTES_UTIL_H
#define ALKAID_BYTES_UTIL_H
#include "assert_util.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

/**
 * @file bytes_util.h
 * @brief 字节数组转换工具，支持字节数组与各种目标类型之间的双向转换
 */
namespace alkaid::BytesUtil {

// region bool
/**
 * @brief 将字节数组中的指定字节转换为 bool
 * @param bytes 源字节数组
 * @param offset 起始偏移量，默认为 0（小于 0 从末尾偏移，大于等于 0 从开头偏移）
 * @return 偏移位置处字节非零时为 true，否则为 false
 */
inline bool readBool(const std::vector<uint8_t> &bytes, const int64_t offset = 0) {
  const auto n = static_cast<int64_t>(bytes.size());
  const int64_t pos = offset >= 0 ? offset : n + offset;
  Assert::isTrue(pos >= 0 && pos < n, "访问越界");
  return bytes[static_cast<std::size_t>(pos)] != 0;
}

/**
 * @brief 从原始字节指针处读取 bool
 * @param data 指向字节数据的指针
 * @return 指针处字节非零时为 true，否则为 false
 */
inline bool readBool(const uint8_t *data) {
  Assert::notNull(data, "字节指针不能为空");
  return *data != 0;
}

/**
 * @brief 将 bool 转换为字节数组
 * @param value 源布尔值
 * @return 字节数组
 */
inline std::vector<uint8_t> writeBool(const bool value) {
  return {static_cast<uint8_t>(value)};
}

/**
 * @brief 将 bool 的字节写入字节数组
 * @param value 源布尔值
 * @param bytes 目标字节数组
 * @param offset 偏移量，默认为 0（小于 0 从末尾偏移，大于等于 0 从开头偏移）
 */
inline void writeBool(const bool value, std::vector<uint8_t> &bytes, const int64_t offset = -1) {
  const auto n = static_cast<int64_t>(bytes.size());
  const int64_t pos = n == 0 && offset == -1 ? 0 : offset >= 0 ? offset
                                                               : n + offset;
  Assert::isTrue(pos >= 0 && pos < n, "访问越界");
  bytes.insert(bytes.begin() + pos, value);
}
// endregion

// region char
/**
 * @brief 将字节数组中的指定字节转换为 char
 * @param bytes 源字节数组
 * @param offset 起始偏移量，默认为 0（小于 0 从末尾偏移，大于等于 0 从开头偏移）
 * @return 偏移位置处的字节作为 char 返回
 */
inline char readChar(const std::vector<uint8_t> &bytes, const int64_t offset = 0) {
  const auto n = static_cast<int64_t>(bytes.size());
  const int64_t pos = offset >= 0 ? offset : n + offset;
  Assert::isTrue(pos >= 0 && pos < n, "访问越界");
  return static_cast<char>(bytes[static_cast<std::size_t>(pos)]);
}

/**
 * @brief 从原始字节指针处读取 char
 * @param data 指向字节数据的指针
 * @return 指针处的字节作为 char 返回
 */
inline char readChar(const uint8_t *data) {
  Assert::notNull(data, "字节指针不能为空");
  return static_cast<char>(*data);
}

/**
 * @brief 将 char 转换为字节数组
 * @param value 源字符值
 * @return 字节数组
 */
inline std::vector<uint8_t> writeChar(const char value) {
  return {static_cast<uint8_t>(value)};
}

/**
 * @brief 将 char 的字节写入字节数组
 *
 * @param value 源字符值
 * @param bytes 目标字节数组
 * @param offset 偏移量，默认为 0（小于 0 从末尾偏移，大于等于 0 从开头偏移）
 */
inline void writeChar(const char value, std::vector<uint8_t> &bytes, const int64_t offset = -1) {
  const auto n = static_cast<int64_t>(bytes.size());
  const int64_t pos = n == 0 && offset == -1 ? 0 : offset >= 0 ? offset
                                                               : n + offset;
  Assert::isTrue(pos >= 0 && pos < n, "访问越界");
  bytes.insert(bytes.begin() + pos, static_cast<uint8_t>(value));
}
// endregion

// region string
/**
 * @brief 将字节数组转换为 std::string
 * @param bytes 源字节数组
 * @param offset 起始偏移量，默认为 0（小于 0 从末尾偏移，大于等于 0 从开头偏移  ）
 * @param length 要转换的字节数，默认为 0（从偏移到数组末尾）
 * @return 转换后的字符串
 */
inline std::string readString(const std::vector<uint8_t> &bytes,
                              const std::size_t length = 0,
                              const int64_t offset = 0) {
  if (bytes.empty()) return {};
  const auto n = static_cast<int64_t>(bytes.size());
  const int64_t pos = offset >= 0 ? offset : n + offset;
  Assert::isTrue(pos >= 0 && pos < n, "访问越界");
  const std::size_t actualLength = length == 0 ? static_cast<std::size_t>(n - pos) : length;
  Assert::isTrue(pos + static_cast<int64_t>(actualLength) <= n, "访问越界");
  return {reinterpret_cast<const char *>(bytes.data() + pos), actualLength};
}

/**
 * @brief 从原始字节指针转换为 std::string
 * @param data 指向字节数据的指针
 * @param length 要转换的字节数
 * @return 转换后的字符串
 */
inline std::string readString(const uint8_t *data, std::size_t length) {
  Assert::notNull(data, "字节指针不能为空");
  return {reinterpret_cast<const char *>(data), length};
}

/**
 * @brief 将 std::string 转换为字节数组
 * @param str 源字符串
 * @return 字节数组
 */
inline std::vector<uint8_t> writeString(const std::string &str) {
  if (str.empty()) return {};
  return {reinterpret_cast<const uint8_t *>(str.data()),
          reinterpret_cast<const uint8_t *>(str.data() + str.size())};
}

/**
 * @brief 将 std::string 的字节写入字节数组
 * @param str 源字符串
 * @param bytes 目标字节数组，字符串字节将插入到偏移位置
 * @param offset 偏移量，默认为 0（小于 0 从末尾偏移，大于等于 0 从开头偏移）
 */
inline void writeString(const std::string &str, std::vector<uint8_t> &bytes, const int64_t offset = -1) {
  if (str.empty()) return;
  const auto *begin = reinterpret_cast<const uint8_t *>(str.data());
  const auto n = static_cast<int64_t>(bytes.size());
  const int64_t pos = n == 0 && offset == -1 ? 0 : offset >= 0 ? offset
                                                               : n + offset;
  Assert::isTrue(pos >= 0 && pos < n, "访问越界");

  bytes.insert(bytes.begin() + pos, begin, begin + str.size());
}

// endregion

// region integer
/**
 * @brief 从原始字节指针处读取整型（长度按类型自动计算）
 * @tparam T 目标整型类型，必须为整数类型
 * @param data 指向字节数据的指针
 * @param endian 字节序，默认为小端序
 * @return 转换后的整型值
 */
template <typename T>
  requires std::integral<T>
T readInteger(const uint8_t *data, const std::endian endian = std::endian::little) {
  Assert::notNull(data, "字节指针不能为空");
  if constexpr (constexpr std::size_t size = sizeof(T); size == 1) {
    return static_cast<T>(data[0]);
  } else {
    using U = std::make_unsigned_t<T>;
    U result = 0;
    for (std::size_t i = 0; i < size; ++i) {
      const std::size_t byteIndex = endian == std::endian::little ? i : size - 1 - i;
      result |= static_cast<U>(data[i]) << (byteIndex * 8);
    }
    return static_cast<T>(result);
  }
}

/**
 * @brief 将字节数组中的字节转换为整型
 * @tparam T 目标整型类型，必须为整数类型
 * @param bytes 源字节数组
 * @param offset 起始偏移量，默认为 0（小于 0 从末尾偏移，大于等于 0 从开头偏移）
 * @param endian 字节序，默认为小端序
 * @return 转换后的整型值
 */
template <typename T>
  requires std::integral<T>
T readInteger(const std::vector<uint8_t> &bytes, const int64_t offset = 0,
              const std::endian endian = std::endian::little) {
  constexpr std::size_t size = sizeof(T);
  const auto n = static_cast<int64_t>(bytes.size());
  const int64_t pos = offset >= 0 ? offset : n + offset;
  Assert::isTrue(pos >= 0 && pos < n, "访问越界");
  Assert::isTrue(pos + static_cast<int64_t>(size) <= n, "访问越界");
  return readInteger<T>(bytes.data() + pos, endian);
}

/**
 * @brief 将整型值转换为字节数组
 * @tparam T 整型类型，必须为整数类型
 * @param value 源整型值
 * @param endian 字节序，默认为小端序
 * @return 转换后的字节数组，大小为 sizeof(T)
 */
template <typename T>
  requires std::integral<T>
std::vector<uint8_t> writeInteger(T value, const std::endian endian = std::endian::little) {
  constexpr std::size_t size = sizeof(T);
  std::vector<uint8_t> result(size);
  if constexpr (size == 1) {
    result[0] = static_cast<uint8_t>(value);
  } else {
    using U = std::make_unsigned_t<T>;
    const U unsignedValue = static_cast<U>(value);
    for (std::size_t i = 0; i < size; ++i) {
      const std::size_t byteIndex = endian == std::endian::little ? i : size - 1 - i;
      result[i] = static_cast<uint8_t>(unsignedValue >> (byteIndex * 8));
    }
  }
  return result;
}

/**
 * @brief 将整型值的字节写入字节数组
 * @tparam T 整型类型，必须为整数类型
 * @param value 源整型值
 * @param bytes 目标字节数组，整型字节将插入到偏移位置
 * @param offset 偏移量，默认为 0（小于 0 从末尾偏移，大于等于 0 从开头偏移）
 * @param endian 字节序，默认为小端序
 */
template <typename T>
  requires std::integral<T>
void writeInteger(T value, std::vector<uint8_t> &bytes, const int64_t offset = -1,
                  const std::endian endian = std::endian::little) {
  constexpr std::size_t size = sizeof(T);
  const auto n = static_cast<int64_t>(bytes.size());
  const int64_t index = n == 0 && offset == -1 ? 0 : offset >= 0 ? offset
                                                                 : n + offset;
  if (n != 0) Assert::isTrue(index >= 0 && index < n, "访问越界");

  std::vector<uint8_t> tmp;
  tmp.reserve(size);

  using U = std::make_unsigned_t<T>;
  const U u = static_cast<U>(value);

  if constexpr (size == 1) {
    tmp.push_back(static_cast<uint8_t>(u));
  } else {
    for (std::size_t i = 0; i < size; ++i) {
      const std::size_t byteIndex =
          endian == std::endian::little ? i : size - 1 - i;
      tmp.push_back(static_cast<uint8_t>(u >> (byteIndex * 8)));
    }
  }

  bytes.insert(bytes.begin() + index,
               tmp.begin(), tmp.end());
}

// endregion

// region floating point
/**
 * @brief 从原始字节指针处读取浮点值（长度按类型自动计算）
 *
 * 读取 sizeof(T) 个字节，按指定端序处理后通过 std::memcpy 转换为浮点值。
 *
 * @tparam T 目标浮点类型（float 或 double）
 * @param data 指向字节数据的指针
 * @param endian 字节序，默认为小端序
 * @return 转换后的浮点值
 * @throws std::runtime_error 字节指针为空
 */
template <typename T>
  requires std::floating_point<T>
T readFloatingPoint(const uint8_t *data, const std::endian endian = std::endian::little) {
  Assert::notNull(data, "字节指针不能为空");
  constexpr std::size_t size = sizeof(T);
  std::array<std::uint8_t, size> temp;
  if (endian == std::endian::little) {
    std::memcpy(temp.data(), data, size);
  } else {
    std::copy(data, data + size, temp.rbegin());
  }

  T result;
  std::memcpy(&result, temp.data(), size);
  return result;
}

/**
 * @brief 将字节数组中的字节转换为浮点型
 *
 * 从偏移量处读取 sizeof(T) 个字节，按指定端序处理后通过 std::memcpy 转换为浮点值。
 *
 * @tparam T 目标浮点类型（float 或 double）
 * @param bytes 源字节数组
 * @param offset 起始偏移量，默认为 0（小于 0 从末尾偏移，大于等于 0 从开头偏移）
 * @param endian 字节序，默认为小端序
 * @return 转换后的浮点值
 * @throws std::runtime_error 偏移量或数据长度不足
 */
template <typename T>
  requires std::floating_point<T>
T readFloatingPoint(const std::vector<uint8_t> &bytes, const int64_t offset = 0,
                    const std::endian endian = std::endian::little) {
  constexpr std::size_t size = sizeof(T);
  const auto n = static_cast<int64_t>(bytes.size());
  const int64_t pos = offset >= 0 ? offset : n + offset;
  Assert::isTrue(pos >= 0 && pos <= n, "访问越界");
  Assert::isTrue(pos + static_cast<int64_t>(size) <= n, "访问越界");
  return readFloatingPoint<T>(bytes.data() + pos, endian);
}

/**
 * @brief 将浮点值转换为字节数组
 *
 * 通过 std::memcpy 获取浮点值的字节表示，按指定端序处理后存入字节数组。
 *
 * @tparam T 浮点类型（float 或 double）
 * @param value 源浮点值
 * @param endian 字节序，默认为小端序
 * @return 转换后的字节数组，大小为 sizeof(T)
 */
template <typename T>
  requires std::floating_point<T>
std::vector<uint8_t> writeFloatingPoint(
    T value, const std::endian endian = std::endian::little) {
  constexpr std::size_t size = sizeof(T);
  std::vector<uint8_t> result(size);
  std::memcpy(result.data(), &value, size);
  if (endian == std::endian::big) {
    std::ranges::reverse(result);
  }
  return result;
}

/**
 * @brief 将浮点值的字节写入字节数组
 *
 * 通过 std::memcpy 获取浮点值的字节表示，按指定端序处理后插入到目标字节数组。
 *
 * @tparam T 浮点类型（float 或 double）
 * @param value 源浮点值
 * @param bytes 目标字节数组，浮点字节将插入到偏移位置
 * @param offset 偏移量，默认为 0（小于 0 从末尾偏移，大于等于 0 从开头偏移）
 * @param endian 字节序，默认为小端序
 */
template <typename T>
  requires std::floating_point<T>
void writeFloatingPoint(T value, std::vector<uint8_t> &bytes,
                        const int64_t offset = -1,
                        const std::endian endian = std::endian::little) {
  constexpr std::size_t size = sizeof(T);

  const auto n = static_cast<int64_t>(bytes.size());
  const int64_t index = offset >= 0 ? offset : n + offset;
  Assert::isFalse(index < 0 || index > n, "访问越界");

  std::vector<uint8_t> tmp(size);
  std::memcpy(tmp.data(), &value, size);
  if (endian == std::endian::big) {
    std::ranges::reverse(tmp);
  }

  bytes.insert(bytes.begin() + index, tmp.begin(), tmp.end());
}
// endregion

} // namespace alkaid::BytesUtil

#endif // ALKAID_BYTES_UTIL_H

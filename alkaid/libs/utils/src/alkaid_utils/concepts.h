#ifndef ALKAID_CONCEPTS_H
#define ALKAID_CONCEPTS_H

#include <concepts>

namespace alkaid::Concept {

/**
 * @brief 布尔概念：支持 bool 及拥有 explicit operator bool() 的类型
 *
 * 满足条件的类型包括：
 * - 原生 bool 类型
 * - 指针类型（可隐式转换为 bool）
 * - 拥有 explicit operator bool() 的类型（如 Result）
 */
template <typename T>
concept Boolean = requires(const T &val) {
  { !val } -> std::convertible_to<bool>;
};

/**
 * @brief 字符类型概念
 *
 * 满足条件的类型：char、wchar_t、char8_t、char16_t、char32_t
 */
template <typename T>
concept CharacterType = std::is_same_v<T, char> || std::is_same_v<T, wchar_t> ||
                        std::is_same_v<T, char8_t> || std::is_same_v<T, char16_t> ||
                        std::is_same_v<T, char32_t>;

/**
 * @brief 字符串概念：std::string / std::string_view 系列及字符指针类型
 *
 * 满足条件的类型：
 * - 拥有 npos 静态成员的类型（std::string、std::wstring、std::string_view 等）
 * - 指向字符类型的指针（const char*、const wchar_t* 等）
 */
template <typename T>
concept StringLike = requires {
  std::remove_cvref_t<T>::npos;
} || (std::is_pointer_v<std::remove_cvref_t<T>> && CharacterType<std::remove_cv_t<std::remove_pointer_t<std::remove_cvref_t<T>>>>);

/**
 * @brief 键值对容器概念：同时拥有 size()、key_type 和 mapped_type 的类型
 *
 * 满足条件的类型：std::map、std::unordered_map、std::multimap 等关联容器
 */
template <typename T>
concept MapLike = requires(const T &c) {
  { c.size() };
  typename std::remove_cvref_t<T>::key_type;
  typename std::remove_cvref_t<T>::mapped_type;
};

/**
 * @brief 容器概念：拥有 size() 方法的类型（排除字符串和键值对容器以避免重载二义性）
 *
 * 满足条件的类型：std::vector、std::array、std::deque、std::set 等
 */
template <typename T>
concept ContainerLike = requires(const T &c) {
  { c.size() };
} && !StringLike<T> && !MapLike<T>;

} // namespace alkaid::Concept

#endif // ALKAID_CONCEPTS_H

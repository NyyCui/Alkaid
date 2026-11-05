#ifndef ALKAID_EXCEPTIONS_H
#define ALKAID_EXCEPTIONS_H

#include <fmt/format.h>
#include <stdexcept>

namespace alkaid::Exceptions {

/**
 * @brief 创建带格式化信息的运行时异常
 *
 * 使用 StrUtil::format 格式化错误信息后构建 std::runtime_error 实例。
 * 占位符语法与 StrUtil::format 一致：
 * - {} 位置占位符
 * - {name} 命名占位符（配合 makeArg 使用）
 *
 * @param fmt 格式模板字符串
 * @param args 可变参数包，支持普通值和 NamedArg
 * @return 格式化后的 std::runtime_error 实例
 *
 * @code
 * throw Exceptions::of("文件 {} 不存在", "config.json");
 * throw Exceptions::of("连接 {host}:{port} 失败",
 *                      StrUtil::makeArg("host", "127.0.0.1"),
 *                      StrUtil::makeArg("port", 8080));
 * @endcode
 */
template<typename... Args>
std::runtime_error of(const std::string &tpl, const Args &... args) {
  return std::runtime_error(fmt::format(fmt::runtime(tpl), args...));
}
} // namespace common::Exceptions

#endif // ALKAID_EXCEPTIONS_H

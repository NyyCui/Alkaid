#ifndef ALKAID_SYS_FUNC_H
#define ALKAID_SYS_FUNC_H

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <tl/expected.hpp>
#include <variant>

namespace alkaid {

struct SysInvokeParam {
  const nlohmann::json param;
  template <typename T>
  T get() const {
    return param.get<T>();
  }

  static SysInvokeParam create(const std::string &param) {
    return SysInvokeParam(param);
  }

private:
  explicit SysInvokeParam(const std::string &param) : param(nlohmann::json::parse(param)) {}
};

struct SysInvokeResult {
  const bool success;
  const int32_t code;
  const std::optional<nlohmann::json> data;
  const std::string msg;

  static SysInvokeResult ok() {
    return {true, 0, std::nullopt, "成功"};
  }

  static SysInvokeResult ok(const nlohmann::json &data) {
    return {true, 0, std::optional(data), "成功"};
  }

  static SysInvokeResult ok(const nlohmann::json &data, const std::string &msg) {
    return {true, 0, std::optional(data), msg};
  }

  static SysInvokeResult fail() {
    return {false, 1, std::nullopt, "失败"};
  }

  static SysInvokeResult fail(const nlohmann::json &data) {
    return {false, 1, std::optional(data), "失败"};
  }

  static SysInvokeResult fail(const nlohmann::json &data, const std::string &msg) {
    return {false, 1, std::optional(data), msg};
  }

  static SysInvokeResult fail(const int32_t code, const nlohmann::json &data, const std::string &msg) {
    return {false, code, std::optional(data), msg};
  }

  friend void to_json(nlohmann::json &j, const SysInvokeResult &p) {
    j = nlohmann::json{
        {"success", p.success},
        {"code", p.code},
        {"data", p.data},
        {"msg", p.msg}};
  }

private:
  SysInvokeResult(const bool success, const int32_t code, const std::optional<nlohmann::json> &data, const std::string &msg)
      : success(success), code(code), data(data), msg(msg) {}
};

class SysFunc {
  template <class... Ts>
  struct overloaded : Ts... {
    using Ts::operator()...;
  };

  static inline std::vector<std::variant<
      std::function<SysInvokeResult(const SysInvokeParam &)>,
      std::function<SysInvokeResult()>>>
      SysFuncs;

  static inline std::vector<std::string> SysFuncNames;

public:
  static void registerSysInvoke(const std::string &sysFuncName, const std::function<SysInvokeResult(const SysInvokeParam &)> &func) {
    // LOGGER_DEBUG("注册系统调用：{}", sysFuncName);
    SysFuncNames.push_back(sysFuncName);
    SysFuncs.push_back(func);
  }

  static void registerSysInvoke(const std::string &sysFuncName, const std::function<SysInvokeResult()> &func) {
    // LOGGER_DEBUG("注册系统调用：{}", sysFuncName);
    SysFuncNames.push_back(sysFuncName);
    SysFuncs.push_back(func);
  }

  static tl::expected<SysInvokeResult, std::string> invoke(const int32_t funcode, const std::optional<std::string> &param) {
    if (funcode < 0 || funcode >= SysFuncs.size()) return tl::unexpected("系统调用不存在：" + std::to_string(funcode));
    return std::visit(overloaded{
                          [&](const std::function<SysInvokeResult(const SysInvokeParam &)> &f) {
                            if (!param) return tl::expected<SysInvokeResult, std::string>(tl::unexpected("参数不存在"));
                            try {
                              return tl::expected<SysInvokeResult, std::string>(f(SysInvokeParam::create(param.value())));
                            } catch (...) {
                              return tl::expected<SysInvokeResult, std::string>(tl::unexpected("系统调用执行失败"));
                            }
                          },
                          [&](const std::function<SysInvokeResult()> &f) {
                            if (param) return tl::expected<SysInvokeResult, std::string>(tl::unexpected("参数不存在"));
                            try {
                              return tl::expected<SysInvokeResult, std::string>(f());
                            } catch (...) {
                              return tl::expected<SysInvokeResult, std::string>(tl::unexpected("系统调用执行失败"));
                            }
                          }},
                      SysFuncs.at(funcode));
  }

  static const std::vector<std::string> &getSysFuncNames() {
    return SysFuncNames;
  }
};

} // namespace alkaid

#endif // ALKAID_SYS_FUNC_H

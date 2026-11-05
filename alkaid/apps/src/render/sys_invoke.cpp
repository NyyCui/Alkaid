#include "sys_invoke.h"

#include "alkaid_log/logging.h"

namespace alkaid {

std::atomic<int32_t> SysInvoke::rid_ = 0;

// promises_ 改为惰性初始化：静态成员在 RendererApp 构造前就会初始化，
// 此时系统函数尚未注册，size() 为 0，会导致后续下标访问越界崩溃
std::vector<std::vector<SysInvoke::PromiseWrapper>> &SysInvoke::getPromises() {
  static std::vector<std::vector<PromiseWrapper>> promises{SysFunc::getSysFuncNames().size()};
  return promises;
}

void SysInvoke::removeByContext(CefRefPtr<CefV8Context> context) {
  auto &promises = getPromises();
  for (auto &funcPromises: promises) {
    // context 释放后不可再进入，仅做指针比较，安全
    std::erase_if(funcPromises, [&context](const PromiseWrapper &wrapper) {
      return wrapper.context.get() == context.get();
    });
  }
}

bool SysInvoke::Execute(
  const CefString &name, CefRefPtr<CefV8Value> object, const CefV8ValueList &arguments, CefRefPtr<CefV8Value> &retval,
  CefString &exception
) {

  LOGGER_DEBUG("调用函数：{}", name.ToString());

  if (name != "sysInvoke") return false;

  const size_t argsCount = arguments.size();

  if ((argsCount != 1 && argsCount != 2) || !arguments[0]->IsInt()) {
    LOGGER_DEBUG("调用失败");
    exception = "调用失败，正确的调用方式: sysInvoke(code: number, json?: string)";
    return true;
  }
  const int32_t funcode = arguments[0]->GetIntValue();
  if (funcode < 0 || funcode >= SysFunc::getSysFuncNames().size()) {
    LOGGER_DEBUG("调用失败，函数不存在");
    exception = "调用失败，函数不存在";
    return true;
  }

  const int32_t rid = rid_.fetch_add(1);
  CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();

  CefRefPtr<CefV8Value> promise = CefV8Value::CreatePromise();
  retval = promise;
  getPromises()[funcode].emplace_back(rid, context, promise);
  LOGGER_DEBUG("调用：{}、{}", rid, funcode);

  auto msg = CefProcessMessage::Create("sysInvoke");
  msg->GetArgumentList()->SetInt(0, rid);
  msg->GetArgumentList()->SetInt(1, funcode);
  if (argsCount == 2) {
    auto value = arguments[1]->GetStringValue().ToString();
    LOGGER_DEBUG("参数：{}", value);

    msg->GetArgumentList()->SetString(2, value);
  }

  CefRefPtr<CefFrame> frame = context->GetFrame();
  frame->SendProcessMessage(PID_BROWSER, msg);

  return true;
}

void SysInvoke::fulfill(CefRefPtr<CefListValue> result) {
  const int32_t rid = result->GetInt(0);
  const int32_t funcode = result->GetInt(1);
  // funcode 来自浏览器进程消息，必须校验边界，防止越界访问
  if (funcode < 0 || funcode >= static_cast<int32_t>(getPromises().size())) return;
  std::vector<PromiseWrapper> &promises = getPromises()[funcode];
  if (promises.empty()) return;
  const auto promiseWrapperIt = std::ranges::find_if(promises, [rid](const PromiseWrapper &pw) {
    return pw.rid == rid;
  });
  if (promiseWrapperIt == promises.end()) return;
  const bool reserved = result->GetBool(2);
  const size_t size = result->GetSize();
  // 拷贝一份 wrapper，保证下面 erase 后仍然有效
  const PromiseWrapper promiseWrapper = *promiseWrapperIt;
  LOGGER_DEBUG("reserved：{}", reserved);
  LOGGER_DEBUG("size：{}", size);
  if (!promiseWrapper.context || !promiseWrapper.context->IsValid()) {
    LOGGER_WARN("上下文已销毁");
    return;
  }

  promiseWrapper.context->Enter();
  if (reserved) {
    promiseWrapper.promise->ResolvePromise(size == 4 ? CefV8Value::CreateString(result->GetString(3)) : CefV8Value::CreateUndefined());
  } else {
    promiseWrapper.promise->RejectPromise(size == 4 ? result->GetString(3) : CefString(""));
  }
  promiseWrapper.context->Exit();

  // 立即从原容器中移除，避免在 Enter/Exit 期间被重入干扰
  std::erase_if(promises, [rid](const PromiseWrapper &wrapper) {
    return wrapper.rid == rid;
  });
}

} // namespace alkaid

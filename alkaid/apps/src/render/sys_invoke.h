#ifndef ALKAID_SYS_INVOKE_H
#define ALKAID_SYS_INVOKE_H
#include "include/cef_v8.h"
#include "sys_func/sys_func.h"

namespace alkaid {

class SysInvoke: public CefV8Handler {
  IMPLEMENT_REFCOUNTING(SysInvoke);

  SysInvoke(const SysInvoke &) = delete;

  SysInvoke &operator=(const SysInvoke &) = delete;

  struct PromiseWrapper {
    int32_t rid;
    CefRefPtr<CefV8Context> context;
    CefRefPtr<CefV8Value> promise;

    PromiseWrapper(const int32_t rid, CefRefPtr<CefV8Context> context, CefRefPtr<CefV8Value> promise)
      : rid(rid)
      , context(context)
      , promise(promise) {}
  };

  static std::atomic<int32_t> rid_;

  static std::vector<std::vector<PromiseWrapper>> &getPromises();

public:
  /// 清除指定 context 下的所有 Promise 记录，用于 context 释放（页面导航、浏览器关闭）时防止悬挂引用
  static void removeByContext(CefRefPtr<CefV8Context> context);

  SysInvoke() = default;

  bool Execute(const CefString &name,
    CefRefPtr<CefV8Value> object,
    const CefV8ValueList &arguments,
    CefRefPtr<CefV8Value> &retval,
    CefString &exception
  ) override;

  static void fulfill(CefRefPtr<CefListValue> result);
};

} // namespace alkaid

#endif // ALKAID_SYS_INVOKE_H

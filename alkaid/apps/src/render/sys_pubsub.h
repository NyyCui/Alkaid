#ifndef ALKAID_SYS_PUBSUB_H
#define ALKAID_SYS_PUBSUB_H

#include "include/cef_v8.h"

namespace alkaid {

class SysPubsub: public CefV8Handler {
  IMPLEMENT_REFCOUNTING(SysPubsub);

  SysPubsub(const SysPubsub &) = delete;

  SysPubsub &operator=(const SysPubsub &) = delete;

  struct ListenerWrapper {
    int32_t sid;
    CefRefPtr<CefV8Context> context;
    CefRefPtr<CefV8Value> listener;

    ListenerWrapper(const uint64_t sid, CefRefPtr<CefV8Context> context, CefRefPtr<CefV8Value> listener)
      : sid(sid)
      , context(context)
      , listener(listener) {}
  };

  static std::atomic<int32_t> sid_;

  static std::vector<std::vector<ListenerWrapper>> &getListeners();

public:
  /// 清除指定 context 下的所有订阅，用于 context 释放（页面导航、浏览器关闭）时防止悬挂引用
  static void removeByContext(CefRefPtr<CefV8Context> context);

  SysPubsub() = default;

  bool Execute(const CefString &name,
    CefRefPtr<CefV8Value> object,
    const CefV8ValueList &arguments,
    CefRefPtr<CefV8Value> &retval,
    CefString &exception
  ) override;

  static void publish(CefRefPtr<CefListValue> message);
};
} // namespace alkaid

#endif // ALKAID_SYS_PUBSUB_H

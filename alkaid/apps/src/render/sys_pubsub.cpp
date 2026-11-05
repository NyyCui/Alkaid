#include "sys_pubsub.h"

#include "alkaid_log/logging.h"
#include "sys_event/sys_event.h"

namespace alkaid {
std::atomic<int32_t> SysPubsub::sid_ = 0;

std::vector<std::vector<SysPubsub::ListenerWrapper>> &SysPubsub::getListeners() {
  static std::vector<std::vector<ListenerWrapper>> listeners{SysEvent::getSysEventNames().size()};
  return listeners;
}

void SysPubsub::removeByContext(CefRefPtr<CefV8Context> context) {
  auto &listeners = getListeners();
  for (auto &eventListeners: listeners) {
    // context 释放后不可再进入，仅做指针比较，安全
    std::erase_if(eventListeners, [&context](const ListenerWrapper &wrapper) {
      return wrapper.context.get() == context.get();
    });
  }
}

bool SysPubsub::Execute(const CefString &name,
  CefRefPtr<CefV8Value> object, const CefV8ValueList &arguments,
  CefRefPtr<CefV8Value> &retval,
  CefString &exception
) {
  if (name == "sysSub") {
    const size_t argsCount = arguments.size();
    if (argsCount != 2 || !arguments[0]->IsInt() || !arguments[1]->IsFunction()) {
      exception = "调用失败，正确的调用方式: sysSub(event: number, listener: (e: SysEvent) => void)";
    } else {
      const int32_t evtcode = arguments[0]->GetIntValue();
      if (evtcode < 0 || evtcode >= SysEvent::getSysEventNames().size()) {
        exception = "调用失败，事件不存在";
        return true;
      }
      const int32_t sid = sid_.fetch_add(1);
      CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();

      getListeners()[evtcode].emplace_back(sid, context, arguments[1]);

      retval = CefV8Value::CreateInt(sid);
    }
    return true;
  }
  if (name == "sysUnsub") {
    const size_t argsCount = arguments.size();
    if (argsCount != 2 || !arguments[0]->IsInt() || !arguments[1]->IsInt()) {
      exception = "调用失败，正确的调用方式: sysUnsub(event: number, id: number)";
    } else {
      const int32_t evtcode = arguments[0]->GetIntValue();
      if (evtcode < 0 || evtcode >= SysEvent::getSysEventNames().size()) {
        exception = "调用失败，事件不存在";
        return true;
      }

      auto &listeners = getListeners();

      std::erase_if(listeners[evtcode], [sid = arguments[1]->GetIntValue()](const ListenerWrapper &wrapper) {
        return wrapper.sid == sid;
      });
      retval = CefV8Value::CreateBool(true);
    }
    return true;
  }
  return false;
}

void SysPubsub::publish(CefRefPtr<CefListValue> message) {
  const int32_t event = message->GetInt(0);
  const auto &listeners = getListeners()[event];
  if (listeners.empty()) {
    LOGGER_DEBUG("事件无监听器：{}", event);
    return;
  }
  const size_t size = message->GetSize();
  for (const auto &listener: listeners) {
    if (!listener.context || !listener.context->IsValid()) {
      LOGGER_WARN("上下文已销毁");
      continue;
    }
    listener.context->Enter();
    listener.listener->ExecuteFunction(nullptr, size == 2 ? CefV8ValueList{CefV8Value::CreateString(message->GetString(1))} : CefV8ValueList{});
    listener.context->Exit();
  }
}

} // namespace alkaid

#include "renderer_app.h"

#include "alkaid_log/logging.h"
#include "sys_invoke.h"
#include "alkaid_utils/module_registry.h"
#include "app_manage/app_manage.h"
#include "window/window_manager.h"
#include "sys_pubsub.h"
#include "sys_event/sys_event.h"

namespace alkaid {

RendererApp::RendererApp() {
  Log::registerLoggingModule();
  AppManage::registerAppManageModule();
  WindowManager::registerWindowManagerModule();

  auto init_all = ModuleRegistry::initAll("Renderer");
}
CefRefPtr<CefRenderProcessHandler> RendererApp::GetRenderProcessHandler() {
  return this;
}

void RendererApp::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
  registrar->AddCustomScheme(RESOURCES_SCHEME, CEF_SCHEME_OPTION_STANDARD);
}

void RendererApp::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefV8Context> context) {
  CefRefPtr sysInvoke = new SysInvoke();
  CefRefPtr sysPubsub = new SysPubsub();
  auto &sysFuncNames = SysFunc::getSysFuncNames();
  auto &sysEventNames = SysEvent::getSysEventNames();
  size_t sysFuncCount = sysFuncNames.size();
  size_t sysEventCount = sysEventNames.size();
  LOGGER_DEBUG("注入系统调用：{} 个", sysFuncCount);
  LOGGER_DEBUG("注入系统事件：{} 个", sysEventCount);
  context->Enter();
  CefRefPtr<CefV8Value> global = context->GetGlobal();

  global->SetValue("sysInvoke",
                   CefV8Value::CreateFunction("sysInvoke", sysInvoke),
                   static_cast<cef_v8_propertyattribute_t>(
                       V8_PROPERTY_ATTRIBUTE_READONLY | V8_PROPERTY_ATTRIBUTE_DONTDELETE));
  global->SetValue("sysSub",
    CefV8Value::CreateFunction("sysSub", sysPubsub),
                   static_cast<cef_v8_propertyattribute_t>(
                       V8_PROPERTY_ATTRIBUTE_READONLY | V8_PROPERTY_ATTRIBUTE_DONTDELETE));
  global->SetValue("sysUnsub",
                   CefV8Value::CreateFunction("sysUnsub", sysPubsub),
                   static_cast<cef_v8_propertyattribute_t>(
                     V8_PROPERTY_ATTRIBUTE_READONLY | V8_PROPERTY_ATTRIBUTE_DONTDELETE));

  const CefRefPtr<CefV8Value> sysFuncNameIdxMap = CefV8Value::CreateObject(nullptr, nullptr);
  const CefRefPtr<CefV8Value> sysEventNameIdxMap = CefV8Value::CreateObject(nullptr, nullptr);

  for (size_t i = 0; i < sysFuncCount; ++i) {
    sysFuncNameIdxMap->SetValue(
      sysFuncNames[i],
      CefV8Value::CreateInt(static_cast<int32_t>(i)),
        static_cast<cef_v8_propertyattribute_t>(
            V8_PROPERTY_ATTRIBUTE_READONLY | V8_PROPERTY_ATTRIBUTE_DONTDELETE));
  }

  for (size_t i = 0; i < sysEventCount; ++i) {
    sysEventNameIdxMap->SetValue(
      sysEventNames[i],
      CefV8Value::CreateInt(static_cast<int32_t>(i)),
      static_cast<cef_v8_propertyattribute_t>(
        V8_PROPERTY_ATTRIBUTE_READONLY | V8_PROPERTY_ATTRIBUTE_DONTDELETE));
  }

  global->SetValue("sysFunc", sysFuncNameIdxMap, static_cast<cef_v8_propertyattribute_t>(V8_PROPERTY_ATTRIBUTE_READONLY | V8_PROPERTY_ATTRIBUTE_DONTDELETE));
  global->SetValue("sysEvent", sysEventNameIdxMap, static_cast<cef_v8_propertyattribute_t>(V8_PROPERTY_ATTRIBUTE_READONLY | V8_PROPERTY_ATTRIBUTE_DONTDELETE));

  context->Exit();
}

void RendererApp::OnContextReleased(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefFrame> frame,
                                    CefRefPtr<CefV8Context> context) {
  // context 释放（页面导航或浏览器关闭）时清除该 context 的所有订阅，防止悬挂引用
  SysPubsub::removeByContext(context);
  // 同时清除未完成的 Promise 记录，避免浏览器进程结果返回时操作已释放的 context
  SysInvoke::removeByContext(context);
}

bool RendererApp::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                           CefRefPtr<CefFrame> frame,
                                           CefProcessId source_process,
                                           CefRefPtr<CefProcessMessage> message) {
  if (message->GetName() == "sysInvokeResult") {
    SysInvoke::fulfill(message->GetArgumentList());
    return true;
  }
  if (message->GetName() == "sysPublish") {
    SysPubsub::publish(message->GetArgumentList());
    return true;
  }
  return false;
}

} // namespace alkaid

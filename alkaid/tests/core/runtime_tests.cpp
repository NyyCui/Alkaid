/** @file runtime_tests.cpp 验证宿主身份、权限隔离与异步请求生命周期。 */
#include "rpc/call_gateway.h"
#include <cstdlib>
#include <iostream>

namespace {
int checks = 0;

void check(bool condition, const char *message) {
  ++checks;
  if (!condition) {
    std::cerr << "验证失败：" << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

template <typename T>
bool errorIs(const alkaid::RuntimeResult<T> &result, const std::string &code) {
  const auto error = std::get_if<alkaid::RuntimeError>(&result);
  return error && error->code == code;
}

bool errorIs(const std::optional<alkaid::RuntimeError> &error, const std::string &code) {
  return error && error->code == code;
}

struct Fixture {
  alkaid::SessionRegistry sessions;
  alkaid::CallGateway gateway{sessions};
  alkaid::DocumentIdentity doc{1, "frame-main", true, "alkaid://notes-origin"};
  uint64_t session = 0;
  alkaid::RuntimeClock::time_point now{};

  explicit Fixture(bool manager = false) {
    check(!sessions.registerApp({"com.example.notes", "notes-origin", {"apps.manage", "platform.exit"}},
      {{"apps.manage", "platform.exit"}, {"apps.manage", "platform.exit"}, manager}), "注册应用");
    check(!sessions.bindBrowser(1, "com.example.notes"), "绑定浏览器");
    session = std::get<uint64_t>(sessions.openDocument(doc));
    for (const auto &method : alkaid::METHOD_CATALOG) {
      check(!gateway.enableMethod(std::string(method.name)), "启用测试处理器契约");
    }
  }

  alkaid::CallRequest request(std::string id = "request-1", std::string method = "runtime.getInfo") const {
    return {1, std::move(id), std::move(method), 100, 1000};
  }
};

void identityTests() {
  Fixture f;
  check(errorIs(f.sessions.registerApp({"other.app", "notes-origin", {}}, {}), "ORIGIN_EXISTS"), "来源不能复用");
  check(errorIs(f.sessions.registerApp({"../bad", "other-origin", {}}, {}), "INVALID_APP"), "拒绝路径形式应用标识");
  auto forged = f.doc;
  forged.browserId = 2;
  check(errorIs(f.gateway.begin(f.session, forged, f.request(), f.now), "SESSION_EXPIRED"), "伪造其他浏览器无效");
  forged = f.doc;
  forged.frameId = "other-frame";
  check(errorIs(f.gateway.begin(f.session, forged, f.request(), f.now), "SESSION_EXPIRED"), "伪造框架无效");
  forged = f.doc;
  forged.mainFrame = false;
  check(errorIs(f.sessions.openDocument(forged), "UNTRUSTED_DOCUMENT"), "子框架不能打开会话");
  check(f.sessions.find(f.session).has_value(), "子框架不撤销主文档");
  forged = f.doc;
  forged.origin = "https://example.com";
  check(errorIs(f.sessions.openDocument(forged), "UNTRUSTED_DOCUMENT"), "外部导航不得获得能力");
  check(!f.sessions.find(f.session), "外部导航撤销原会话");
}

void permissionTests() {
  Fixture ordinary;
  check(errorIs(ordinary.gateway.begin(ordinary.session, ordinary.doc, ordinary.request("x", "apps.list")),
    "PERMISSION_DENIED"), "普通应用不能凭声明获得管理权限");
  check(ordinary.gateway.capabilities(ordinary.session).size() == 18, "普通应用获得事件与插件调用入口及已授权方法");

  Fixture desktop(true);
  check(desktop.gateway.capabilities(desktop.session).size() == 51, "受信桌面获得插件、应用授权及数据管理能力");
  check(errorIs(ordinary.gateway.begin(ordinary.session, ordinary.doc, ordinary.request("plugin-list", "plugins.list")),
    "PERMISSION_DENIED"), "普通应用不能管理插件");
  check(errorIs(ordinary.gateway.begin(ordinary.session, ordinary.doc, ordinary.request("platform-config", "config.platform.set")),
    "PERMISSION_DENIED"), "普通应用不能写入平台配置");
  for (const auto &method : {"apps.launch", "apps.focus", "apps.close", "apps.prepareInstall", "apps.confirmInstall", "apps.cancelInstall", "apps.listTasks", "apps.getTask", "apps.uninstall", "apps.rollback", "apps.advancePack", "apps.cancelPack", "apps.revealPack", "apps.installPacked", "window.minimize", "window.toggleMaximize"}) {
    check(errorIs(ordinary.gateway.begin(ordinary.session, ordinary.doc, ordinary.request("app-action", method)),
      "PERMISSION_DENIED"), "普通应用不能调用管理接口");
  }
  const auto permit = std::get<alkaid::CallPermit>(desktop.gateway.begin(desktop.session, desktop.doc,
    desktop.request("x", "apps.list"), desktop.now));
  check(!desktop.sessions.setAuthorization("com.example.notes", {{"apps.manage"}, {}, true}), "撤销用户授权");
  check(errorIs(desktop.gateway.checkReady(permit, desktop.now), "PERMISSION_DENIED"), "排队任务执行前重查授权");
  check(errorIs(desktop.gateway.finish(permit, desktop.now), "PERMISSION_DENIED"), "在途调用不能返回已撤销能力结果");
  check(!desktop.sessions.setAuthorization("com.example.notes", {{}, {"apps.manage"}, true}), "撤销平台策略");
  check(errorIs(desktop.gateway.begin(desktop.session, desktop.doc, desktop.request("y", "apps.list")),
    "PERMISSION_DENIED"), "用户授权不能越过平台策略");

  alkaid::SessionRegistry undeclared;
  check(!undeclared.registerApp({"com.example.plain", "plain-origin", {}}, {{"apps.manage"}, {"apps.manage"}, true}), "注册无声明应用");
  check(!undeclared.bindBrowser(4, "com.example.plain"), "绑定无声明应用");
  const auto session = std::get<uint64_t>(undeclared.openDocument({4, "main", true, "alkaid://plain-origin"}));
  check(!undeclared.allows(session, "apps.manage", true), "平台和用户不能替代包声明");
}

void protocolTests() {
  Fixture f;
  auto request = f.request();
  request.protocolVersion = 2;
  check(errorIs(f.gateway.begin(f.session, f.doc, request), "PROTOCOL_MISMATCH"), "协议不兼容");
  request = f.request();
  request.payloadBytes = alkaid::MAX_PAYLOAD_BYTES + 1;
  check(errorIs(f.gateway.begin(f.session, f.doc, request), "PAYLOAD_TOO_LARGE"), "报文限额");
  request = f.request();
  request.timeoutMs = 0;
  check(errorIs(f.gateway.begin(f.session, f.doc, request), "INVALID_REQUEST"), "不接受无期限请求");
  request = f.request("");
  check(errorIs(f.gateway.begin(f.session, f.doc, request), "INVALID_REQUEST"), "空标识拒绝");
  check(errorIs(f.gateway.begin(f.session, f.doc, f.request("a", "arbitrary.execute")), "METHOD_NOT_FOUND"), "任意方法不可调用");
  alkaid::CallGateway emptyGateway(f.sessions);
  check(errorIs(emptyGateway.begin(f.session, f.doc, f.request()), "METHOD_NOT_FOUND"), "契约不代表方法实现");
  check(emptyGateway.capabilities(f.session).empty(), "未实现的方法不列入能力");
  check(errorIs(f.gateway.enableMethod("runtime.getInfo"), "METHOD_EXISTS"), "重复注册不覆盖");
}

void lifecycleTests() {
  Fixture f;
  const auto old = std::get<alkaid::CallPermit>(f.gateway.begin(f.session, f.doc, f.request(), f.now));
  check(errorIs(f.gateway.begin(f.session, f.doc, f.request(), f.now), "DUPLICATE_REQUEST"), "拒绝在途重复请求");
  const auto newSession = std::get<uint64_t>(f.sessions.openDocument(f.doc));
  check(newSession != f.session, "刷新产生新的会话代次");
  check(errorIs(f.gateway.finish(old, f.now), "SESSION_EXPIRED"), "旧结果不投递给新页面");
  check(f.gateway.pendingCount() == 0, "迟到调用释放额度");
  const auto current = std::get<alkaid::CallPermit>(f.gateway.begin(newSession, f.doc, f.request(), f.now));
  f.sessions.unregisterApp("com.example.notes");
  check(errorIs(f.gateway.finish(current, f.now), "SESSION_EXPIRED"), "卸载撤销会话");
  check(errorIs(f.sessions.openDocument(f.doc), "UNTRUSTED_DOCUMENT"), "卸载撤销浏览器绑定");
}

void timeoutTests() {
  Fixture f;
  const auto old = std::get<alkaid::CallPermit>(f.gateway.begin(f.session, f.doc, f.request(), f.now));
  const auto later = f.now + std::chrono::milliseconds(1000);
  const auto expired = f.gateway.expire(later);
  check(expired.size() == 1 && expired[0].ticket == old.ticket, "到达截止时间回收");
  const auto current = std::get<alkaid::CallPermit>(f.gateway.begin(f.session, f.doc, f.request(), later));
  check(errorIs(f.gateway.finish(old, later), "REQUEST_EXPIRED"), "旧任务不能完成复用标识的新请求");
  check(f.gateway.pendingCount() == 1, "迟到任务不移除新请求");
  check(!f.gateway.finish(current, later), "新请求正常完成");
  check(errorIs(f.gateway.finish(current, later), "REQUEST_EXPIRED"), "完成幂等防重复");
  const auto deadline = std::get<alkaid::CallPermit>(f.gateway.begin(f.session, f.doc, f.request(), f.now));
  check(errorIs(f.gateway.finish(deadline, later), "TIMEOUT"), "未调用定时器也检查超时");
}

void quotaTests() {
  Fixture f;
  for (std::size_t index = 0; index < alkaid::MAX_PENDING_PER_SESSION; ++index) {
    check(std::holds_alternative<alkaid::CallPermit>(f.gateway.begin(f.session, f.doc, f.request(std::to_string(index)), f.now)), "限额内准入");
  }
  check(errorIs(f.gateway.begin(f.session, f.doc, f.request("overflow"), f.now), "RESOURCE_EXHAUSTED"), "会话并发有界");
  check(!f.sessions.bindBrowser(2, "com.example.notes"), "创建第二窗口");
  const alkaid::DocumentIdentity second{2, "main-2", true, "alkaid://notes-origin"};
  const auto other = std::get<uint64_t>(f.sessions.openDocument(second));
  check(std::holds_alternative<alkaid::CallPermit>(f.gateway.begin(other, second, f.request(), f.now)), "其他会话不受单会话配额影响");
  f.sessions.invalidateDocument(1);
  f.gateway.discardSession(f.session);
  check(f.gateway.pendingCount() == 1, "销毁只清理自己的请求");
  f.sessions.unbindBrowser(2);
  check(f.gateway.expire(f.now).size() == 1, "定时器回收已销毁会话");
  check(f.gateway.pendingCount() == 0, "没有遗留请求");
}
} // namespace

int main() {
  identityTests();
  permissionTests();
  protocolTests();
  lifecycleTests();
  timeoutTests();
  quotaTests();
  std::cout << "运行核心验证通过，断言数量：" << checks << '\n';
  return EXIT_SUCCESS;
}

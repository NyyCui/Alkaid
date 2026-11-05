/** @file call_gateway.h 系统调用准入与完成检查，不持有 CEF 对象或执行系统操作。 */
#ifndef ALKAID_CALL_GATEWAY_H
#define ALKAID_CALL_GATEWAY_H
#include "method_catalog.h"
#include "runtime/session_registry.h"
#include <map>
#include <set>
#include <vector>

namespace alkaid {

struct CallRequest {
  int protocolVersion = PROTOCOL_VERSION;
  std::string requestId;
  std::string method;
  std::size_t payloadBytes = 0;
  int timeoutMs = 15000;
};

/** 与 SessionRegistry 一样由宿主调度线程独占，工作结果必须投递回该线程。 */
class CallGateway {
public:
  explicit CallGateway(SessionRegistry &sessions);
  /** 只在实际服务处理器接入后启用方法，已知方法不等于当前宿主已实现。 */
  std::optional<RuntimeError> enableMethod(const std::string &method);
  /** 适配层校验报文结构及参数后，使用真实文档身份申请执行许可。 */
  RuntimeResult<CallPermit> begin(uint64_t sessionId, const DocumentIdentity &document,
                                const CallRequest &request, RuntimeClock::time_point now = RuntimeClock::now());
  /** 排队任务开始执行前再次检查；完成检查不能撤销已经发生的系统副作用。 */
  std::optional<RuntimeError> checkReady(const CallPermit &permit, RuntimeClock::time_point now = RuntimeClock::now()) const;
  /** 执行完成时重新检查会话、权限和截止时间；返回错误时不得投递成功结果。 */
  std::optional<RuntimeError> finish(const CallPermit &permit, RuntimeClock::time_point now = RuntimeClock::now());
  /** 导航、关闭和卸载后调用，释放旧会话请求额度。 */
  void discardSession(uint64_t sessionId);
  /** 宿主定时器调用；过期请求立即释放额度，返回结果供适配层发送超时响应。 */
  std::vector<CallPermit> expire(RuntimeClock::time_point now = RuntimeClock::now());
  /** 返回已实现且当前会话被授权的方法列表。 */
  std::vector<std::string> capabilities(uint64_t sessionId) const;
  std::size_t pendingCount() const;

private:
  struct PendingCall {
    CallPermit permit;
    std::string method;
    RuntimeClock::time_point deadline;
  };
  SessionRegistry &sessions_;
  std::set<std::string> enabled_;
  std::map<std::pair<uint64_t, std::string>, PendingCall> pending_;
  uint64_t nextTicket_ = 1;
};

} // namespace alkaid
#endif // ALKAID_CALL_GATEWAY_H

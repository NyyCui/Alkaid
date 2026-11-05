/** @file call_gateway.cpp 默认拒绝、限额和迟到结果处理。 */
#include "call_gateway.h"
#include <algorithm>
#include <limits>

namespace alkaid {
namespace _ {
const MethodContract *findMethod(const std::string &name) {
  const auto found = std::ranges::find_if(METHOD_CATALOG, [&](const auto &entry) { return entry.name == name; });
  return found == METHOD_CATALOG.end() ? nullptr : &*found;
}
bool validRequestId(const std::string &id) {
  return !id.empty() && id.size() <= 128 && std::ranges::all_of(id, [](unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
      (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
  });
}
} // namespace _

CallGateway::CallGateway(SessionRegistry &sessions) : sessions_(sessions) {}

std::optional<RuntimeError> CallGateway::enableMethod(const std::string &method) {
  if (!_::findMethod(method)) return RuntimeError{"METHOD_NOT_FOUND", "方法未在契约中声明"};
  if (!enabled_.insert(method).second) return RuntimeError{"METHOD_EXISTS", "方法已经启用"};
  return std::nullopt;
}

RuntimeResult<CallPermit> CallGateway::begin(uint64_t sessionId, const DocumentIdentity &document,
                                           const CallRequest &request, RuntimeClock::time_point now) {
  // 不自动清理超时记录：宿主需通过 expire 取得超时列表，避免悄悄丢掉响应。
  if (!sessions_.resolve(sessionId, document)) return RuntimeError{"SESSION_EXPIRED", "文档会话已失效"};
  if (request.protocolVersion != PROTOCOL_VERSION) return RuntimeError{"PROTOCOL_MISMATCH", "系统调用协议版本不兼容"};
  if (!_::validRequestId(request.requestId)) return RuntimeError{"INVALID_REQUEST", "请求标识不合法"};
  if (request.payloadBytes > MAX_PAYLOAD_BYTES) return RuntimeError{"PAYLOAD_TOO_LARGE", "请求报文超过大小限制"};
  const auto method = _::findMethod(request.method);
  if (!method || !enabled_.contains(request.method)) return RuntimeError{"METHOD_NOT_FOUND", "系统能力尚未实现或未启用"};
  if (request.timeoutMs <= 0 || request.timeoutMs > method->timeoutMs) return RuntimeError{"INVALID_REQUEST", "请求超时设置不合法"};
  if (!sessions_.allows(sessionId, std::string(method->capability), method->managerOnly)) {
    return RuntimeError{"PERMISSION_DENIED", "应用没有调用此系统能力的权限"};
  }
  const auto key = std::make_pair(sessionId, request.requestId);
  if (pending_.contains(key)) return RuntimeError{"DUPLICATE_REQUEST", "请求标识正在使用"};
  const auto count = std::ranges::count_if(pending_, [sessionId](const auto &entry) {
    return entry.second.permit.sessionId == sessionId;
  });
  if (static_cast<std::size_t>(count) >= MAX_PENDING_PER_SESSION) return RuntimeError{"RESOURCE_EXHAUSTED", "当前会话未完成请求过多"};
  if (nextTicket_ == std::numeric_limits<uint64_t>::max()) return RuntimeError{"RESOURCE_EXHAUSTED", "调用标识已耗尽"};
  const CallPermit permit{sessionId, request.requestId, nextTicket_++};
  pending_.emplace(key, PendingCall{permit, request.method, now + std::chrono::milliseconds(request.timeoutMs)});
  return permit;
}

std::optional<RuntimeError> CallGateway::checkReady(const CallPermit &permit, RuntimeClock::time_point now) const {
  const auto pending = pending_.find({permit.sessionId, permit.requestId});
  if (pending == pending_.end() || pending->second.permit.ticket != permit.ticket) {
    return RuntimeError{"REQUEST_EXPIRED", "请求已经结束或失效"};
  }
  const auto &call = pending->second;
  if (!sessions_.find(permit.sessionId)) return RuntimeError{"SESSION_EXPIRED", "文档会话已失效"};
  if (now >= call.deadline) return RuntimeError{"TIMEOUT", "系统调用超时"};
  const auto method = _::findMethod(call.method);
  if (!sessions_.allows(permit.sessionId, std::string(method->capability), method->managerOnly)) {
    return RuntimeError{"PERMISSION_DENIED", "调用权限已经撤销"};
  }
  return std::nullopt;
}

std::optional<RuntimeError> CallGateway::finish(const CallPermit &permit, RuntimeClock::time_point now) {
  const auto error = checkReady(permit, now);
  const auto pending = pending_.find({permit.sessionId, permit.requestId});
  if (pending != pending_.end() && pending->second.permit.ticket == permit.ticket) pending_.erase(pending);
  return error;
}

void CallGateway::discardSession(uint64_t sessionId) {
  std::erase_if(pending_, [sessionId](const auto &entry) { return entry.second.permit.sessionId == sessionId; });
}

std::vector<CallPermit> CallGateway::expire(RuntimeClock::time_point now) {
  std::vector<CallPermit> expired;
  std::erase_if(pending_, [&](const auto &entry) {
    if (entry.second.deadline <= now || !sessions_.find(entry.second.permit.sessionId)) {
      expired.push_back(entry.second.permit);
      return true;
    }
    return false;
  });
  return expired;
}

std::vector<std::string> CallGateway::capabilities(uint64_t sessionId) const {
  std::vector<std::string> result;
  for (const auto &method : METHOD_CATALOG) {
    if (enabled_.contains(std::string(method.name)) &&
        sessions_.allows(sessionId, std::string(method.capability), method.managerOnly)) result.emplace_back(method.name);
  }
  return result;
}

std::size_t CallGateway::pendingCount() const { return pending_.size(); }
} // namespace alkaid

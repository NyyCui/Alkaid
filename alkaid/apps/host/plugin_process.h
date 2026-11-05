/** @file plugin_process.h 单文档插件进程和有界管道协议，仅在插件工作线程使用。 */
#ifndef ALKAID_PLUGIN_PROCESS_H
#define ALKAID_PLUGIN_PROCESS_H
#include "data_store.h"
#include "include/cef_task.h"
#include <functional>

namespace alkaid {
class PluginProcess : public std::enable_shared_from_this<PluginProcess> {
public:
  PluginProcess(std::filesystem::path dataRoot, std::string appId)
    : dataRoot_(std::move(dataRoot)), appId_(std::move(appId)) {}
  ~PluginProcess();
  PluginProcess(const PluginProcess &) = delete;
  PluginProcess &operator=(const PluginProcess &) = delete;
  static std::optional<RuntimeError> probe(const std::filesystem::path &dataRoot, const VerifiedPackage &package);
  RuntimeResult<Json> call(const std::string &id, const PluginBinding &binding, const std::string &method,
    const Json &params, const CallPermit &permit, const DataLease &lease,
    const std::function<void(std::string, Json)> &event);
  void release(uint64_t session);
  void startPolling(CefRefPtr<CefTaskRunner> runner);
private:
  void pollIdle();
  struct Instance;
  std::filesystem::path dataRoot_;
  std::string appId_;
  std::map<std::pair<uint64_t, std::string>, std::shared_ptr<Instance>> instances_;
  bool pollScheduled_ = false;
};
}
#endif

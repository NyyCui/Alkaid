#ifndef ALKAID_MODULE_REGISTRY_H
#define ALKAID_MODULE_REGISTRY_H

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <tl/expected.hpp>

namespace alkaid {

/**
 * @brief 模块初始化注册表，统一管理各模块生命周期
 *
 * 设计要点：
 * 1.
 * 注册与执行分离：注册阶段仅登记元信息，不执行有副作用的操作，
 *    从根本上规避静态初始化顺序问题（SIOF）；
 * 2.
 * 依赖自动排序：initAll 时拓扑排序，保证依赖先于被依赖者初始化；
 * 3. 多进程过滤：模块声明适用进程类型，各 CEF
 * 进程只初始化自己需要的模块；
 * 4. 逆序释放：releaseAll 按初始化逆序执行，保证释放顺序安全；
 * 5. 幂等：initAll
 * 可重复调用，已初始化的模块自动跳过。
 */
class ModuleRegistry {
public:
  /** 初始化回调：返回空表示成功，否则返回错误描述（禁用异常，使用 tl::expected） */
  using InitFn = std::function<tl::expected<void, std::string>()>;
  /** 释放回调 */
  using ReleaseFn = std::function<void()>;

  /** 模块注册信息 */
  struct ModuleInfo {
    std::string name;              ///< 模块名（全局唯一）
    std::string process;           ///< 适用进程类型，空串表示所有进程
    std::vector<std::string> deps; ///< 依赖的模块名列表
    InitFn init;                   ///< 初始化回调
    ReleaseFn release;             ///< 释放回调
  };

  /** 注册模块，同名重复注册以最后一次为准（幂等） */
  static void registerModule(const ModuleInfo &info) {
    auto &mods = modules();
    for (auto &m : mods) {
      if (m.name == info.name) {
        m = info;
        return;
      }
    }
    mods.push_back(info);
  }

  /** 初始化指定进程的全部模块，按依赖拓扑序执行，任一失败立即停止 */
  static tl::expected<void, std::string> initAll(const std::string &process) {
    // 按进程类型过滤
    std::vector<ModuleInfo> filtered;
    for (const auto &m : modules())
      if (m.process.empty() || m.process == process) filtered.push_back(m);

    const auto sorted = topoSort(filtered);
    if (!sorted) return tl::unexpected(sorted.error());

    for (const auto idx : *sorted) {
      const auto &mod = filtered[idx];
      if (initialized().contains(mod.name)) continue; // 幂等：已初始化则跳过
      if (!mod.init) return tl::unexpected("模块 [" + mod.name + "] 未提供初始化回调");
      const auto result = mod.init();
      if (!result) return tl::unexpected("模块 [" + mod.name + "] 初始化失败：" + result.error());
      initialized().insert(mod.name);
      initOrder().push_back(mod.name);
    }
    return {};
  }

  /** 按初始化逆序释放已初始化的模块 */
  static void releaseAll(const std::string &process) {
    const auto order = initOrder(); // 拷贝一份，逆序释放
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
      const auto found = std::find_if(modules().begin(), modules().end(), [&](const ModuleInfo &m) {
        return m.name == *it;
      });
      if (found != modules().end() && found->release) found->release();
    }
    initOrder().clear();
    initialized().clear();
  }

  /** 查询模块是否已完成初始化 */
  static bool isInitialized(const std::string &name) { return initialized().contains(name); }

  /** 已注册模块总数（调试用） */
  static std::size_t count() { return modules().size(); }

private:
  static std::vector<ModuleInfo> &modules() {
    static std::vector<ModuleInfo> instance;
    return instance;
  }

  static std::vector<std::string> &initOrder() {
    static std::vector<std::string> instance;
    return instance;
  }

  static std::unordered_set<std::string> &initialized() {
    static std::unordered_set<std::string> instance;
    return instance;
  }

  /** 拓扑排序（Kahn 算法），返回下标序列；依赖缺失或存在环时返回错误 */
  static tl::expected<std::vector<std::size_t>, std::string> topoSort(const std::vector<ModuleInfo> &mods) {
    std::unordered_map<std::string, std::size_t> index;
    for (std::size_t i = 0; i < mods.size(); ++i)
      index.emplace(mods[i].name, i);

    // 建图：依赖项 → 被依赖项
    std::vector<std::size_t> indegree(mods.size(), 0);
    std::vector<std::vector<std::size_t>> dependents(mods.size());
    for (std::size_t i = 0; i < mods.size(); ++i) {
      for (const auto &dep : mods[i].deps) {
        const auto it = index.find(dep);
        if (it == index.end()) return tl::unexpected("模块 [" + mods[i].name + "] 依赖的模块 [" + dep + "] 未注册");
        dependents[it->second].push_back(i);
        ++indegree[i];
      }
    }

    // Kahn 算法
    std::vector<std::size_t> ready;
    for (std::size_t i = 0; i < indegree.size(); ++i)
      if (indegree[i] == 0) ready.push_back(i);

    std::vector<std::size_t> order;
    order.reserve(mods.size());
    while (!ready.empty()) {
      const auto cur = ready.back();
      ready.pop_back();
      order.push_back(cur);
      for (const auto next : dependents[cur])
        if (--indegree[next] == 0) ready.push_back(next);
    }

    if (order.size() != mods.size()) {
      // 排序未覆盖全部模块 → 存在循环依赖，列出参与环的模块
      std::string names;
      for (std::size_t i = 0; i < mods.size(); ++i) {
        if (indegree[i] == 0) continue;
        if (!names.empty()) names += "、";
        names += mods[i].name;
      }
      return tl::unexpected("模块之间存在循环依赖，涉及模块：" + names);
    }
    return order;
  }
};

} // namespace alkaid

#endif // ALKAID_MODULE_REGISTRY_H

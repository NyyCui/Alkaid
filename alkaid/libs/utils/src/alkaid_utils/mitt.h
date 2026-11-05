#ifndef ALKAID_MITT_H
#define ALKAID_MITT_H

#include <BS_thread_pool.hpp>

#include <concepts>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <utility>
#include <vector>

/**
 * @file mitt.h
 * @brief 异步发布订阅事件系统
 *
 * 提供基于 C++20 concept 约束的类型安全事件机制，所有回调均异步提交到线程池执行。
 * 支持无参事件（MittEvent<>）和带参事件（MittEvent<const T&>），
 * 带参事件的参数仅拷贝/移动一次，由所有回调通过 shared_ptr 共享只读访问。
 *
 * 线程安全策略：
 * - 回调列表采用 shared_ptr + 写时复制（COW），emit 获取快照无需阻塞 on
 * - 每个事件实例持有独立的 shared_mutex，不同事件之间零锁竞争
 * - 快照模式下 emit 持锁时间仅为一次 shared_ptr 拷贝
*
 */
namespace alkaid::Mitt {

/**
 * @brief 常左值引用约束
 *
 * 事件参数类型必须是 const T& 形式，确保：
 * 1. 回调接收的是只读引用，无法修改原始数据
 * 2. 参数可以绑定到左值、临时值（右值），适用范围广
 * 3. 排除了右值引用和非 const 引用，避免悬挂引用和意外修改
 */
template <typename T>
concept IsConstLvalueReference =
    std::is_lvalue_reference_v<T> &&
    std::is_const_v<std::remove_reference_t<T>>;

/**
 * @brief 事件参数类型约束
 *
 * 合法的事件参数类型只有两种：
 * - void：表示无参事件
 * - const T&（T 为对象类型）：表示带参事件
 *
 * 额外排除了数组引用和 volatile 限定类型，因为：
 * - 数组在传参时会退化为指针，语义不一致
 * - volatile 对象无法安全地跨线程共享
 */
template <typename T>
concept EventType = std::same_as<T, void> ||
                    (IsConstLvalueReference<T> &&
                     std::is_object_v<std::remove_reference_t<T>> &&
                     !std::is_array_v<std::remove_reference_t<T>> &&
                     !std::is_volatile_v<std::remove_reference_t<T>>);

namespace _ {

/**
 * @brief 事件特征萃取器
 *
 * 根据事件参数类型 T 提取两个关键类型：
 * - value_type：参数的值类型（去除 cv 限定和引用），用于 make_shared 构造共享参数
 * - callback_type：对应的 std::function 签名类型
 *
 * 将 void 特化单独处理，避免形成 std::function<void(void)> 这种非法类型
 */
template <typename T>
struct EventTraits {
  using value_type = std::remove_cvref_t<T>;
  using callback_type = std::function<void(T)>;
};

/**
 * @brief void 事件的特征特化
 *
 * - value_type 定义为空占位结构体，防止用户意外构造并调用带参版 emit
 * - callback_type 为无参签名 std::function<void()>
 */
template <>
struct EventTraits<void> {
  struct value_type {};
  using callback_type = std::function<void()>;
};

/**
 * @brief 安全调用回调函数
 *
 * 在 try/catch 中执行回调，捕获所有异常并转发给全局异常处理函数。
 * 标记 noexcept 确保异常不会逃逸到线程池框架。
 *
 * @tparam F 回调函数类型（std::function 的引用）
 * @tparam Args 回调参数类型
 * @param callback 要调用的回调函数
 * @param args 传递给回调的参数
 */
template <typename F, typename... Args>
void invoke_callback(F &callback, Args &&...args) noexcept {
  try {
    std::invoke(callback, std::forward<Args>(args)...);
  } catch (const std::exception &e) {
    LOGGER_ERROR("事件处理失败：{}", e.what());
  } catch (...) {
    LOGGER_ERROR("事件处理失败：未知异常");
  }
}

inline std::shared_ptr<BS::thread_pool<>> pool() {
  static auto pool_ = std::make_shared<BS::thread_pool<>>();
  return pool_;
}

} // namespace _

/**
 * @brief 回调函数约束
 *
 * 验证 F 是否可以作为事件 T 的回调，需同时满足：
 * 1. T 是合法的事件类型（EventType）
 * 2. F 可拷贝构造（std::function 需要拷贝存储）
 * 3. F 可从传入类型构造（支持 lambda、函数指针、仿函数等）
 * 4. 调用签名匹配：
 *    - T = void 时：F 必须可无参调用且返回 void
 *    - T = const U& 时：F 必须接受 const U& 参数且返回 void
 *
 * 使用 requires 表达式而非 std::invoke_result_t，
 * 因为 requires 表达式在条件不满足时只是约束失败，
 * 而 std::invoke_result_t<F, void> 会触发硬编译错误。
 */
template <typename F, typename T>
concept EventCallbackConcept = EventType<T> &&
                               std::is_copy_constructible_v<std::decay_t<F>> &&
                               std::is_constructible_v<std::decay_t<F>, F> &&
                               ((std::same_as<T, void> && requires(std::decay_t<F> &f) {
                                  { std::invoke(f) } -> std::same_as<void>;
                                }) || (!std::same_as<T, void> && requires(std::decay_t<F> &f, T value) {
                                  { std::invoke(f, value) } -> std::same_as<void>;
                                }));

/**
 * @brief 回调函数类型别名
 *
 * T = void 时解析为 std::function<void()>
 * T = const int& 时解析为 std::function<void(const int&)>
 */
template <EventType T>
using EventCallback = typename _::EventTraits<T>::callback_type;

/**
 * @brief 事件条目
 *
 * 每个 MittEvent 实例持有一个 EventEntry，包含：
 * - callbacks：回调列表，用 shared_ptr 包装以支持写时复制（COW）
 * - mtx：独立读写锁，保护 callbacks 的并发访问
 *
 * COW 机制说明：
 * emit 通过 shared_lock 获取 callbacks 的 shared_ptr 快照（仅一次原子引用计数 +1），
 * on 通过 unique_lock 修改 callbacks。当有 emit 持有快照时，on 会复制整个列表
 * 再追加新回调（写时复制），确保已获取的快照不被修改。
 * 当无读者时（use_count == 1），on 直接原地追加，避免不必要的拷贝。
 *
 * @tparam T 事件参数类型
 */
template <EventType T>
struct EventEntry {
  using CallbackList = std::vector<std::shared_ptr<EventCallback<T>>>;

  std::shared_ptr<CallbackList> callbacks = std::make_shared<CallbackList>();
  mutable std::shared_mutex mtx;
};

/**
 * @brief 异步发布订阅事件
 *
 * 支持两种模式：
 * - 无参事件：MittEvent<> 或 MittEvent<void>
 * - 带参事件：MittEvent<const T&>（T 为对象类型）
 *
 * @par 基本用法
 * @code
 * // 无参事件
 * alkaid::Mitt::MittEvent event;
 * event.on([] {});
 * event.emit();
 *
 * // 带参事件
 * struct A {};
 * alkaid::Mitt::MittEvent<const A &> event2;
 * event2.on([](const A &value) {});
 * event2.emit(A());       // 右值，移动构造
 * A value;
 * event2.emit(value);     // 左值，拷贝构造
 * @endcode
 *
 * @par 并发契约
 * - 同一对象的 on / emit 可并发调用；回调始终提交到线程池执行
 * - 同一回调可以同时执行多次；不保证回调开始/完成顺序或固定工作线程
 * - 回调可能在 emit 返回前开始；回调中可再次 on / emit
 * - 回调不得在所属线程池的工作线程中等待该池全部任务完成（避免死锁）
 * - 析构不等待、也不取消已提交的回调；析构不能与本对象成员调用并发
 * - 参数只按其类型的构造语义复制/移动；指针、view、引用成员不深拷贝
 * - 回调捕获的 this/引用及参数指向的外部对象，其生命周期和同步由调用方负责
 * - 发布时分配/入队失败会向调用方抛异常；可能已有部分订阅成功入队
 * - 无背压或队列上限，生产速度持续超过消费速度时需调用方限流
 *
 * @tparam T 事件参数类型，void 表示无参，const U& 表示带参，默认 void
 */
template <EventType T = void>
class MittEvent {
  /// 参数的值类型（去除 cv 和引用）。void 时为空占位结构体
  using value_type = typename _::EventTraits<T>::value_type;
  /// 回调列表类型
  using CallbackList = typename EventEntry<T>::CallbackList;
  /// 快照类型：指向常量回调列表的 shared_ptr，emit 通过拷贝此指针获取快照
  using Snapshot = std::shared_ptr<const CallbackList>;

  std::shared_ptr<BS::thread_pool<>> pool_;
  /// 事件条目：回调列表 + 读写锁
  EventEntry<T> entry_;
  /**
   * @brief 获取回调列表快照
   *
   * 通过 shared_lock 获取 callbacks 的 shared_ptr 拷贝。
   * 仅一次原子引用计数操作，无论回调数量多少，开销恒定 O(1)。
   * 快照在 emit 遍历期间保持回调列表有效，即使 on 同时触发 COW 也不影响。
   *
   * @return 回调列表的只读快照
   */
  Snapshot snapshot() const {
    std::shared_lock lock(entry_.mtx);
    return entry_.callbacks;
  }

  /**
   * @brief 带参事件发布的核心实现
   *
   * 流程：
   * 1. 获取快照，若无回调则直接返回（避免无意义的参数拷贝和任务提交）
   * 2. 将参数通过 shared_ptr 共享分配，仅拷贝/移动一次
   * 3. 遍历快照，为每个回调创建线程池任务，任务捕获 shared_ptr 共享参数
   *
   * @tparam U 参数转发类型（左值引用触发拷贝，右值引用触发移动）
   * @param value 事件参数
   */
  template <typename U>
  void emit_value(U &&value) const {
    const auto callbacks = snapshot();
    if (callbacks->empty()) return;

    // 参数只构造一次，所有回调通过 shared_ptr 共享只读访问
    auto param = std::make_shared<const value_type>(std::forward<U>(value));
    // 缓存池指针到局部变量，避免循环中重复访问成员
    for (const auto &callback : *callbacks) {
      pool_->detach_task([callback, param]() noexcept {
        _::invoke_callback(*callback, *param);
      });
    }
  }

public:
  /**
   * @brief 默认构造，使用全局线程池
   *
   * 通过委托构造调用 exec_pool()，确保静态事件对象构造时就初始化默认池，
   * 使默认池的生命周期覆盖所有静态事件。
   */
  MittEvent() : pool_(_::pool()) {}
  explicit MittEvent(std::shared_ptr<BS::thread_pool<>> pool) : pool_(pool) {}

  MittEvent(const MittEvent &) = delete;
  MittEvent &operator=(const MittEvent &) = delete;
  MittEvent(MittEvent &&) = delete;
  MittEvent &operator=(MittEvent &&) = delete;

  /**
   * @brief 创建使用默认线程池的事件（堆分配）
   * @return 事件的 shared_ptr
   */
  static std::shared_ptr<MittEvent> make_event() {
    return std::make_shared<MittEvent>();
  }

  /**
   * @brief 创建使用自定义线程池的事件（堆分配）
   * @param pool 线程池引用
   * @return 事件的 shared_ptr
   */
  static std::shared_ptr<MittEvent> make_event(BS::thread_pool<> &pool) {
    return std::make_shared<MittEvent>(pool);
  }

  /**
   * @brief 订阅事件
   *
   * 注册一个回调函数，当事件发布时异步执行。
   * 采用写时复制（COW）策略管理回调列表：
   *
   * 1. 先在锁外构造 std::function（用户回调的拷贝/移动构造可能执行用户代码）
   * 2. 获取独占锁后检查引用计数：
   *    - use_count == 1：无读者持有快照，原地追加，vector 扩容可摊销
   *    - use_count > 1：有读者持有快照，复制整个列表后追加（COW），
   *      保证已获取快照的 emit 遍历的仍是旧列表
   *
   * @tparam H 回调类型，须满足 EventCallbackConcept 约束
   * @param handler 回调函数（支持 lambda、函数指针、仿函数）
   * @throws std::invalid_argument 当 handler 构造出空的 std::function 时
   */
  template <typename H>
    requires EventCallbackConcept<H, T>
  void on(H &&handler) {
    // 用户回调的复制/移动构造可能执行用户代码，放在列表锁之外
    auto callback = std::make_shared<EventCallback<T>>(std::forward<H>(handler));

    std::unique_lock lock(entry_.mtx);
    if (entry_.callbacks.use_count() == 1) {
      // 独占锁禁止新增读快照；引用数为 1 时现存读者为零，可原地追加。
      // 不复制整个列表，连续注册的 vector 扩容成本可以摊销。
      entry_.callbacks->push_back(std::move(callback));
      return;
    }

    // 有发布者持有快照时才写时复制，已获取的快照保持不变
    auto next = std::make_shared<CallbackList>();
    next->reserve(entry_.callbacks->size() + 1);
    next->insert(next->end(), entry_.callbacks->begin(), entry_.callbacks->end());
    next->push_back(std::move(callback));
    entry_.callbacks = std::move(next);
  }

  /**
   * @brief 发布无参事件
   *
   * 获取回调快照后，将每个回调提交到线程池异步执行。
   * 仅在 T = void 时可用（由 requires 子句约束）。
   */
  void emit() const
    requires std::same_as<T, void>
  {
    const auto callbacks = snapshot();
    if (callbacks->empty()) return;

    for (const auto &callback : *callbacks) {
      pool_->detach_task([callback]() noexcept {
        _::invoke_callback(*callback);
      });
    }
  }

  /**
   * @brief 发布带参事件（左值引用重载）
   *
   * 接受 const 左值引用，通过拷贝构造创建共享参数。
   * 仅在 T != void 且 value_type 支持拷贝构造时可用。
   *
   * void 的 value_type 为空占位结构体，此重载通过 requires 排除，
   * 避免形成 void& 等非法类型。
   *
   * @param value 事件参数（将被拷贝到 shared_ptr 中供所有回调共享）
   */
  void emit(const value_type &value) const
    requires(!std::same_as<T, void> &&
             std::is_constructible_v<value_type, const value_type &>)
  {
    emit_value(value);
  }

  /**
   * @brief 发布带参事件（右值引用重载）
   *
   * 接受右值引用，通过移动构造创建共享参数，减少不必要的深拷贝。
   * 仅在 T != void 且 value_type 支持移动构造时可用。
   *
   * @param value 事件参数（将被移动到 shared_ptr 中供所有回调共享）
   */
  void emit(value_type &&value) const
    requires(!std::same_as<T, void> &&
             std::is_constructible_v<value_type, value_type &&>)
  {
    emit_value(std::move(value));
  }
};

} // namespace alkaid::Mitt

#endif // ALKAID_MITT_H

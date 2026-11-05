#ifndef ALKAID_CTRL_SNOWFLAKE_H
#define ALKAID_CTRL_SNOWFLAKE_H

#include <chrono>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>

namespace alkaid {
/**
 * 雪花算法 ID 生成器
 * 生成64位唯一ID，结构为：
 * - 1位符号位 (始终为0)
 * - 41位时间戳 (毫秒级)
 * - 5位数据中心ID
 * - 5位工作机器ID
 * - 12位序列号
 */
class Snowflake {
  using TimePoint = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>;

  // 常量定义
  static constexpr uint64_t WorkerIdBits = 5;     // 工作机器ID位数
  static constexpr uint64_t DatacenterIdBits = 5; // 数据中心ID位数
  static constexpr uint64_t SequenceBits = 12;    // 序列号位数

  static constexpr uint64_t MaxWorkerId = (1ULL << WorkerIdBits) - 1;         // 最大工作机器ID
  static constexpr uint64_t MaxDatacenterId = (1ULL << DatacenterIdBits) - 1; // 最大数据中心ID
  static constexpr uint64_t MaxSequence = (1ULL << SequenceBits) - 1;         // 序列号掩码

  static constexpr uint64_t WorkerIdShift = SequenceBits;                                    // 工作机器ID左移位数
  static constexpr uint64_t DatacenterIdShift = SequenceBits + WorkerIdBits;                 // 数据中心ID左移位数
  static constexpr uint64_t TimestampShift = SequenceBits + WorkerIdBits + DatacenterIdBits; // 时间戳左移位数

  // 成员变量
  const uint64_t workerId_;     // 工作机器ID
  const uint64_t datacenterId_; // 数据中心ID
  const TimePoint epoch_;       // 纪元时间点

  uint64_t sequence_ = 0;      // 当前序列号
  uint64_t lastTimestamp_ = 0; // 上次生成ID的时间戳

  mutable std::mutex idMutex_; // 互斥锁，保证线程安全

  // 获取当前时间戳（毫秒，相对于纪元）
  uint64_t currentTimestamp() const {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    return std::chrono::duration_cast<std::chrono::milliseconds>(ms - epoch_).count();
  }

  // 等待下一毫秒
  uint64_t waitNextMillis(uint64_t lastTimestamp) const {
    uint64_t timestamp;
    do {
      timestamp = currentTimestamp();
    } while (timestamp <= lastTimestamp);

    return timestamp;
  }

public:
  // 类型别名

  /**
   * 构造函数
   * @param workerId 工作机器ID (0-31)
   * @param datacenterId 数据中心ID (0-31)
   * @param epoch 自定义纪元时间点，默认为2025-01-01 00:00:00
   */
  explicit Snowflake(
      const uint64_t workerId,
      const uint64_t datacenterId,
      const TimePoint epoch = TimePoint{std::chrono::milliseconds{1735660800000}})
      : workerId_(workerId), datacenterId_(datacenterId), epoch_(epoch) {
    // 检查 workerId 是否超出最大允许值
    if (workerId > MaxWorkerId) {
      // 若超出范围，抛出异常提示 workerId 必须在 0 到 kMaxWorkerId 之间
      throw std::invalid_argument("机器 Id 必须为 0 ~ " + std::to_string(MaxWorkerId));
    }
    // 检查 datacenterId 是否超出最大允许值
    if (datacenterId > MaxDatacenterId) {
      // 若超出范围，抛出异常提示 datacenterId 必须在 0 到 kMaxDatacenterId 之间
      throw std::invalid_argument("数据中心 Id 必须为 0 ~ " + std::to_string(MaxDatacenterId));
    }
  }

  // 禁用拷贝构造和赋值
  Snowflake(const Snowflake &) = delete;

  Snowflake &operator=(const Snowflake &) = delete;

  /**
   * @brief 生成下一个唯一 ID。
   *
   * 该方法使用雪花算法生成一个 64 位的唯一 ID，包含时间戳、数据中心 ID、工作机器 ID 和序列号。
   * 使用互斥锁保证线程安全，处理时钟回拨问题。
   *
   * @return uint64_t 生成的唯一 ID。
   * @throws std::runtime_error 当发生时钟回拨时抛出。
   */
  uint64_t nextId() {
    // 使用互斥锁保证线程安全，确保同一时间只有一个线程能生成 ID
    std::lock_guard lock(idMutex_);

    // 获取当前时间戳（相对于 epoch 的毫秒数）
    uint64_t currentTime = currentTimestamp();

    // 处理时钟回拨问题
    if (currentTime < lastTimestamp_) {
      // 若当前时间戳小于上一次生成 ID 的时间戳，说明发生了时钟回拨
      // 抛出异常并提示时钟回拨的毫秒数
      // 计算时间差（毫秒）
      const uint64_t diff = lastTimestamp_ - currentTime;
      // 转换为更友好的时间格式
      const uint64_t days = diff / (1000 * 60 * 60 * 24);
      const uint64_t hours = (diff % (1000 * 60 * 60 * 24)) / (1000 * 60 * 60);
      const uint64_t minutes = (diff % (1000 * 60 * 60)) / (1000 * 60);
      const uint64_t seconds = (diff % (1000 * 60)) / 1000;
      const uint64_t milliseconds = diff % 1000;

      throw std::runtime_error(
          "检测到时钟回拨！拒绝生成ID。回拨时间: " +
          std::to_string(days) + "天" +
          std::to_string(hours) + "小时" +
          std::to_string(minutes) + "分钟" +
          std::to_string(seconds) + "秒" +
          std::to_string(milliseconds) + "毫秒");
    }

    // 若当前时间戳与上一次生成 ID 的时间戳相同
    if (currentTime == lastTimestamp_) {
      // 序列号加 1 并通过按位与操作确保序列号在允许范围内
      sequence_ = ++sequence_ & MaxSequence;
      if (sequence_ == 0) {
        currentTime = waitNextMillis(lastTimestamp_);
      }
    } else {
      sequence_ = 0;
    }

    lastTimestamp_ = currentTime;

    return (currentTime << TimestampShift) |
           (datacenterId_ << DatacenterIdShift) |
           (workerId_ << WorkerIdShift) |
           sequence_;
  }

  /**
   * @brief 生成下一个字符串类型的唯一ID
   * @return 生成的字符串形式的唯一ID
   * @throws std::runtime_error 如果时钟回拨超过容忍范围
   */
  std::string nextIdStr() {
    return std::to_string(nextId());
  }

  /**
   * 获取当前时间戳（相对于纪元）
   * @return 当前时间戳（毫秒）
   */
  uint64_t getTimestamp() const {
    return currentTimestamp();
  }

  /**
   * 从ID解析时间戳
   * @param id 雪花算法生成的ID
   * @param epoch 生成ID时使用的纪元时间
   * @return 对应的时间点
   */
  static TimePoint extractTimePoint(const uint64_t id, const TimePoint epoch = TimePoint{std::chrono::milliseconds{1735660800000}}) {
    const uint64_t timestamp = id >> TimestampShift;
    return epoch + std::chrono::milliseconds{timestamp};
  }

  static std::shared_ptr<Snowflake> &getInstance() {
    static auto snowflake = std::make_shared<Snowflake>(1, 1);
    return snowflake;
  }

  static std::string genStrId() {
    return getInstance()->nextIdStr();
  }

  static uint64_t genIntId() {
    return getInstance()->nextId();
  }
};
} // namespace alkaid

#endif // ALKAID_CTRL_SNOWFLAKE_H

#ifndef ALKAID_LOCAL_DATETIME_H
#define ALKAID_LOCAL_DATETIME_H

#include <compare>
#include <memory>
#include <string>
#include <tl/expected.hpp>
#include <unicode/calendar.h>
#include <unicode/parsepos.h>
#include <unicode/smpdtfmt.h>
#include <unicode/stringpiece.h>
#include <unicode/timezone.h>
#include <unicode/ucal.h>
#include <unicode/unistr.h>
#include <unicode/utypes.h>
#include <utility>
#include <alkaid_log/logging.h>

/**
 * @file local_date_time.h
 * @brief 本地完整日期时间工具类，基于 ICU 库实现
 * @note 不可变值类型，所有修改操作返回新对象；不关联UTC，仅存储北京时间值
 * @note yyyy（yy）-->年（四位、两位）、MM（M）-->月（两位、一位）、dd（d）-->日、
 * @note HH（H）-->时（24H，两位、一位）、hh（h）-->时（12H，两位、一位）、mm（m）-->分（两位、一位）、ss（s）-->秒（两位、一位）、SSS-->毫秒（三位）
 * @note a-->上午/下午、E（EEEE）-->星期（短、长：周一，星期一）
 */
namespace alkaid {

/**
 * @brief 时间间隔/持续时间结构体
 * @note 各字段可正可负，正数表示未来方向，负数表示过去方向
 */
struct Duration {
  int32_t years = 0;   ///< 年数
  int32_t months = 0;  ///< 月数
  int32_t weeks = 0;   ///< 周数
  int32_t days = 0;    ///< 天数
  int32_t hours = 0;   ///< 小时数
  int32_t minutes = 0; ///< 分钟数
  int32_t seconds = 0; ///< 秒数
  int32_t millis = 0;  ///< 毫秒数

  /// @brief 取反操作，返回各字段符号取反后的新 Duration
  Duration operator-() const {
    return Duration{-years, -months, -weeks, -days, -hours, -minutes, -seconds, -millis};
  }

  /**
   * @brief 将时间间隔格式化为可读字符串
   * @param longDesc 是否使用完整描述（组合所有非零字段），false 时仅展示最大单位
   * @param suffix 是否附加"前"/"后"后缀
   * @return 格式化后的字符串，如 "3小时"、"2天前后"；全部为零时返回空字符串
   */
  std::string pretty(const bool longDesc = false, const bool suffix = false) const {
    if (longDesc) {
      if (years == 0 && months == 0 && weeks == 0 && days == 0 && hours == 0 && minutes == 0 && seconds == 0 && millis == 0) return "";
      std::string result;
      if (years != 0) result += std::format("{}年", std::abs(years));
      if (months != 0) result += std::format("{}个月", std::abs(months));
      if (weeks != 0) result += std::format("{}周", std::abs(weeks));
      if (days != 0) result += std::format("{}天", std::abs(days));
      if (hours != 0) result += std::format("{}小时", std::abs(hours));
      if (minutes != 0) result += std::format("{}分钟", std::abs(minutes));
      if (seconds != 0) result += std::format("{}秒", std::abs(seconds));
      if (millis != 0) result += std::format("{}毫秒", std::abs(millis));
      if (suffix) {
        const bool positive = years > 0 || months > 0 || weeks > 0 || days > 0 || hours > 0 || minutes > 0 || seconds > 0 || millis > 0;
        return result + (positive ? "后" : "前");
      }
      return result;
    }
    if (years > 0) return std::format("{}年{}", years, suffix ? "后" : "");
    if (years < 0) return std::format("{}年{}", -years, suffix ? "前" : "");

    if (months > 0) return std::format("{}个月{}", months, suffix ? "后" : "");
    if (months < 0) return std::format("{}个月{}", -months, suffix ? "前" : "");

    if (weeks > 0) return std::format("{}周{}", weeks, suffix ? "后" : "");
    if (weeks < 0) return std::format("{}周{}", -weeks, suffix ? "前" : "");

    if (days > 0) return std::format("{}天{}", days, suffix ? "后" : "");
    if (days < 0) return std::format("{}天{}", -days, suffix ? "前" : "");

    if (hours > 0) return std::format("{}小时{}", hours, suffix ? "后" : "");
    if (hours < 0) return std::format("{}小时{}", -hours, suffix ? "前" : "");

    if (minutes > 0) return std::format("{}分钟{}", minutes, suffix ? "后" : "");
    if (minutes < 0) return std::format("{}分钟{}", -minutes, suffix ? "前" : "");

    if (seconds > 0) return std::format("{}秒{}", seconds, suffix ? "后" : "");
    if (seconds < 0) return std::format("{}秒{}", -seconds, suffix ? "前" : "");

    if (millis > 0) return std::format("{}毫秒{}", millis, suffix ? "后" : "");
    if (millis < 0) return std::format("{}毫秒{}", -millis, suffix ? "前" : "");

    return "";
  }
};

/**
 * @brief 日期时间对象（北京时间），不可变
 */
class LocalDateTime {
  std::unique_ptr<icu::Calendar> calendar_; ///< ICU 日历对象（持有北京时区）
  UDate timestamp_;                         ///< UTC 时间戳（毫秒）
  int32_t year_;                            ///< 年
  int32_t month_;                           ///< 月（1-12）
  int32_t day_;                             ///< 日（1-31）
  int32_t hour_;                            ///< 时（0-23）
  int32_t minute_;                          ///< 分（0-59）
  int32_t second_;                          ///< 秒（0-59）
  int32_t ms_;                              ///< 毫秒（0-999）
  UCalendarDaysOfWeek weekday_;             ///< 星期几

  /**
   * @brief 私有构造函数，从 ICU Calendar 对象提取所有日期时间字段
   * @param calendar ICU 日历对象（所有权转移）
   */
  LocalDateTime(std::unique_ptr<icu::Calendar> calendar) : calendar_(std::move(calendar))
                                                         , timestamp_(0.0)
                                                         , year_(1970)
                                                         , month_(1)
                                                         , day_(1)
                                                         , hour_(0)
                                                         , minute_(0)
                                                         , second_(0)
                                                         , ms_(0)
                                                         , weekday_(UCAL_SUNDAY) {
    if (!calendar_) return;
    UErrorCode status = U_ZERO_ERROR;
    timestamp_ = calendar_->getTime(status);
    year_ = calendar_->get(UCAL_YEAR, status);
    month_ = calendar_->get(UCAL_MONTH, status) + 1;
    day_ = calendar_->get(UCAL_DATE, status);
    hour_ = calendar_->get(UCAL_HOUR_OF_DAY, status);
    minute_ = calendar_->get(UCAL_MINUTE, status);
    second_ = calendar_->get(UCAL_SECOND, status);
    ms_ = calendar_->get(UCAL_MILLISECOND, status);
    weekday_ = static_cast<UCalendarDaysOfWeek>(calendar_->get(UCAL_DAY_OF_WEEK, status));
  }

  /**
   * @brief 内部方法：对指定字段增加指定量，返回新对象
   * @param field ICU 日期字段枚举
   * @param amount 增加的量（正负）
   * @return 成功返回新的 LocalDateTime，失败返回错误信息
   */
  tl::expected<LocalDateTime, std::string> add(const UCalendarDateFields field, const int32_t amount) const {
    if (!calendar_) {
      return tl::make_unexpected(std::string("无效的日期"));
    }

    auto calendar = std::unique_ptr<icu::Calendar>(calendar_->clone());
    if (!calendar) {
      return tl::make_unexpected(std::string("无效的日期"));
    }

    UErrorCode status = U_ZERO_ERROR;
    calendar->add(field, amount, status);
    if (U_FAILURE(status)) {
      return tl::make_unexpected(std::string("无效的日期"));
    }
    return LocalDateTime(std::move(calendar));
  }

public:
  /// @brief 拷贝构造，深拷贝 ICU Calendar 对象
  LocalDateTime(const LocalDateTime &other)
    : LocalDateTime(other.calendar_ ? std::unique_ptr<icu::Calendar>(other.calendar_->clone()) : nullptr) {}

  /// @brief 拷贝赋值，使用 copy-and-swap 惯用法
  LocalDateTime &operator=(const LocalDateTime &other) {
    if (this != &other) {
      LocalDateTime tmp(other);
      swap(tmp);
    }
    return *this;
  }

  /// @brief 交换两个对象的所有成员
  void swap(LocalDateTime &other) noexcept {
    std::swap(calendar_, other.calendar_);
    std::swap(timestamp_, other.timestamp_);
    std::swap(year_, other.year_);
    std::swap(month_, other.month_);
    std::swap(day_, other.day_);
    std::swap(hour_, other.hour_);
    std::swap(minute_, other.minute_);
    std::swap(second_, other.second_);
    std::swap(ms_, other.ms_);
    std::swap(weekday_, other.weekday_);
  }

  /// @brief  spaceship 比较运算符，按时间先后比较
  std::strong_ordering operator<=>(const LocalDateTime &rhs) const {
    UErrorCode status = U_ZERO_ERROR;
    if (calendar_->before(*rhs.calendar_, status)) return std::strong_ordering::less;
    if (calendar_->after(*rhs.calendar_, status)) return std::strong_ordering::greater;
    return std::strong_ordering::equal;
  }

  /// @brief 相等比较运算符
  bool operator==(const LocalDateTime &rhs) const {
    UErrorCode status = U_ZERO_ERROR;
    return calendar_->equals(*rhs.calendar_, status);
  }

  static LocalDateTime nulldt() {
    return LocalDateTime(nullptr);
  }

  /**
   * @brief 获取当前北京时间
   * @return 成功返回当前 LocalDateTime，失败返回错误信息
   */
  static LocalDateTime now() {
    UErrorCode status = U_ZERO_ERROR;
    auto calendar = std::unique_ptr<icu::Calendar>(icu::Calendar::createInstance(icu::TimeZone::createTimeZone(icu::UnicodeString::fromUTF8("Asia/Shanghai")), icu::Locale::getChinese(), status));
    if (U_FAILURE(status) || !calendar) {
      LOGGER_ERROR("无效的日期");
      return nulldt();
    }
    return LocalDateTime(std::move(calendar));
  }

  /**
   * @brief 从字符串解析日期时间
   * @param str 日期时间字符串
   * @param fmt ICU 格式串，默认 "yyyy-MM-dd HH:mm:ss"
   * @return 成功返回解析后的 LocalDateTime，失败返回错误信息
   */
  static tl::expected<LocalDateTime, std::string> parse(const std::string &str, const std::string &fmt = "yyyy-MM-dd HH:mm:ss") {
    UErrorCode status = U_ZERO_ERROR;

    const icu::UnicodeString pattern = icu::UnicodeString::fromUTF8(icu::StringPiece(fmt.data(), static_cast<int32_t>(fmt.size())));

    icu::SimpleDateFormat sdf(pattern, icu::Locale::getChinese(), status);
    if (U_FAILURE(status)) {
      return tl::make_unexpected(std::string("无效的日期格式"));
    }
    const auto tz = icu::TimeZone::createTimeZone(icu::UnicodeString::fromUTF8("Asia/Shanghai"));
    sdf.setTimeZone(*tz);

    const icu::UnicodeString timeStr = icu::UnicodeString::fromUTF8(icu::StringPiece(str.data(), static_cast<int32_t>(str.size())));

    icu::ParsePosition pos(0);
    const UDate date = sdf.parse(timeStr, pos);

    if (pos.getErrorIndex() >= 0 || pos.getIndex() != timeStr.length()) {
      return tl::make_unexpected(std::string("无效的日期"));
    }
    auto calendar = std::unique_ptr<icu::Calendar>(icu::Calendar::createInstance(tz, icu::Locale::getChinese(), status));
    if (U_FAILURE(status) || !calendar) {
      return tl::make_unexpected(std::string("无效的日期"));
    }
    calendar->setTime(date, status);
    return LocalDateTime(std::move(calendar));
  }

  /**
   * @brief 从 UTC 时间戳构造日期时间
   * @param timestamp 毫秒级时间戳（自 1970-01-01 00:00:00 UTC 起算）
   * @return 成功返回对应的 LocalDateTime，失败返回错误信息
   */
  static tl::expected<LocalDateTime, std::string> of(const int64_t timestamp) {
    UErrorCode status = U_ZERO_ERROR;
    auto calendar = std::unique_ptr<icu::Calendar>(icu::Calendar::createInstance(icu::TimeZone::createTimeZone(icu::UnicodeString::fromUTF8("Asia/Shanghai")), icu::Locale::getChinese(), status));
    if (U_FAILURE(status) || !calendar) {
      return tl::make_unexpected(std::string("无效的日期"));
    }
    calendar->setTime(timestamp, status);
    return LocalDateTime(std::move(calendar));
  }

  /**
   * @brief 从年月日时分秒毫秒构造日期时间
   * @param year 年
   * @param month 月（1-12），默认 1
   * @param day 日（1-31），默认 1
   * @param hour 时（0-23），默认 0
   * @param minute 分（0-59），默认 0
   * @param second 秒（0-59），默认 0
   * @param ms 毫秒（0-999），默认 0
   * @return 成功返回对应的 LocalDateTime，失败返回错误信息
   */
  static tl::expected<LocalDateTime, std::string> of(
    const int32_t year,
    const int32_t month = 1,
    const int32_t day = 1,
    const int32_t hour = 0,
    const int32_t minute = 0,
    const int32_t second = 0,
    const int32_t ms = 0
  ) {
    UErrorCode status = U_ZERO_ERROR;
    auto calendar = std::unique_ptr<icu::Calendar>(icu::Calendar::createInstance(icu::TimeZone::createTimeZone(icu::UnicodeString::fromUTF8("Asia/Shanghai")), icu::Locale::getChinese(), status));
    if (U_FAILURE(status) || !calendar) {
      return tl::make_unexpected(std::string("无效的日期"));
    }
    calendar->set(year, month - 1, day, hour, minute, second);
    calendar->set(UCAL_MILLISECOND, ms);
    return LocalDateTime(std::move(calendar));
  }

  /// @return 时间戳（自 1970-01-01 00:00:00 UTC 起算，毫秒）
  int64_t timestamp() const {
    return static_cast<int64_t>(timestamp_);
  }

  /// @return 毫秒（0-999）
  int32_t millisecond() const {
    return ms_;
  }

  /// @return 秒（0-59）
  int32_t second() const {
    return second_;
  }

  /// @return 分钟（0-59）
  int32_t minute() const {
    return minute_;
  }

  /// @return 小时（0-23）
  int32_t hour() const {
    return hour_;
  }

  /// @return 日（1-31）
  int32_t day() const {
    return day_;
  }

  /// @return 月（1-12）
  int32_t month() const {
    return month_;
  }

  /// @return 年
  int32_t year() const {
    return year_;
  }

  /**
   * @brief 获取星期几的文本描述
   * @param fullName true 返回全称（如"星期一"），false 返回简称（如"周一"）
   * @return 星期文本字符串
   */
  std::string weekday(const bool fullName = false) const {
    return fullName ? format("EEEE") : format("E");
  }

  /**
   * @brief 获取星期几的数值（ISO 风格）
   * @return 1（周一）~ 7（周日），失败返回 0
   */
  int32_t dayOfWeek() const {
    switch (weekday_) {
      case UCAL_SUNDAY: return 7;
      case UCAL_MONDAY: return 1;
      case UCAL_TUESDAY: return 2;
      case UCAL_WEDNESDAY: return 3;
      case UCAL_THURSDAY: return 4;
      case UCAL_FRIDAY: return 5;
      case UCAL_SATURDAY: return 6;
      default: return 0;
    }
  }

  /// @return 当年中的第几天（1-366）
  int32_t dayOfYear() const {
    if (calendar_ == nullptr) return 0;
    UErrorCode status = U_ZERO_ERROR;
    int32_t val = calendar_->get(UCAL_DAY_OF_YEAR, status);
    return U_FAILURE(status) ? 0 : val;
  }

  /// @return 当年中的第几周（1-53）
  uint32_t weekOfYear() const {
    if (calendar_ == nullptr) return 0;
    UErrorCode status = U_ZERO_ERROR;
    int32_t val = calendar_->get(UCAL_WEEK_OF_YEAR, status);
    return U_FAILURE(status) ? 0 : val;
  }

  /// @return 当月中的第几周（1-5）
  uint32_t weekOfMonth() const {
    if (calendar_ == nullptr) return 0;
    UErrorCode status = U_ZERO_ERROR;
    int32_t val = calendar_->get(UCAL_WEEK_OF_MONTH, status);
    return U_FAILURE(status) ? 0 : val;
  }

  /// @return 每周第一天的编号文本（ICU 枚举值）
  std::string firstDayOfWeek() const {
    if (calendar_ == nullptr) return "";
    UErrorCode status = U_ZERO_ERROR;
    auto week = calendar_->getFirstDayOfWeek(status);

    return U_FAILURE(status) ? "" : std::to_string(week);
  }

  /// @brief 判断是否为周末（周六或周日）
  bool isWeekend() const {
    return calendar_->isWeekend();
  }

  /// @brief 判断是否为上午（0:00 - 11:59）
  bool isAM() const {
    return hour_ < 12;
  }

  /// @brief 判断是否为下午（12:00 - 23:59）
  bool isPM() const {
    return hour_ >= 12;
  }

  /// @return 当月总天数（28-31）
  uint32_t daysInMonth() const {
    if (calendar_ == nullptr) return 0;
    UErrorCode status = U_ZERO_ERROR;
    int32_t val = calendar_->getActualMaximum(UCAL_DATE, status);
    return U_FAILURE(status) ? 0 : val;
  }

  /// @brief 判断当前年份是否为闰年
  bool isLeapYear() const {
    if (calendar_ == nullptr) return false;
    UErrorCode status = U_ZERO_ERROR;
    return calendar_->inTemporalLeapYear(status);
  }

  /**
   * @brief 增加 N 毫秒，返回新对象
   * @param milliseconds
   */
  LocalDateTime addMilliseconds(const int32_t milliseconds) const {
    return add(UCAL_MILLISECOND, milliseconds).value();
  }

  /**
   * @brief 增加 N 秒，返回新对象
   * @param seconds
   */
  LocalDateTime addSeconds(const int32_t seconds) const {
    return add(UCAL_SECOND, seconds).value();
  }

  /**
   * @brief 增加 N 分钟，返回新对象
   * @param minutes
   */
  LocalDateTime addMinutes(const int32_t minutes) const {
    return add(UCAL_MINUTE, minutes).value();
  }

  /**
   * @brief 增加 N 小时，返回新对象
   * @param hours
   */
  LocalDateTime addHours(const int32_t hours) const {
    return add(UCAL_HOUR_OF_DAY, hours).value();
  }

  /**
   * @brief 增加 N 天，返回新对象
   * @param days
   */
  LocalDateTime addDays(const int32_t days) const {
    return add(UCAL_DATE, days).value();
  }

  /**
   * @brief 增加 N 月，返回新对象
   * @param months
   */
  LocalDateTime addMonths(const int32_t months) const {
    return add(UCAL_MONTH, months).value();
  }

  /**
   * @brief 增加 N 周，返回新对象
   * @param weeks
   */
  LocalDateTime addWeeks(const int32_t weeks) const {
    return add(UCAL_WEEK_OF_YEAR, weeks).value();
  }

  /**
   * @brief 增加 N 年，返回新对象
   * @param years 年数（正负）
   */
  LocalDateTime addYears(const int32_t years) const {
    return add(UCAL_YEAR, years).value();
  }

  /// @brief 增加一段时间，返回新对象
  LocalDateTime addDuration(const Duration &duration) const {
    if (!calendar_) return LocalDateTime(nullptr);

    auto calendar = std::unique_ptr<icu::Calendar>(calendar_->clone());
    if (!calendar) return LocalDateTime(nullptr);

    UErrorCode status = U_ZERO_ERROR;
    calendar->add(UCAL_MILLISECOND, duration.millis, status);
    calendar->add(UCAL_SECOND, duration.seconds, status);
    calendar->add(UCAL_MINUTE, duration.minutes, status);
    calendar->add(UCAL_HOUR_OF_DAY, duration.hours, status);
    calendar->add(UCAL_WEEK_OF_YEAR, duration.weeks, status);
    calendar->add(UCAL_MONTH, duration.months, status);
    calendar->add(UCAL_DATE, duration.days, status);
    calendar->add(UCAL_YEAR, duration.years, status);
    if (U_FAILURE(status)) return LocalDateTime(nullptr);
    return LocalDateTime(std::move(calendar));
  }

  /// @return 当天 00:00:00.000
  LocalDateTime beginOfDay() const {
    if (!calendar_) return {nullptr};
    auto calendar = std::unique_ptr<icu::Calendar>(calendar_->clone());
    calendar->set(UCAL_HOUR_OF_DAY, 0);
    calendar->set(UCAL_MINUTE, 0);
    calendar->set(UCAL_SECOND, 0);
    calendar->set(UCAL_MILLISECOND, 0);
    return LocalDateTime(std::move(calendar));
  }

  /// @return 当天 23:59:59.999
  LocalDateTime endOfDay() const {
    if (!calendar_) return {nullptr};
    auto calendar = std::unique_ptr<icu::Calendar>(calendar_->clone());
    calendar->set(UCAL_HOUR_OF_DAY, 23);
    calendar->set(UCAL_MINUTE, 59);
    calendar->set(UCAL_SECOND, 59);
    calendar->set(UCAL_MILLISECOND, 999);
    return LocalDateTime(std::move(calendar));
  }

  /// @return 当月第一天 00:00:00.000
  LocalDateTime beginOfMonth() const {
    if (!calendar_) return {nullptr};
    auto calendar = std::unique_ptr<icu::Calendar>(calendar_->clone());
    calendar->set(UCAL_DATE, 1);
    calendar->set(UCAL_HOUR_OF_DAY, 0);
    calendar->set(UCAL_MINUTE, 0);
    calendar->set(UCAL_SECOND, 0);
    calendar->set(UCAL_MILLISECOND, 0);
    return LocalDateTime(std::move(calendar));
  }

  /// @return 当月最后一天 23:59:59.999
  LocalDateTime endOfMonth() const {
    if (!calendar_) return {nullptr};
    auto calendar = std::unique_ptr<icu::Calendar>(calendar_->clone());
    UErrorCode status = U_ZERO_ERROR;
    const int32_t lastDay = calendar->getActualMaximum(UCAL_DATE, status);
    calendar->set(UCAL_DATE, lastDay);
    calendar->set(UCAL_HOUR_OF_DAY, 23);
    calendar->set(UCAL_MINUTE, 59);
    calendar->set(UCAL_SECOND, 59);
    calendar->set(UCAL_MILLISECOND, 999);
    return LocalDateTime(std::move(calendar));
  }

  /// @return 当周第一天 00:00:00.000
  LocalDateTime beginOfWeek() const {
    if (!calendar_) return {nullptr};
    auto calendar = std::unique_ptr<icu::Calendar>(calendar_->clone());
    UErrorCode status = U_ZERO_ERROR;
    auto firstDayOfWeek = calendar->getFirstDayOfWeek(status);
    calendar->set(UCAL_DAY_OF_WEEK, firstDayOfWeek);
    calendar->set(UCAL_HOUR_OF_DAY, 0);
    calendar->set(UCAL_MINUTE, 0);
    calendar->set(UCAL_SECOND, 0);
    calendar->set(UCAL_MILLISECOND, 0);
    return LocalDateTime(std::move(calendar));
  }

  /// @return 当周最后一天 23:59:59.999
  LocalDateTime endOfWeek() const {
    if (!calendar_) return {nullptr};
    auto calendar = std::unique_ptr<icu::Calendar>(calendar_->clone());
    UErrorCode status = U_ZERO_ERROR;
    auto firstDayOfWeek = calendar->getFirstDayOfWeek(status);
    calendar->set(UCAL_DAY_OF_WEEK, firstDayOfWeek);
    calendar->add(UCAL_DATE, 6, status);
    calendar->set(UCAL_HOUR_OF_DAY, 23);
    calendar->set(UCAL_MINUTE, 59);
    calendar->set(UCAL_SECOND, 59);
    calendar->set(UCAL_MILLISECOND, 999);
    return LocalDateTime(std::move(calendar));
  }

  /// @return 当年1月1日 00:00:00.000
  LocalDateTime beginOfYear() const {
    if (!calendar_) return {nullptr};
    auto calendar = std::unique_ptr<icu::Calendar>(calendar_->clone());
    calendar->set(UCAL_MONTH, 0);
    calendar->set(UCAL_DATE, 1);
    calendar->set(UCAL_HOUR_OF_DAY, 0);
    calendar->set(UCAL_MINUTE, 0);
    calendar->set(UCAL_SECOND, 0);
    calendar->set(UCAL_MILLISECOND, 0);
    return LocalDateTime(std::move(calendar));
  }

  /// @return 当年12月31日 23:59:59.999
  LocalDateTime endOfYear() const {
    if (!calendar_) return {nullptr};
    auto calendar = std::unique_ptr<icu::Calendar>(calendar_->clone());
    calendar->set(UCAL_MONTH, 11);
    calendar->set(UCAL_DATE, 31);
    calendar->set(UCAL_HOUR_OF_DAY, 23);
    calendar->set(UCAL_MINUTE, 59);
    calendar->set(UCAL_SECOND, 59);
    calendar->set(UCAL_MILLISECOND, 999);
    return LocalDateTime(std::move(calendar));
  }

  /**
   * @brief 计算与另一个日期时间的差值
   * @param other 另一个日期时间对象
   * @param computeWeek 是否计算周数差值
   * @return Duration 对象，表示各字段的差值（this - other）
   */
  Duration diff(const LocalDateTime &other, const bool computeWeek = false) const {
    UErrorCode status = U_ZERO_ERROR;

    const UDate date = calendar_->getTime(status);
    const auto calendar = std::unique_ptr<icu::Calendar>(other.calendar_->clone());

    const int32_t years = calendar->fieldDifference(date, UCAL_YEAR, status);
    const int32_t months = calendar->fieldDifference(date, UCAL_MONTH, status);
    int32_t weeks = 0;
    if (computeWeek) {
      weeks = calendar->fieldDifference(date, UCAL_WEEK_OF_YEAR, status);
    }
    const int32_t days = calendar->fieldDifference(date, UCAL_DATE, status);
    const int32_t hours = calendar->fieldDifference(date, UCAL_HOUR_OF_DAY, status);
    const int32_t minutes = calendar->fieldDifference(date, UCAL_MINUTE, status);
    const int32_t seconds = calendar->fieldDifference(date, UCAL_SECOND, status);
    const int32_t millis = calendar->fieldDifference(date, UCAL_MILLISECOND, status);

    return U_FAILURE(status) ? Duration(0, 0, 0, 0, 0, 0, 0, 0) : Duration({years, months, weeks, days, hours, minutes, seconds, millis});
  }

  /**
   * @brief 格式化输出
   * @param fmt ICU 格式串，默认 "yyyy-MM-dd HH:mm:ss"
   * @note 按北京时间 Asia/Shanghai 格式化。失败时返回空字符串。
   */
  std::string format(const std::string &fmt = "yyyy-MM-dd HH:mm:ss") const {
    if (calendar_ == nullptr) {
      LOGGER_ERROR("日期格式化失败");
      return {};
    }

    UErrorCode status = U_ZERO_ERROR;

    const icu::UnicodeString pattern = icu::UnicodeString::fromUTF8(icu::StringPiece(fmt.data(), static_cast<int32_t>(fmt.size())));

    icu::SimpleDateFormat sdf(pattern, status);

    sdf.setTimeZone(calendar_->getTimeZone());

    const UDate date = calendar_->getTime(status);

    icu::UnicodeString out;
    sdf.format(date, out, status);
    if (U_FAILURE(status)) {
      LOGGER_ERROR("日期格式化失败");
      return {};
    }

    std::string result;
    out.toUTF8String(result);
    return result;
  }

  /// @brief 加运算符，当前时间加上一个时间间隔
  LocalDateTime operator+(const Duration &duration) const {
    return addDuration(duration);
  }

  /// @brief 减运算符，当前时间减去一个时间间隔
  LocalDateTime operator-(const Duration &duration) const {
    return addDuration(-duration);
  }

  /// @brief 减运算符，计算两个日期时间的差值
  Duration operator-(const LocalDateTime &other) const {
    return diff(other);
  }

  /**
   * @brief 格式化为相对时间的可读字符串（如"3天前"、"刚刚"）
   * @param longDesc 是否使用完整描述（组合多个时间单位），false 时仅展示最大单位
   * @param computeWeek 是否在差值计算中包含周数
   * @return 相对时间描述字符串
   */
  std::string pretty(const bool longDesc = false, const bool computeWeek = false) const {
    const auto now = LocalDateTime::now();
    const auto &[years,
      months,
      weeks,
      days,
      hours,
      minutes,
      seconds,
      millis] = this->diff(now, computeWeek);
    if (longDesc) {
      if (years == 0 && months == 0 && weeks == 0 && days == 0 && hours == 0 && minutes == 0) {
        if (seconds == 0) return "刚刚";
        if (seconds > 5) return std::format("{}秒后", seconds);
        if (seconds > 0 && seconds <= 5) return "片刻后";
        if (seconds >= -5 && seconds < 0) return "刚刚";
        if (seconds < -5) return std::format("{}秒前", seconds);
      }
      std::string result;
      if (years != 0) result += std::format("{}年", std::abs(years));
      if (months != 0) result += std::format("{}个月", std::abs(months));
      if (weeks != 0) result += std::format("{}周", std::abs(weeks));
      if (days != 0) result += std::format("{}天", std::abs(days));
      if (hours != 0) result += std::format("{}小时", std::abs(hours));
      if (minutes != 0) result += std::format("{}分钟", std::abs(minutes));
      if (seconds != 0) result += std::format("{}秒", std::abs(seconds));
      const bool positive = years > 0 || months > 0 || weeks > 0 || days > 0 || hours > 0 || minutes > 0 || seconds > 0;
      return result + (positive ? "后" : "前");
    }
    if (years > 0) return std::format("{}年后", years);
    if (years < 0) return std::format("{}年前", -years);

    if (months > 0) return std::format("{}个月后", months);
    if (months < 0) return std::format("{}个月前", -months);

    if (weeks > 0) return std::format("{}周后", weeks);
    if (weeks < 0) return std::format("{}周前", -weeks);

    if (days > 0) return std::format("{}天后", days);
    if (days < 0) return std::format("{}天前", -days);

    if (hours > 0) return std::format("{}小时后", hours);
    if (hours < 0) return std::format("{}小时前", -hours);

    if (minutes > 0) return std::format("{}分钟后", minutes);
    if (minutes < 0) return std::format("{}分钟前", -minutes);

    if (seconds > 5) return std::format("{}秒后", seconds);
    if (seconds > 0 && seconds <= 5) return "片刻后";
    if (seconds >= -5 && seconds < 0) return "刚刚";
    if (seconds < -5) return std::format("{}秒前", seconds);
    return "刚刚";
  }
};

} // namespace common

#endif // ALKAID_LOCAL_DATETIME_H

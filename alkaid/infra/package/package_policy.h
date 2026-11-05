/** @file package_policy.h 严格 JSON、Windows 资源路径与版本策略。 */
#ifndef ALKAID_PACKAGE_POLICY_H
#define ALKAID_PACKAGE_POLICY_H
#include "package_types.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <set>

namespace alkaid::PackagePolicy {
inline constexpr std::size_t MAX_ARCHIVE = 32 * 1024 * 1024;
inline constexpr std::size_t MAX_CONTENT = 32 * 1024 * 1024;
inline constexpr std::size_t MAX_FILE = 8 * 1024 * 1024;
inline constexpr std::size_t MAX_METADATA = 256 * 1024;
inline std::string lower(std::string value) {
  for (auto &c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return value;
}
inline bool path(const std::string &value) {
  if (value.empty() || value.size() > 240 || value.front() == '/' || value.back() == '/') return false;
  for (const auto c : value) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/')) return false;
  std::size_t offset = 0;
  while (offset < value.size()) {
    const auto end = value.find('/', offset);
    const auto part = lower(value.substr(offset, end == std::string::npos ? value.size() - offset : end - offset));
    if (part.empty() || part.size() > 80 || part == "." || part == ".." || part.back() == '.') return false;
    const auto base = part.substr(0, part.find('.'));
    if (base == "con" || base == "prn" || base == "aux" || base == "nul" ||
        (base.size() == 4 && (base.starts_with("com") || base.starts_with("lpt")) && base[3] >= '1' && base[3] <= '9')) return false;
    if (end == std::string::npos) break;
    offset = end + 1;
  }
  return true;
}
inline bool appId(const std::string &value) {
  if (value.size() < 3 || value.size() > 100 || value.find('.') == std::string::npos || value.starts_with("org.alkaid.")) return false;
  bool beginning = true;
  for (const auto c : value) {
    if (c == '.') { if (beginning) return false; beginning = true; continue; }
    if (!((c >= 'a' && c <= 'z') || (!beginning && ((c >= '0' && c <= '9') || c == '-')))) return false;
    beginning = false;
  }
  return !beginning;
}
inline bool pluginMember(std::string_view name) {
  return !name.empty() && name.size() <= 64 && name.front() >= 'a' && name.front() <= 'z' &&
    std::ranges::all_of(name, [](char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    });
}
inline bool appIcon(std::string_view value) {
  // 读取 WebP 首块的画布尺寸，避免微小压缩图造成异常大的解码分配。
  if (value.size() < 40 || value.size() > 16384 || value.size() % 4 ||
      !value.starts_with("UklGR") || value.substr(11, 5) != "XRUJQ") return false;
  const auto padding = value.ends_with("==") ? 2 : value.ends_with("=") ? 1 : 0;
  constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const auto digit = [&](char c) { return alphabet.find(c); };
  if (!std::ranges::all_of(value.substr(0, value.size() - padding), [&](char c) { return digit(c) != alphabet.npos; }))
    return false;
  std::array<unsigned char, 30> header{};
  for (std::size_t i = 0; i < 40; i += 4) {
    if (i + 4 > value.size() - padding) return false;
    const auto bits = (digit(value[i]) << 18) | (digit(value[i + 1]) << 12) | (digit(value[i + 2]) << 6) | digit(value[i + 3]);
    const auto offset = i / 4 * 3;
    header[offset] = static_cast<unsigned char>(bits >> 16);
    header[offset + 1] = static_cast<unsigned char>(bits >> 8);
    header[offset + 2] = static_cast<unsigned char>(bits);
  }
  const std::string_view chunk(reinterpret_cast<const char *>(header.data() + 12), 4);
  if (chunk == "VP8X") {
    if (header[20] & 2) return false; // 图标不接受动画。
    return header[24] == 95 && header[25] == 0 && header[26] == 0 &&
      header[27] == 95 && header[28] == 0 && header[29] == 0;
  }
  if (chunk == "VP8L")
    return header[20] == 0x2f && (header[21] | ((header[22] & 63) << 8)) == 95 &&
      ((header[22] >> 6) | (header[23] << 2) | ((header[24] & 15) << 10)) == 95;
  return chunk == "VP8 " && header[23] == 0x9d && header[24] == 1 && header[25] == 0x2a &&
    (header[26] | ((header[27] & 63) << 8)) == 96 && (header[28] | ((header[29] & 63) << 8)) == 96;
}
inline std::optional<std::array<int, 3>> version(const std::string &value) {
  std::array<int, 3> result{};
  std::size_t offset = 0;
  for (int i = 0; i < 3; ++i) {
    const auto end = value.find('.', offset);
    const auto part = value.substr(offset, end == std::string::npos ? value.size() - offset : end - offset);
    if (part.empty() || part.size() > 6 || (part.size() > 1 && part[0] == '0')) return std::nullopt;
    for (const auto c : part) if (c < '0' || c > '9') return std::nullopt;
    const auto parsed = std::from_chars(part.data(), part.data() + part.size(), result[i]);
    if (parsed.ec != std::errc{} || (i < 2 && end == std::string::npos) || (i == 2 && end != std::string::npos)) return std::nullopt;
    offset = end + 1;
  }
  return result;
}
inline bool keys(const Json &value, std::initializer_list<const char *> names) {
  if (!value.is_object() || value.size() != names.size()) return false;
  for (const auto name : names) if (!value.contains(name)) return false;
  return true;
}
/** SAX 首遍拒绝重复键与过深嵌套，再生成 DOM；不让禁用异常的 JSON 库处理未验证类型。 */
class StrictJson : public nlohmann::json_sax<Json> {
public:
  bool null() override { return true; }
  bool boolean(bool) override { return true; }
  bool number_integer(number_integer_t) override { return true; }
  bool number_unsigned(number_unsigned_t) override { return true; }
  bool number_float(number_float_t, const string_t &) override { return true; }
  bool string(string_t &) override { return true; }
  bool binary(binary_t &) override { return false; }
  bool start_object(std::size_t) override { stack_.emplace_back(); return stack_.size() <= 16; }
  bool key(string_t &key) override { return !stack_.empty() && stack_.back().insert(key).second; }
  bool end_object() override { stack_.pop_back(); return true; }
  bool start_array(std::size_t) override { stack_.emplace_back(); return stack_.size() <= 16; }
  bool end_array() override { stack_.pop_back(); return true; }
  bool parse_error(std::size_t, const std::string &, const nlohmann::detail::exception &) override { return false; }
private:
  std::vector<std::set<std::string>> stack_;
};
inline Json json(const std::string &text) {
  StrictJson checker;
  if (text.size() > MAX_METADATA || !Json::sax_parse(text, &checker)) return Json();
  return Json::parse(text, nullptr, false);
}
inline bool webAsset(const std::string &name) {
  const auto dot = name.rfind('.');
  if (dot == std::string::npos) return false;
  static const std::set<std::string> allowed{"html", "css", "js", "mjs", "json", "txt", "svg", "png", "jpg", "jpeg", "gif", "webp", "ico", "woff", "woff2", "map"};
  return allowed.contains(lower(name.substr(dot + 1)));
}
} // namespace alkaid::PackagePolicy
#endif // ALKAID_PACKAGE_POLICY_H

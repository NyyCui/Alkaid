/** @file package_verifier.cpp 先解析有界容器，再验签和校验所有运行资源。 */
#include "package_verifier.h"
#include "package_policy.h"
#include "crypto.h"
#include "capability_policy.h"
#include "network_policy.h"
#include <miniz.h>
#include <fstream>

namespace alkaid {
namespace _ {
struct ZipReader {
  mz_zip_archive zip{};
  ~ZipReader() { mz_zip_reader_end(&zip); }
};
RuntimeError invalid(const std::string &message) { return {"PACKAGE_INVALID", message}; }
bool displayName(const Json &value) {
  if (!value.is_string()) return false;
  const auto &name = value.get_ref<const std::string &>();
  return !name.empty() && name.size() <= 128 &&
    std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool validWindow(const Json &window) {
  if (!PackagePolicy::keys(window, {"width", "height", "minWidth", "minHeight", "resizable"}) ||
      !window["resizable"].is_boolean()) return false;
  for (const auto key : {"width", "height", "minWidth", "minHeight"})
    if (!window[key].is_number_unsigned() || window[key] < 1 || window[key] > 8192) return false;
  return window["minWidth"] <= window["width"] && window["minHeight"] <= window["height"];
}
bool readDependencies(const Json &dependencies, std::set<std::string> &plugins) {
  if (!dependencies.is_array() || dependencies.size() > CapabilityPolicy::MAX_PLUGIN_DEPENDENCIES) return false;
  for (const auto &dependency : dependencies) {
    if (!PackagePolicy::keys(dependency, {"pluginId", "minVersion", "maxVersionExclusive", "required"}) ||
        !dependency["pluginId"].is_string() || !dependency["minVersion"].is_string() ||
        !dependency["maxVersionExclusive"].is_string() || !dependency["required"].is_boolean()) return false;
    const auto id = dependency["pluginId"].get<std::string>();
    const auto min = PackagePolicy::version(dependency["minVersion"].get<std::string>());
    const auto max = PackagePolicy::version(dependency["maxVersionExclusive"].get<std::string>());
    if (!PackagePolicy::appId(id) || !plugins.insert(id).second || !min || !max || *min >= *max) return false;
  }
  return true;
}
bool pluginNames(const Json &names) {
  if (!names.is_array() || names.size() > 128) return false;
  std::set<std::string> seen;
  for (const auto &value : names) {
    if (!value.is_string()) return false;
    const auto &name = value.get_ref<const std::string &>();
    if (!PackagePolicy::pluginMember(name) || !seen.insert(name).second) return false;
  }
  return true;
}
bool x64Executable(std::string_view bytes) {
  if (bytes.size() < 64 || !bytes.starts_with("MZ")) return false;
  const auto byte = [&](std::size_t offset) { return static_cast<unsigned char>(bytes[offset]); };
  std::size_t pe = 0;
  for (unsigned i = 0; i < 4; ++i) pe |= std::size_t(byte(60 + i)) << (8 * i);
  return pe >= 64 && pe <= bytes.size() - 26 &&
    bytes.substr(pe, 6) == std::string_view("PE\0\0\x64\x86", 6) &&
    (byte(pe + 22) & 2) != 0 && (byte(pe + 23) & 0x20) == 0 &&
    byte(pe + 24) == 0x0b && byte(pe + 25) == 2;
}
}
RuntimeResult<std::shared_ptr<const VerifiedPackage>> PackageVerifier::read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input || input.tellg() <= 0 || input.tellg() > static_cast<std::streamoff>(PackagePolicy::MAX_ARCHIVE))
    return RuntimeError{"PACKAGE_IO", "包无法读取或超过 32 MiB 限制"};
  const auto size = static_cast<std::size_t>(input.tellg());
  std::string bytes(size, '\0');
  input.seekg(0);
  if (!input.read(bytes.data(), static_cast<std::streamsize>(size))) return RuntimeError{"PACKAGE_IO", "读取应用包失败"};
  return verify(std::move(bytes));
}
RuntimeResult<std::shared_ptr<const VerifiedPackage>> PackageVerifier::verify(std::string bytes) {
  if (bytes.empty() || bytes.size() > PackagePolicy::MAX_ARCHIVE) return _::invalid("包大小不合法");
  _::ZipReader reader;
  if (!mz_zip_reader_init_mem(&reader.zip, bytes.data(), bytes.size(), 0)) return _::invalid("不是有效的 ZIP 应用包");
  const auto count = mz_zip_reader_get_num_files(&reader.zip);
  if (count < 4 || count > 515) return _::invalid("包内文件数不合法，资源最多 512 个");
  std::map<std::string, std::string> files;
  std::set<std::string> names;
  std::size_t total = 0;
  for (mz_uint index = 0; index < count; ++index) {
    mz_zip_archive_file_stat stat{};
    if (!mz_zip_reader_file_stat(&reader.zip, index, &stat)) return _::invalid("ZIP 条目损坏");
    const auto nameSize = mz_zip_reader_get_filename(&reader.zip, index, nullptr, 0);
    const std::string name = stat.m_filename;
    const auto mode = (stat.m_external_attr >> 16) & 0170000;
    if (nameSize != name.size() + 1 || !PackagePolicy::path(name) || !names.insert(PackagePolicy::lower(name)).second ||
        stat.m_is_directory || stat.m_is_encrypted || !stat.m_is_supported || (mode && mode != 0100000) ||
        (stat.m_external_attr & FILE_ATTRIBUTE_REPARSE_POINT) || (stat.m_method != 0 && stat.m_method != 8))
      return _::invalid("包内路径冲突、不安全条目、链接或不支持的压缩方式");
    const bool meta = name == "manifest.json" || name == "integrity.json" || name == "signature.json";
    if (!meta && (!name.starts_with("assets/") || (!PackagePolicy::webAsset(name) && name != "assets/worker.exe")))
      return _::invalid("包包含未允许的文件类型或顶层文件");
    if (stat.m_uncomp_size > (meta ? PackagePolicy::MAX_METADATA : PackagePolicy::MAX_FILE) ||
        stat.m_uncomp_size > 200 * std::max<mz_uint64>(1, stat.m_comp_size) ||
        stat.m_uncomp_size > PackagePolicy::MAX_CONTENT - total) return _::invalid("包超过文件大小、总大小或压缩比限制");
    total += static_cast<std::size_t>(stat.m_uncomp_size);
    std::string content(static_cast<std::size_t>(stat.m_uncomp_size), '\0');
    if (!mz_zip_reader_extract_to_mem(&reader.zip, index, content.data(), content.size(), 0)) return _::invalid("ZIP 内容或 CRC 校验失败");
    files.emplace(name, std::move(content));
  }
  for (const auto name : {"manifest.json", "integrity.json", "signature.json"}) if (!files.contains(name)) return _::invalid("缺少包元数据");
  const auto manifest = PackagePolicy::json(files.at("manifest.json"));
  const auto integrity = PackagePolicy::json(files.at("integrity.json"));
  const auto signature = PackagePolicy::json(files.at("signature.json"));
  if (!manifest.is_object() || !manifest.value("manifestVersion", Json()).is_number_unsigned() || manifest["manifestVersion"] != 3 ||
      !_::displayName(manifest.value("name", Json())) ||
      !PackagePolicy::keys(manifest.value("publisher", Json()), {"name"}) || !_::displayName(manifest["publisher"]["name"]) ||
      !manifest.value("version", Json()).is_string() || !PackagePolicy::version(manifest["version"].get<std::string>()) ||
      !manifest.value("publisherKeyId", Json()).is_string())
    return _::invalid("清单版本、发布者或包版本不合法");
  auto package = std::make_shared<VerifiedPackage>();
  const bool plugin = manifest.value("type", Json()) == "plugin";
  if (plugin) {
    if (!PackagePolicy::keys(manifest, {"manifestVersion", "type", "pluginId", "name", "version", "publisher", "publisherKeyId",
          "protocolMajor", "architecture", "entry", "capabilities", "events"}) ||
        !manifest["pluginId"].is_string() || !PackagePolicy::appId(manifest["pluginId"].get<std::string>()) ||
        !manifest["protocolMajor"].is_number_unsigned() || manifest["protocolMajor"] != 1 ||
        manifest["architecture"] != "win-x64" || manifest["entry"] != "worker.exe" ||
        !_::pluginNames(manifest["capabilities"]) || !_::pluginNames(manifest["events"]))
      return _::invalid("插件清单、入口或协议不合法");
    package->pluginId = manifest["pluginId"].get<std::string>();
    package->entry = "worker.exe";
    package->pluginCapabilities = manifest["capabilities"];
    package->pluginEvents = manifest["events"];
  } else {
    if (files.contains("assets/worker.exe")) return _::invalid("应用包不能包含插件可执行文件");
    auto baseManifest=manifest; baseManifest.erase("networkEndpoints");
    if (manifest.contains("icon")) {
      const auto &icon = manifest["icon"];
      if (!icon.is_string() || !PackagePolicy::appIcon(icon.get_ref<const std::string &>())) return _::invalid("应用图标须为有界 WebP Base64");
      package->icon = icon.get<std::string>();
      baseManifest.erase("icon");
    }
    std::set<std::string> plugins;
    if (!PackagePolicy::keys(baseManifest, {"manifestVersion", "type", "appId", "name", "version", "publisher", "publisherKeyId",
          "runtime", "entry", "routing", "instanceMode", "window", "permissions", "dependencies"}) ||
        manifest["type"] != "app" || !manifest["appId"].is_string() || !manifest["entry"].is_string() ||
        manifest["routing"] != "hash" || !_::validWindow(manifest["window"]) ||
        !_::readDependencies(manifest["dependencies"], plugins) ||
        manifest["instanceMode"] != "single" || !CapabilityPolicy::valid(manifest["permissions"], false, plugins) ||
        !PackagePolicy::keys(manifest["runtime"], {"apiMajor", "minVersion"}) || !manifest["runtime"]["apiMajor"].is_number_unsigned() || manifest["runtime"]["apiMajor"] != 1 ||
        !manifest["runtime"]["minVersion"].is_string()) return _::invalid("清单字段、入口、协议或权限不受当前运行时支持");
    package->dependencies = manifest["dependencies"];
    package->entry = manifest["entry"].get<std::string>();
    if (!PackagePolicy::path(package->entry) || !package->entry.ends_with(".html"))
      return _::invalid("入口必须为包内 HTML 资源");
    const auto &window = manifest["window"];
    package->window = {window["width"].get<int>(), window["height"].get<int>(),
      window["minWidth"].get<int>(), window["minHeight"].get<int>(), window["resizable"].get<bool>()};
    package->permissions = manifest["permissions"].get<std::set<std::string>>();
    if(manifest.contains("networkEndpoints")) {
      if(!package->permissions.contains("net.request") || !NetworkPolicy::endpoints(manifest["networkEndpoints"])) return _::invalid("网络端点清单或网络权限不合法");
      package->networkEndpoints=manifest["networkEndpoints"];
      for(auto &endpoint:package->networkEndpoints) {
        const auto scope=NetworkPolicy::scope(endpoint); package->permissions.insert(scope); endpoint["scope"]=scope;
      }
    } else if(package->permissions.contains("net.request")) return _::invalid("网络权限必须声明具体端点");
    package->appId = manifest["appId"].get<std::string>();
    const auto minVersion = PackagePolicy::version(manifest["runtime"]["minVersion"].get<std::string>());
    if (!PackagePolicy::appId(package->appId) || !minVersion || *minVersion > std::array<int, 3>{2, 0, 0})
      return _::invalid("应用标识或兼容性不合法");
  }
  package->publisherName = manifest["publisher"]["name"].get<std::string>();
  package->name = manifest["name"].get<std::string>();
  package->version = manifest["version"].get<std::string>();
  package->publisherKeyId = manifest["publisherKeyId"].get<std::string>();
  if (!PackagePolicy::keys(signature, {"algorithm", "publicKey", "signature"}) || signature["algorithm"] != "ECDSA-P256-SHA256" ||
      !signature["publicKey"].is_string() || !signature["signature"].is_string()) return _::invalid("签名格式不合法");
  package->publicKey = signature["publicKey"].get<std::string>();
  const auto keyBytes = PackageCrypto::unhex(package->publicKey);
  if (keyBytes.size() != 65 || package->publisherKeyId != "sha256:" + PackageCrypto::sha256(std::string(keyBytes.begin(), keyBytes.end())) ||
      !PackageCrypto::verify(package->publicKey, signature["signature"].get<std::string>(),
        PackageCrypto::signatureInput(files.at("manifest.json"), files.at("integrity.json"))))
    return RuntimeError{"SIGNATURE_INVALID", "发布者指纹或包签名校验失败"};
  if (!PackagePolicy::keys(integrity, {"files"}) || !integrity["files"].is_array() || integrity["files"].size() != count - 3)
    return _::invalid("完整性清单与包内资源数量不一致");
  std::set<std::string> listed;
  for (const auto &entry : integrity["files"]) {
    if (!PackagePolicy::keys(entry, {"path", "size", "sha256"}) || !entry["path"].is_string() ||
        !entry["size"].is_number_unsigned() || !entry["sha256"].is_string()) return _::invalid("完整性条目不合法");
    const auto path = entry["path"].get<std::string>();
    if (!path.starts_with("assets/") || !files.contains(path) || !listed.insert(path).second ||
        entry["size"].get<uint64_t>() != files.at(path).size() || entry["sha256"].get<std::string>() != PackageCrypto::sha256(files.at(path)))
      return _::invalid("资源缺失、重复或 SHA-256 校验失败");
    package->assets.emplace(path.substr(7), std::move(files.at(path)));
  }
  if (!package->assets.contains(package->entry)) return _::invalid("包入口不存在");
  if (plugin && !_::x64Executable(package->assets.at(package->entry))) return _::invalid("插件入口必须是 Windows x64 EXE");
  package->digest = PackageCrypto::sha256(bytes);
  if (package->digest.empty()) return _::invalid("无法计算包摘要");
  package->archive = std::move(bytes);
  return std::shared_ptr<const VerifiedPackage>(std::move(package));
}
} // namespace alkaid

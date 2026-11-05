/** @file main.cpp 通过宿主 IPC 使用 Windows 串口，句柄仅归当前文档会话。 */
#include "plugin_wire.h"
#include <cwchar>

namespace {
using alkaid::Json;
using alkaid::RuntimeError;
namespace Policy = alkaid::PackagePolicy;
namespace Wire = alkaid::PluginWire;
constexpr auto PLUGIN_ID = "com.alkaid.serial";
const Json METHODS = {"list","open","read","write","close"};
const Json EVENTS = {"disconnected"};
constexpr DWORD MAX_BYTES = 4096;

bool portName(std::wstring_view name) {
  return name.starts_with(L"COM") && name.size() >= 4 && name.size() <= 7 && name[3] != L'0' &&
    std::ranges::all_of(name.substr(3), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; });
}
RuntimeError invalid() { return {"INVALID_REQUEST", "串口参数不合法"}; }
RuntimeError winError(const char *code, const char *message, DWORD error) {
  return {code, std::string(message) + "（Win32 " + std::to_string(error) + "）"};
}
bool inRange(const Json &value, unsigned low, unsigned high) {
  return value.is_number_unsigned() && value >= low && value <= high;
}

class Serial {
public:
  ~Serial() { reset(); }
  void release(const std::string &owner) { if (owner_ == owner) reset(); }
  alkaid::RuntimeResult<Json> call(const std::string &method, const Json &params, const std::string &owner, HANDLE pipe) {
    if (method == "list") return list(params);
    if (method == "open") return open(params, owner);
    if (!params.is_object() || !params.contains("handle") || !params["handle"].is_string()) return invalid();
    if (file_ == INVALID_HANDLE_VALUE || owner_ != owner || params["handle"] != handle_)
      return RuntimeError{"HANDLE_EXPIRED", "串口句柄已失效"};
    if (method == "close" && Policy::keys(params, {"handle"})) { reset(); return Json(nullptr); }
    if (method == "read" && Policy::keys(params, {"handle","maxBytes","waitMs"}) &&
        inRange(params["maxBytes"], 1, MAX_BYTES) && inRange(params["waitMs"], 1, 250)) {
      COMMTIMEOUTS timeouts{MAXDWORD, MAXDWORD, params["waitMs"].get<DWORD>(), 0, 250};
      if (!SetCommTimeouts(file_, &timeouts)) return ioFailure(pipe, GetLastError());
      std::vector<uint8_t> bytes(params["maxBytes"].get<DWORD>());
      DWORD count = 0;
      if (!ReadFile(file_, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr)) return ioFailure(pipe, GetLastError());
      bytes.resize(count);
      return Json{{"bytes",bytes}};
    }
    if (method == "write" && Policy::keys(params, {"handle","bytes"}) && params["bytes"].is_array() &&
        !params["bytes"].empty() && params["bytes"].size() <= MAX_BYTES) {
      std::vector<uint8_t> bytes;
      for (const auto &value : params["bytes"]) {
        if (!inRange(value, 0, 255)) return invalid();
        bytes.push_back(value.get<uint8_t>());
      }
      DWORD written = 0;
      if (!WriteFile(file_, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)) return ioFailure(pipe, GetLastError());
      return Json{{"written",written},{"complete",written == bytes.size()}};
    }
    return invalid();
  }
private:
  alkaid::RuntimeResult<Json> list(const Json &params) const {
    if (!params.is_object() || !params.empty()) return invalid();
    std::vector<wchar_t> names(65536);
    const auto size = QueryDosDeviceW(nullptr, names.data(), static_cast<DWORD>(names.size()));
    if (!size) return winError("SERIAL_ENUM_FAILED", "无法枚举串口", GetLastError());
    auto ports = Json::array();
    for (const auto *name = names.data(); *name; name += std::wcslen(name) + 1) {
      const std::wstring_view value(name);
      if (!portName(value)) continue;
      std::string ascii;
      for (const auto ch : value) ascii.push_back(static_cast<char>(ch));
      ports.push_back(std::move(ascii));
    }
    return Json{{"ports",ports}};
  }
  alkaid::RuntimeResult<Json> open(const Json &params, const std::string &owner) {
    if (!Policy::keys(params, {"port","baudRate"}) || !params["port"].is_string() || !inRange(params["baudRate"], 1200, 921600))
      return invalid();
    const auto &name = params["port"].get_ref<const std::string &>();
    if (name.size() > 7 || !portName(std::wstring(name.begin(), name.end()))) return invalid();
    if (file_ != INVALID_HANDLE_VALUE) return RuntimeError{"SERIAL_BUSY", "当前会话已有打开的串口"};
    const auto device = L"\\\\.\\" + std::wstring(name.begin(), name.end());
    const auto opened = CreateFileW(device.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (opened == INVALID_HANDLE_VALUE) {
      const auto code = GetLastError();
      if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return winError("SERIAL_NOT_FOUND", "串口不存在", code);
      if (code == ERROR_SHARING_VIOLATION) return winError("SERIAL_BUSY", "串口已被占用", code);
      if (code == ERROR_ACCESS_DENIED) return winError("SERIAL_ACCESS_DENIED", "串口拒绝访问，可能被占用或受驱动权限限制", code);
      return winError("SERIAL_OPEN_FAILED", "无法打开串口", code);
    }
    DCB settings{};
    settings.DCBlength = sizeof(settings);
    if (!GetCommState(opened, &settings)) {
      const auto code = GetLastError();
      CloseHandle(opened);
      return winError("SERIAL_CONFIG_FAILED", "无法读取串口配置", code);
    }
    settings.BaudRate = params["baudRate"].get<DWORD>();
    settings.ByteSize = 8; settings.Parity = NOPARITY; settings.StopBits = ONESTOPBIT;
    settings.fBinary = TRUE; settings.fParity = FALSE;
    settings.fOutxCtsFlow = FALSE; settings.fOutxDsrFlow = FALSE; settings.fDsrSensitivity = FALSE;
    settings.fOutX = FALSE; settings.fInX = FALSE; settings.fAbortOnError = FALSE;
    settings.fNull = FALSE; settings.fErrorChar = FALSE;
    settings.fDtrControl = DTR_CONTROL_DISABLE; settings.fRtsControl = RTS_CONTROL_DISABLE;
    COMMTIMEOUTS timeouts{MAXDWORD, MAXDWORD, 100, 0, 250};
    if (!SetCommState(opened, &settings) || !SetCommTimeouts(opened, &timeouts)) {
      const auto code = GetLastError();
      CloseHandle(opened);
      return winError("SERIAL_CONFIG_FAILED", "无法应用 8N1 串口配置", code);
    }
    file_ = opened;
    owner_ = owner;
    port_ = name;
    handle_ = "serial-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()) +
      "-" + std::to_string(++nextHandle_);
    return Json{{"handle",handle_},{"port",port_},{"baudRate",settings.BaudRate}};
  }
  alkaid::RuntimeResult<Json> ioFailure(HANDLE pipe, DWORD error) {
    Wire::write(pipe, {{"type","event"},{"owner",owner_},{"event","disconnected"},{"payload",{{"port",port_},{"win32",error}}}});
    reset();
    return winError("SERIAL_IO_FAILED", "串口读写失败，句柄已释放", error);
  }
  void reset() {
    if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
    file_ = INVALID_HANDLE_VALUE;
    owner_.clear(); port_.clear(); handle_.clear();
  }
  HANDLE file_ = INVALID_HANDLE_VALUE;
  uint64_t nextHandle_ = 0;
  std::string owner_, port_, handle_;
};
}

int wmain(int argc, wchar_t **argv) {
  if (argc != 2 || !std::wstring_view(argv[1]).starts_with(L"\\\\.\\pipe\\alkaid-plugin-")) return 2;
  const auto pipe = CreateFileW(argv[1], GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) return 3;
  const auto hello = Wire::read(pipe);
  if (!Policy::keys(hello, {"type","protocolMajor","pluginId","version"}) || hello["type"] != "hello" ||
      hello["protocolMajor"] != 1 || hello["pluginId"] != PLUGIN_ID || !hello["version"].is_string() ||
      !Wire::write(pipe, {{"type","ready"},{"protocolMajor",1},{"pluginId",PLUGIN_ID},{"version",hello["version"]},
                          {"methods",METHODS},{"events",EVENTS}})) { CloseHandle(pipe); return 4; }
  Serial serial;
  while (true) {
    const auto frame = Wire::read(pipe);
    if (!frame.is_object()) break;
    const auto type = frame.value("type", Json());
    if (type == "shutdown") break;
    if (type == "releaseOwner" && Policy::keys(frame, {"type","owner"}) && frame["owner"].is_string()) {
      serial.release(frame["owner"].get<std::string>());
      continue;
    }
    // 已结束的调用可能与取消交错，宿主决定是否保留此实例。
    if (type == "cancel" && Policy::keys(frame, {"type","id"})) continue;
    if (type != "call" || !Policy::keys(frame, {"type","id","method","params","owner","timeoutMs"}) ||
        !frame["id"].is_string() || !frame["method"].is_string() || !frame["owner"].is_string()) break;
    const auto result = serial.call(frame["method"].get<std::string>(), frame["params"], frame["owner"].get<std::string>(), pipe);
    Json response{{"type","result"},{"id",frame["id"]}};
    if (const auto error = std::get_if<RuntimeError>(&result)) response["error"] = {{"code",error->code},{"message",error->message}};
    else response["value"] = std::get<Json>(result);
    if (!Wire::write(pipe, response)) break;
  }
  CloseHandle(pipe);
  return 0;
}

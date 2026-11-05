/** @file local_publisher.h 本地发布者使用 Windows 用户级 DPAPI 保存私钥。 */
#ifndef ALKAID_LOCAL_PUBLISHER_H
#define ALKAID_LOCAL_PUBLISHER_H
#include "safe_files.h"
#include "crypto.h"
#include "include/cef_parser.h"
#include <dpapi.h>
#include <fstream>

namespace alkaid::LocalPublisher {
inline auto path(const std::filesystem::path &root) { return root / "config" / "publisher.json"; }
inline constexpr unsigned char PREFIX[] = {0x30,0x77,0x02,0x01,0x01,0x04,0x20};
inline constexpr unsigned char PARAMS[] = {0xa0,0x0a,0x06,0x08,0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07,0xa1,0x44,0x03,0x42,0x00,0x04};

inline Json fileData(const std::filesystem::path &file) {
  std::error_code error;
  if (!PackageFiles::safe(file) || std::filesystem::file_size(file, error) > 8192 || error) return nullptr;
  std::ifstream stream(file, std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(stream)), {});
  return Json::parse(bytes, nullptr, false);
}

inline RuntimeResult<Json> read(const std::filesystem::path &root) {
  const auto file = path(root);
  std::error_code error;
  if (PackageFiles::safe(file) && !std::filesystem::exists(file, error) && !error) return Json(nullptr);
  {
    const auto value = fileData(file);
    if (value.is_object() && value.contains("name") && value["name"].is_string() &&
        value.contains("keyId") && value["keyId"].is_string() &&
        value.contains("protectedKey") && value["protectedKey"].is_string())
      return Json{{"name", value["name"]}, {"keyId", value["keyId"]}};
  }
  return RuntimeError{"PUBLISHER_READ_FAILED", "无法读取本地发布者，请检查配置文件"};
}

inline RuntimeResult<Json> save(const std::filesystem::path &root, const std::string &name, std::string &der) {
  if (der.size() != 121 || memcmp(der.data(), PREFIX, sizeof(PREFIX)) != 0 ||
      memcmp(der.data() + sizeof(PREFIX) + 32, PARAMS, sizeof(PARAMS)) != 0)
    return RuntimeError{"PUBLISHER_KEY_FAILED", "备份不包含有效的 P-256 签名密钥"};
  const auto keyId = "sha256:" + PackageCrypto::sha256(der.substr(56));
  DATA_BLOB input{static_cast<DWORD>(der.size()), reinterpret_cast<BYTE *>(der.data())}, encrypted{};
  if (!CryptProtectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &encrypted))
    return RuntimeError{"PUBLISHER_KEY_FAILED", "无法保护本地签名密钥"};
  const auto encoded = CefBase64Encode(encrypted.pbData, encrypted.cbData).ToString();
  LocalFree(encrypted.pbData);
  const auto file = path(root);
  auto temp = file;
  temp += L".new";
  if (!PackageFiles::directory(file.parent_path()) || !PackageFiles::safe(temp))
    return RuntimeError{"PUBLISHER_WRITE_FAILED", "无法创建本地发布者配置"};
  DeleteFileW(temp.c_str());
  const auto bytes = Json{{"name", name}, {"keyId", keyId}, {"protectedKey", encoded}}.dump();
  if (!PackageFiles::write(temp, bytes) || !MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temp.c_str());
    return RuntimeError{"PUBLISHER_WRITE_FAILED", "无法保存本地发布者配置"};
  }
  return Json{{"name", name}, {"keyId", keyId}};
}

inline RuntimeResult<Json> create(const std::filesystem::path &root, const std::string &name) {
  const auto current = read(root);
  if (const auto error = std::get_if<RuntimeError>(&current)) return *error;
  if (!std::get<Json>(current).is_null())
    return RuntimeError{"PUBLISHER_EXISTS", "此用户已创建本地发布者"};
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_KEY_HANDLE key = nullptr;
  std::array<unsigned char, sizeof(BCRYPT_ECCKEY_BLOB) + 96> blob{};
  ULONG written = 0;
  const bool generated = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) >= 0 &&
    BCryptGenerateKeyPair(algorithm, &key, 256, 0) >= 0 && BCryptFinalizeKeyPair(key, 0) >= 0 &&
    BCryptExportKey(key, nullptr, BCRYPT_ECCPRIVATE_BLOB, blob.data(), static_cast<ULONG>(blob.size()), &written, 0) >= 0 &&
    written == blob.size();
  if (key) BCryptDestroyKey(key);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  if (!generated) return RuntimeError{"PUBLISHER_KEY_FAILED", "无法生成本地签名密钥"};

  // SEC1 ECPrivateKey 包含 P-256 私钥及未压缩公钥。
  const auto coordinates = reinterpret_cast<const char *>(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB));
  std::string der(reinterpret_cast<const char *>(PREFIX), sizeof(PREFIX));
  der.append(coordinates + 64, 32).append(reinterpret_cast<const char *>(PARAMS), sizeof(PARAMS)).append(coordinates, 64);
  SecureZeroMemory(blob.data(), blob.size());
  const auto result = save(root, name, der);
  SecureZeroMemory(der.data(), der.size());
  return result;
}
}
#endif

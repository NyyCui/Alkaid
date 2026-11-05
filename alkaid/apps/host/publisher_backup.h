/** @file publisher_backup.h 使用口令加密可迁移的发布者备份。 */
#ifndef ALKAID_PUBLISHER_BACKUP_H
#define ALKAID_PUBLISHER_BACKUP_H
#include "local_publisher.h"
#include <algorithm>

namespace alkaid::PublisherBackup {
inline bool random(std::span<unsigned char> bytes) {
  return BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
}

inline bool transform(bool restore, std::string &password, std::vector<unsigned char> &salt,
    std::vector<unsigned char> &nonce, std::vector<unsigned char> &tag, std::string &input, std::string &output) {
  BCRYPT_ALG_HANDLE hash = nullptr, aes = nullptr;
  BCRYPT_KEY_HANDLE key = nullptr;
  std::array<unsigned char, 32> material{};
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;
  BCRYPT_INIT_AUTH_MODE_INFO(auth);
  auth.pbNonce = nonce.data(); auth.cbNonce = static_cast<ULONG>(nonce.size());
  auth.pbTag = tag.data(); auth.cbTag = static_cast<ULONG>(tag.size());
  ULONG written = 0;
  output.resize(input.size());
  const auto in = reinterpret_cast<PUCHAR>(input.data()), out = reinterpret_cast<PUCHAR>(output.data());
  const auto size = static_cast<ULONG>(input.size());
  const bool ready = BCryptOpenAlgorithmProvider(&hash, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) >= 0 &&
    BCryptDeriveKeyPBKDF2(hash, reinterpret_cast<PUCHAR>(password.data()), static_cast<ULONG>(password.size()),
      salt.data(), static_cast<ULONG>(salt.size()), 600000, material.data(), static_cast<ULONG>(material.size()), 0) >= 0 &&
    BCryptOpenAlgorithmProvider(&aes, BCRYPT_AES_ALGORITHM, nullptr, 0) >= 0 &&
    BCryptSetProperty(aes, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_GCM)),
      sizeof(BCRYPT_CHAIN_MODE_GCM), 0) >= 0 &&
    BCryptGenerateSymmetricKey(aes, &key, nullptr, 0, material.data(), static_cast<ULONG>(material.size()), 0) >= 0;
  const bool ok = ready && (restore ? BCryptDecrypt(key,in,size,&auth,nullptr,0,out,size,&written,0) :
    BCryptEncrypt(key,in,size,&auth,nullptr,0,out,size,&written,0)) >= 0 && written == size;
  SecureZeroMemory(material.data(), material.size());
  if (key) BCryptDestroyKey(key);
  if (aes) BCryptCloseAlgorithmProvider(aes, 0);
  if (hash) BCryptCloseAlgorithmProvider(hash, 0);
  return ok;
}

inline std::optional<RuntimeError> run(const std::filesystem::path &root, const std::filesystem::path &file,
    std::string &password, bool restore) {
  const auto current = LocalPublisher::read(root);
  if (const auto error = std::get_if<RuntimeError>(&current)) return *error;
  const auto &publisher = std::get<Json>(current);
  if (restore && !publisher.is_null()) return RuntimeError{"PUBLISHER_EXISTS", "已有本地发布者，恢复不会覆盖它"};
  if (!restore && publisher.is_null()) return RuntimeError{"PUBLISHER_REQUIRED", "请先创建本地发布者"};

  const RuntimeError invalid{"PUBLISHER_BACKUP_INVALID", "备份格式无效或口令错误"};
  std::vector<unsigned char> salt(16), nonce(12), tag(16);
  std::string input, output;
  if (restore) {
    const auto backup = LocalPublisher::fileData(file);
    if (!backup.is_object() || backup.value("version", Json()) != 1) return invalid;
    for (const auto field : {"salt", "nonce", "tag", "ciphertext"})
      if (!backup.contains(field) || !backup[field].is_string()) return invalid;
    salt = PackageCrypto::unhex(backup["salt"].get<std::string>());
    nonce = PackageCrypto::unhex(backup["nonce"].get<std::string>());
    tag = PackageCrypto::unhex(backup["tag"].get<std::string>());
    const auto cipher = PackageCrypto::unhex(backup["ciphertext"].get<std::string>());
    if (salt.size() != 16 || nonce.size() != 12 || tag.size() != 16 || cipher.size() < 123 || cipher.size() > 242) return invalid;
    input.assign(cipher.begin(), cipher.end());
  } else {
    const auto value = LocalPublisher::fileData(LocalPublisher::path(root));
    if (!value.is_object() || !value.contains("protectedKey") || !value["protectedKey"].is_string()) return invalid;
    const auto bytes = CefBase64Decode(value["protectedKey"].get<std::string>());
    if (!bytes || !random(salt) || !random(nonce)) return invalid;
    std::vector<unsigned char> protectedKey(bytes->GetSize());
    bytes->GetData(protectedKey.data(), protectedKey.size(), 0);
    DATA_BLOB encrypted{static_cast<DWORD>(protectedKey.size()), protectedKey.data()}, der{};
    if (!CryptUnprotectData(&encrypted, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &der))
      return RuntimeError{"PUBLISHER_KEY_FAILED", "当前 Windows 用户无法解密发布者密钥"};
    input = publisher["name"].get<std::string>() + '\0';
    input.append(reinterpret_cast<char *>(der.pbData), der.cbData);
    SecureZeroMemory(der.pbData, der.cbData);
    LocalFree(der.pbData);
  }
  const bool ok = transform(restore, password, salt, nonce, tag, input, output);
  SecureZeroMemory(input.data(), input.size());
  if (!ok) {
    SecureZeroMemory(output.data(), output.size());
    return invalid;
  }
  if (!restore) {
    const auto cipher = std::span(reinterpret_cast<const unsigned char *>(output.data()), output.size());
    const auto bytes = Json{{"version",1},{"salt",PackageCrypto::hex(salt)},{"nonce",PackageCrypto::hex(nonce)},
      {"tag",PackageCrypto::hex(tag)},{"ciphertext",PackageCrypto::hex(cipher)}}.dump();
    if (!PackageFiles::write(file, bytes))
      return RuntimeError{"PUBLISHER_BACKUP_WRITE_FAILED", "无法保存备份，请选择可写且尚不存在的文件"};
    return std::nullopt;
  }
  const auto separator = output.find('\0');
  RuntimeResult<Json> saved = invalid;
  if (separator != std::string::npos && separator > 0 && separator <= 120 &&
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, output.data(), static_cast<int>(separator), nullptr, 0) > 0 &&
      std::none_of(output.begin(), output.begin() + separator, [](unsigned char c) { return c < 32 || c == 127; })) {
    auto der = output.substr(separator + 1);
    saved = LocalPublisher::save(root, output.substr(0, separator), der);
    SecureZeroMemory(der.data(), der.size());
  }
  SecureZeroMemory(output.data(), output.size());
  if (const auto error = std::get_if<RuntimeError>(&saved)) return *error;
  return std::nullopt;
}
}
#endif

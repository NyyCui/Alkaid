/** @file crypto.h Windows CNG 哈希与 ECDSA 验签；运行时不持有发布者私钥。 */
#ifndef ALKAID_PACKAGE_CRYPTO_H
#define ALKAID_PACKAGE_CRYPTO_H
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <string>
#include <vector>
#include <span>

namespace alkaid::PackageCrypto {
inline std::vector<unsigned char> unhex(const std::string &text) {
  if (text.size() % 2) return {};
  auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
  std::vector<unsigned char> result(text.size() / 2);
  for (std::size_t i = 0; i < result.size(); ++i) {
    const auto a = digit(text[i * 2]), b = digit(text[i * 2 + 1]);
    if (a < 0 || b < 0) return {};
    result[i] = static_cast<unsigned char>(a * 16 + b);
  }
  return result;
}
inline std::string hex(std::span<const unsigned char> bytes) {
  constexpr char DIGITS[] = "0123456789abcdef";
  std::string result;
  result.reserve(bytes.size() * 2);
  for (const auto value : bytes) { result += DIGITS[value >> 4]; result += DIGITS[value & 15]; }
  return result;
}
inline std::string sha256(const std::string &bytes) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
  std::array<unsigned char, 32> digest{};
  const auto status = BCryptHash(algorithm, nullptr, 0,
    reinterpret_cast<PUCHAR>(const_cast<char *>(bytes.data())), static_cast<ULONG>(bytes.size()),
    digest.data(), static_cast<ULONG>(digest.size()));
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return status < 0 ? "" : hex(digest);
}
inline bool verify(const std::string &publicHex, const std::string &signatureHex, const std::string &input) {
  const auto publicKey = unhex(publicHex);
  auto signature = unhex(signatureHex);
  auto hash = unhex(sha256(input));
  if (publicKey.size() != 65 || publicKey[0] != 4 || signature.size() != 64 || hash.size() != 32) return false;
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_KEY_HANDLE key = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0) return false;
  std::array<unsigned char, sizeof(BCRYPT_ECCKEY_BLOB) + 64> blob{};
  const BCRYPT_ECCKEY_BLOB header{BCRYPT_ECDSA_PUBLIC_P256_MAGIC, 32};
  memcpy(blob.data(), &header, sizeof(header));
  memcpy(blob.data() + sizeof(header), publicKey.data() + 1, 64);
  const auto imported = BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key,
    blob.data(), static_cast<ULONG>(blob.size()), 0);
  const bool valid = imported >= 0 && BCryptVerifySignature(key, nullptr, hash.data(),
    static_cast<ULONG>(hash.size()), signature.data(), static_cast<ULONG>(signature.size()), 0) >= 0;
  if (key) BCryptDestroyKey(key);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return valid;
}
inline std::string signatureInput(const std::string &manifest, const std::string &integrity) {
  std::string result = "YPKG-V3\n";
  for (const auto *bytes : {&manifest, &integrity}) {
    const auto size = static_cast<uint32_t>(bytes->size());
    for (int i = 0; i < 4; ++i) result += static_cast<char>((size >> (i * 8)) & 255);
    result += *bytes;
  }
  return result;
}
} // namespace alkaid::PackageCrypto
#endif // ALKAID_PACKAGE_CRYPTO_H

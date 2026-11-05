#ifndef ALKAID_CRYPTO_UTIL_H
#define ALKAID_CRYPTO_UTIL_H

#include <algorithm>
#include <functional>
#include <istream>
#include <ostream>
#include <sodium.h>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

/**
 * @file crypto_util.h
 * @brief 加解密工具，提供对称加密、非对称加密、哈希、签名功能
 *
 * 基于 libsodium 实现：
 * - 对称加密：XChaCha20-Poly1305（crypto_secretbox）
 * - 非对称加密：X25519 + XChaCha20-Poly1305（crypto_box）
 * - 哈希算法：SHA-256、SHA-512、BLAKE2b
 * - 数字签名：Ed25519
 *
 * 每种操作统一支持 3 种输入：ReadSource（ReadCallback / istream）、字节数组
 * 输出支持：返回字节数组（CryptoResult）、写入输出流（WriteSink）
 */
namespace alkaid::CryptoUtil {

/// 秘钥对
struct KeyPair {
  std::vector<uint8_t> publicKey;  ///< 公钥（加密、验签）
  std::vector<uint8_t> privateKey; ///< 私钥（解密、签名）
};

// region 流式读写源类型

/**
 * @brief 流式读取回调：从数据源读取数据到缓冲区
 *
 * 回调签名：int64_t(uint8_t* buffer, size_t len)
 * - buffer: 输出缓冲区
 * - len: 缓冲区最大容量
 * - 返回值: 实际读取的字节数（>=0），返回负数表示读取失败
 */
using Reader = std::function<int64_t(uint8_t *buffer, size_t len)>;

/**
 * @brief 流式写入回调：将数据写入目标
 *
 * 回调签名：bool(const uint8_t* data, size_t len)
 * - data: 要写入的数据
 * - len: 数据大小
 * - 返回值: true 表示写入成功，false 表示写入失败
 */
using Writer = std::function<bool(const uint8_t *data, size_t len)>;

/// 流式读取源：支持回调或标准输入流
using ReadSource = std::variant<Reader, std::istream *>;

/// 流式写入目标：支持回调或标准输出流
using WriteSink = std::variant<Writer, std::ostream *>;

// endregion

namespace _ {

/// 流式操作的读写块大小
constexpr size_t STREAM_CHUNK_SIZE = 16384;

/**
 * @brief 确保 libsodium 已初始化（线程安全，仅首次调用时执行）
 * @return true 表示初始化成功
 */
inline bool ensureInit() {
  static const bool ok = sodium_init() >= 0;
  return ok;
}

/**
 * @brief 从 ReadSource 创建 Reader
 * @param source 读取源（回调或输入流指针）
 * @return 统一的读取回调
 */
inline Reader makeReader(const ReadSource &source) {
  if (std::holds_alternative<Reader>(source)) {
    return std::get<Reader>(source);
  }
  return [is = std::get<std::istream *>(source)](uint8_t *buf, const size_t max) -> int64_t {
    is->read(reinterpret_cast<char *>(buf), static_cast<std::streamsize>(max));
    return is->gcount();
  };
}

/**
 * @brief 从 WriteSink 创建 Writer
 * @param sink 写入目标（回调或输出流指针）
 * @return 统一的写入回调
 */
inline Writer makeWriter(const WriteSink &sink) {
  if (std::holds_alternative<Writer>(sink)) {
    return std::get<Writer>(sink);
  }
  return [os = std::get<std::ostream *>(sink)](const uint8_t *data, const size_t size) -> bool {
    os->write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
    return os->good();
  };
}

} // namespace _

// region 秘钥生成

/**
 * @brief 生成Hash秘钥（BLAKE2b）
 * @param keyLen 秘钥长度，可选值：crypto_generichash_KEYBYTES_MIN、crypto_generichash_KEYBYTES、crypto_generichash_KEYBYTES_MAX
 * @return 生成的秘钥
 */
inline std::vector<uint8_t> generateSymmetricKey(const uint32_t keyLen = crypto_generichash_KEYBYTES) {
  _::ensureInit();
  std::vector<uint8_t> key(keyLen);
  randombytes_buf(key.data(), key.size());
  return key;
}

/// 生成对称加密秘钥
inline std::vector<uint8_t> generateSymmetricKey() {
  _::ensureInit();
  std::vector<uint8_t> key(crypto_secretbox_KEYBYTES);
  randombytes_buf(key.data(), key.size());
  return key;
}

/// 生成非对称加密秘钥对
inline KeyPair generateAsymmetricKey() {
  _::ensureInit();
  std::vector<uint8_t> pk(crypto_box_PUBLICKEYBYTES);
  std::vector<uint8_t> sk(crypto_box_SECRETKEYBYTES);
  crypto_box_keypair(pk.data(), sk.data());
  return {std::move(pk), std::move(sk)};
}

/// 生成签名秘钥对
inline KeyPair generateSigningKey() {
  _::ensureInit();
  std::vector<uint8_t> pk(crypto_sign_PUBLICKEYBYTES);
  std::vector<uint8_t> sk(crypto_sign_SECRETKEYBYTES);
  crypto_sign_keypair(pk.data(), sk.data());
  return {std::move(pk), std::move(sk)};
}

// endregion

// region 哈希

/**
 * @brief 计算 SHA-256 哈希值（流式读取）
 * @param source 数据读取源
 * @return 哈希结果（32 字节），或错误信息
 */
inline tl::expected<std::vector<uint8_t>, std::string> sha256(const ReadSource &source) {
  if (!_::ensureInit()) return tl::make_unexpected("初始化失败");
  const auto reader = _::makeReader(source);
  crypto_hash_sha256_state state{};
  if (crypto_hash_sha256_init(&state) != 0) return tl::make_unexpected("初始化失败");
  std::vector<uint8_t> buf(_::STREAM_CHUNK_SIZE);

  while (true) {
    const int64_t bytesRead = reader(buf.data(), buf.size());
    if (bytesRead < 0) return tl::make_unexpected("数据读取失败");
    if (bytesRead == 0) break;
    if (crypto_hash_sha256_update(&state, buf.data(), static_cast<uint64_t>(bytesRead)) != 0) return tl::make_unexpected("计算失败");
  }
  std::vector<uint8_t> result(crypto_hash_sha256_BYTES);
  if (crypto_hash_sha256_final(&state, result.data()) != 0) return tl::make_unexpected("计算失败");
  return std::move(result);
}

/**
 * @brief 计算 SHA-256 哈希值（内存数据）
 * @param data 待计算的数据
 * @return 哈希结果（32 字节），或错误信息
 */
inline tl::expected<std::vector<uint8_t>, std::string> sha256(const std::vector<uint8_t> &data) {
  if (!_::ensureInit()) return tl::make_unexpected("初始化失败");
  std::vector<uint8_t> result(crypto_hash_sha256_BYTES);
  if (crypto_hash_sha256(result.data(), data.data(), data.size()) != 0) return tl::make_unexpected("计算失败");
  return std::move(result);
}

/**
 * @brief 计算 SHA-512 哈希值（流式读取）
 * @param source 数据读取源
 * @return 哈希结果（64 字节），或错误信息
 */
inline tl::expected<std::vector<uint8_t>, std::string> sha512(const ReadSource &source) {
  if (!_::ensureInit()) return tl::make_unexpected("初始化失败");
  const auto reader = _::makeReader(source);
  crypto_hash_sha512_state state{};
  if (crypto_hash_sha512_init(&state) != 0) return tl::make_unexpected("初始化失败");
  std::vector<uint8_t> buf(_::STREAM_CHUNK_SIZE);

  while (true) {
    const int64_t bytesRead = reader(buf.data(), buf.size());
    if (bytesRead < 0) return tl::make_unexpected("数据读取失败");
    if (bytesRead == 0) break;
    if (crypto_hash_sha512_update(&state, buf.data(), static_cast<uint64_t>(bytesRead)) != 0) return tl::make_unexpected("计算失败");
  }
  std::vector<uint8_t> result(crypto_hash_sha512_BYTES);
  if (crypto_hash_sha512_final(&state, result.data()) != 0) return tl::make_unexpected("计算失败");
  return std::move(result);
}

/**
 * @brief 计算 SHA-512 哈希值（内存数据）
 * @param data 待计算的数据
 * @return 哈希结果（64 字节），或错误信息
 */
inline tl::expected<std::vector<uint8_t>, std::string> sha512(const std::vector<uint8_t> &data) {
  if (!_::ensureInit()) return tl::make_unexpected("初始化失败");
  std::vector<uint8_t> result(crypto_hash_sha512_BYTES);
  if (crypto_hash_sha512(result.data(), data.data(), data.size()) != 0) return tl::make_unexpected("计算失败");
  return std::move(result);
}

/**
 * @brief 计算 BLAKE2b 哈希值（流式读取）
 *
 * 支持可选秘钥和自定义输出长度。
 * hashLen 可选值：
 * - crypto_generichash_blake2b_BYTES_MIN（16）：最小输出长度
 * - crypto_generichash_blake2b_BYTES（32）：默认输出长度
 * - crypto_generichash_blake2b_BYTES_MAX（64）：最大输出长度
 *
 * @param source 数据读取源
 * @param key 可选的秘钥，为空则使用无秘钥模式（0~64 字节）
 * @param hashLen 输出哈希长度（16~64 字节）
 * @return 哈希结果，或错误信息
 */
inline tl::expected<std::vector<uint8_t>, std::string> blake2b(const ReadSource &source,
                                                               const std::string &key = {},
                                                               const size_t hashLen = crypto_generichash_blake2b_BYTES) {
  if (!_::ensureInit()) return tl::make_unexpected("初始化失败");
  const size_t keylen = key.size();
  if (keylen > crypto_generichash_KEYBYTES_MAX) return tl::make_unexpected("秘钥长度超出限制");
  const auto reader = _::makeReader(source);
  crypto_generichash_state state{};
  if (const auto keyPtr = key.empty() ? nullptr : reinterpret_cast<const uint8_t *>(key.data());
      crypto_generichash_init(&state, keyPtr, keylen, hashLen) != 0) {
    return tl::make_unexpected("初始化失败");
  }
  std::vector<uint8_t> buf(_::STREAM_CHUNK_SIZE);

  while (true) {
    const int64_t bytesRead = reader(buf.data(), buf.size());
    if (bytesRead < 0) return tl::make_unexpected("数据读取失败");
    if (bytesRead == 0) break;
    if (crypto_generichash_update(&state, buf.data(), static_cast<uint64_t>(bytesRead)) != 0) return tl::make_unexpected("计算失败");
  }
  std::vector<uint8_t> result(hashLen);
  if (crypto_generichash_final(&state, result.data(), hashLen) != 0) return tl::make_unexpected("计算失败");
  return std::move(result);
}

/**
 * @brief 计算 BLAKE2b 哈希值（内存数据）
 *
 * 支持可选秘钥和自定义输出长度。
 * hashLen 可选值：
 * - crypto_generichash_blake2b_BYTES_MIN（16）：最小输出长度
 * - crypto_generichash_blake2b_BYTES（32）：默认输出长度
 * - crypto_generichash_blake2b_BYTES_MAX（64）：最大输出长度
 *
 * @param data 待计算的数据
 * @param key 可选的秘钥，为空则使用无秘钥模式（0~64 字节）
 * @param hashLen 输出哈希长度（16~64 字节）
 * @return 哈希结果，或错误信息
 */
inline tl::expected<std::vector<uint8_t>, std::string> blake2b(const std::vector<uint8_t> &data,
                                                               const std::string &key = {},
                                                               const size_t hashLen = crypto_generichash_blake2b_BYTES) {
  if (!_::ensureInit()) return tl::make_unexpected("初始化失败");
  const size_t keylen = key.size();
  if (keylen > crypto_generichash_KEYBYTES_MAX) return tl::make_unexpected("秘钥长度超出限制");

  std::vector<uint8_t> result(hashLen);
  if (const auto keyPtr = key.empty() ? nullptr : reinterpret_cast<const uint8_t *>(key.data());
      crypto_generichash(result.data(), hashLen, data.data(), data.size(), keyPtr, keylen) != 0) {
    return tl::make_unexpected("计算失败");
  }
  return std::move(result);
}

/**
 * @brief 常量时间比较两个哈希值是否相等
 *
 * 使用 sodium_memcmp 进行安全比较，防止时序攻击
 *
 * @param hash1 第一个哈希值
 * @param hash2 第二个哈希值
 * @return true 表示两个哈希值相等，false 表示不相等或长度不一致
 */
inline bool verifyHash(const std::vector<uint8_t> &hash1, const std::vector<uint8_t> &hash2) {
  if (!_::ensureInit()) return false;
  if (hash1.size() != hash2.size()) return false;
  return sodium_memcmp(hash1.data(), hash2.data(), hash1.size()) == 0;
}
// endregion

// region 对称加密（XChaCha20-Poly1305）

/**
 * @brief 对称加密（内存数据，返回密文字节数组）
 *
 * 使用 XChaCha20-Poly1305 加密，自动生成随机 nonce。
 * 输出格式：nonce + 密文 + MAC 标签
 *
 * @param plaintext 待加密的明文数据
 * @param key 对称秘钥
 * @return 密文字节数组（nonce + 密文 + MAC），或错误信息
 */
inline tl::expected<std::vector<uint8_t>, std::string> encrypt(const std::vector<uint8_t> &plaintext,
                                                               const std::vector<uint8_t> &key) {
  if (!_::ensureInit()) return tl::make_unexpected("初始化失败");
  if (key.size() != crypto_secretbox_KEYBYTES) return tl::make_unexpected("秘钥错误");

  std::vector<uint8_t> nonce(crypto_secretbox_NONCEBYTES);
  randombytes_buf(nonce.data(), crypto_secretbox_NONCEBYTES);

  const size_t plaintextSize = plaintext.size();
  std::vector<uint8_t> ciphertext(nonce.size() + plaintextSize + crypto_secretbox_MACBYTES);
  std::copy_n(nonce.begin(), nonce.size(), ciphertext.begin());
  if (crypto_secretbox_easy(ciphertext.data() + nonce.size(), plaintext.data(),
                            plaintextSize,
                            nonce.data(), key.data()) != 0) {
    return tl::make_unexpected("加密失败");
  }
  return std::move(ciphertext);
}

/**
 * @brief 对称加密（内存数据，写入到指定输出目标）
 *
 * 使用 XChaCha20-Poly1305 加密，自动生成随机 nonce。
 * 输出格式：nonce + 密文 + MAC 标签
 *
 * @param plaintext 待加密的明文数据
 * @param writeSink 密文写入目标
 * @param key 对称秘钥
 * @return 空字符串表示成功，否则为错误信息
 */
inline tl::expected<std::string, std::string> encrypt(const std::vector<uint8_t> &plaintext,
                                                      const WriteSink &writeSink,
                                                      const std::vector<uint8_t> &key) {
  if (!_::ensureInit()) return "初始化失败";
  const auto writer = _::makeWriter(writeSink);
  if (key.size() != crypto_secretbox_KEYBYTES) return "秘钥错误";

  std::vector<uint8_t> nonce(crypto_secretbox_NONCEBYTES);
  randombytes_buf(nonce.data(), crypto_secretbox_NONCEBYTES);

  const size_t plaintextSize = plaintext.size();
  std::vector<uint8_t> ciphertext(nonce.size() + plaintextSize + crypto_secretbox_MACBYTES);
  std::copy_n(nonce.begin(), nonce.size(), ciphertext.begin());
  if (crypto_secretbox_easy(ciphertext.data() + nonce.size(), plaintext.data(),
                            plaintextSize, nonce.data(), key.data()) != 0) {
    return "加密失败";
  }
  if (!writer(ciphertext.data(), ciphertext.size())) return "写入失败";
  return {};
}

/**
 * @brief 对称加密（流式读取，流式写入）
 *
 * 使用 XChaCha20-Poly1305 加密，自动生成随机 nonce。
 * 适用于大数据量的分块加密场景。
 * 输出格式：stream header + 分块密文（每块含 MAC 标签）
 *
 * @param source 明文数据读取源
 * @param writeSink 密文写入目标
 * @param key 对称秘钥
 * @return 空字符串表示成功，否则为错误信息
 */
inline tl::expected<std::string, std::string> encrypt(const ReadSource &source,
                                                      const WriteSink &writeSink,
                                                      const std::vector<uint8_t> &key) {
  if (!_::ensureInit()) return "初始化失败";
  if (key.size() != crypto_secretbox_KEYBYTES) return "秘钥错误";
  const auto reader = _::makeReader(source);
  const auto writer = _::makeWriter(writeSink);

  crypto_secretstream_xchacha20poly1305_state state;
  uint8_t header[crypto_secretstream_xchacha20poly1305_HEADERBYTES];
  if (crypto_secretstream_xchacha20poly1305_init_push(&state, header, key.data()) != 0) return "初始化失败";
  if (!writer(header, sizeof(header))) return "写入失败";
  std::vector<uint8_t> inChunk(_::STREAM_CHUNK_SIZE);
  std::vector<uint8_t> outChunk(_::STREAM_CHUNK_SIZE + crypto_secretstream_xchacha20poly1305_ABYTES);

  while (true) {
    const auto readBytes = reader(inChunk.data(), _::STREAM_CHUNK_SIZE);
    if (readBytes < 0) return "数据读取失败";
    const uint8_t tag = readBytes == 0 ? crypto_secretstream_xchacha20poly1305_TAG_FINAL : crypto_secretstream_xchacha20poly1305_TAG_MESSAGE;
    uint64_t outLen;
    if (crypto_secretstream_xchacha20poly1305_push(
            &state, outChunk.data(), &outLen,
            inChunk.data(), static_cast<uint64_t>(readBytes),
            nullptr, 0, tag) != 0) {
      return "加密失败";
    }
    if (!writer(outChunk.data(), outLen)) return "写入失败";
    if (tag == crypto_secretstream_xchacha20poly1305_TAG_FINAL) break;
  }
  return {};
}

/**
 * @brief 对称解密（内存数据，返回明文字节数组）
 *
 * 输入格式应为：nonce + 密文 + MAC 标签
 *
 * @param ciphertext 待解密的密文数据
 * @param key 对称秘钥
 * @return 明文字节数组，或错误信息
 */
inline tl::expected<std::vector<uint8_t>, std::string> decrypt(const std::vector<uint8_t> &ciphertext,
                                                               const std::vector<uint8_t> &key) {
  if (!_::ensureInit()) return tl::make_unexpected("初始化失败");
  if (key.size() != crypto_secretbox_KEYBYTES) return tl::make_unexpected("秘钥错误");

  if (ciphertext.size() < crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES) return tl::make_unexpected("密文数据不完整");

  const auto *nonce = ciphertext.data();
  const auto *encrypted = ciphertext.data() + crypto_secretbox_NONCEBYTES;
  const auto encryptedLen = ciphertext.size() - crypto_secretbox_NONCEBYTES;

  std::vector<uint8_t> plaintext(encryptedLen - crypto_secretbox_MACBYTES);
  if (crypto_secretbox_open_easy(plaintext.data(), encrypted,
                                 encryptedLen,
                                 nonce, key.data()) != 0) {
    return tl::make_unexpected("解密失败，密文可能被篡改或秘钥不匹配");
  }
  return std::move(plaintext);
}

/**
 * @brief 对称解密（内存数据，写入到指定输出目标）
 *
 * 输入格式应为：nonce + 密文 + MAC 标签
 *
 * @param ciphertext 待解密的密文数据
 * @param writeSink 明文写入目标
 * @param key 对称秘钥
 * @return 空字符串表示成功，否则为错误信息
 */
inline tl::expected<std::string, std::string> decrypt(const std::vector<uint8_t> &ciphertext,
                                                      const WriteSink &writeSink,
                                                      const std::vector<uint8_t> &key) {
  if (!_::ensureInit()) return "初始化失败";
  const auto writer = _::makeWriter(writeSink);
  if (key.size() != crypto_secretbox_KEYBYTES) return "秘钥错误";

  if (ciphertext.size() < crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES) return "密文数据不完整";

  const auto *nonce = ciphertext.data();
  const auto *encrypted = ciphertext.data() + crypto_secretbox_NONCEBYTES;
  const auto encryptedLen = ciphertext.size() - crypto_secretbox_NONCEBYTES;

  std::vector<uint8_t> plaintext(encryptedLen - crypto_secretbox_MACBYTES);
  if (crypto_secretbox_open_easy(plaintext.data(), encrypted,
                                 encryptedLen,
                                 nonce, key.data()) != 0) {
    return "解密失败，密文可能被篡改或秘钥不匹配";
  }
  if (!writer(plaintext.data(), plaintext.size())) return "写入失败";
  return "";
}

/**
 * @brief 对称解密（流式读取，流式写入）
 *
 * 使用 XChaCha20-Poly1305 进行流式解密，
 * 适用于大数据量的分块解密场景。
 * 输入格式应为：stream header + 分块密文
 *
 * @param source 密文数据读取源
 * @param writeSink 明文写入目标
 * @param key 对称秘钥
 * @return 空字符串表示成功，否则为错误信息
 */
inline tl::expected<std::string, std::string> decrypt(const ReadSource &source,
                                                      const WriteSink &writeSink,
                                                      const std::vector<uint8_t> &key) {
  if (!_::ensureInit()) return "初始化失败";

  if (key.size() != crypto_secretbox_KEYBYTES) return "秘钥错误";
  const auto reader = _::makeReader(source);
  const auto writer = _::makeWriter(writeSink);

  uint8_t header[crypto_secretstream_xchacha20poly1305_HEADERBYTES];
  auto readBytes = reader(header, sizeof(header));
  if (readBytes != sizeof(header)) return "密文头不完整";

  crypto_secretstream_xchacha20poly1305_state state;
  if (crypto_secretstream_xchacha20poly1305_init_pull(&state, header, key.data()) != 0) return "初始化失败";

  std::vector<uint8_t> inChunk(_::STREAM_CHUNK_SIZE + crypto_secretstream_xchacha20poly1305_ABYTES);
  std::vector<uint8_t> outChunk(_::STREAM_CHUNK_SIZE);

  while (true) {
    readBytes = reader(inChunk.data(), inChunk.size());
    if (readBytes < 0) return "数据读取失败";
    if (readBytes == 0) return "数据不完整";

    uint64_t outLen;
    uint8_t tag;
    const int ret = crypto_secretstream_xchacha20poly1305_pull(
        &state, outChunk.data(), &outLen, &tag,
        inChunk.data(), readBytes,
        nullptr, 0);
    if (ret != 0) return "解密失败，密文可能被篡改或秘钥不匹配";
    if (!writer(outChunk.data(), outLen)) return "写入失败";

    if (tag == crypto_secretstream_xchacha20poly1305_TAG_FINAL) break;
  }
  return "";
}

// endregion

// region 非对称加密（X25519 + XChaCha20-Poly1305）

/**
 * @brief 非对称加密（内存数据，返回密文字节数组）
 *
 * 使用 X25519 + XChaCha20-Poly1305 加密，自动生成随机 nonce。
 * 输出格式：nonce + 密文 + MAC 标签
 *
 * @param plaintext 待加密的明文数据
 * @param recipientPublicKey 接收方公钥
 * @param senderPrivateKey 发送方私钥
 * @return 密文字节数组（nonce + 密文 + MAC），或错误信息
 */
inline tl::expected<std::vector<uint8_t>, std::string> encryptAsymmetric(const std::vector<uint8_t> &plaintext,
                                                                         const std::vector<uint8_t> &recipientPublicKey,
                                                                         const std::vector<uint8_t> &senderPrivateKey) {
  if (!_::ensureInit()) return tl::make_unexpected("初始化失败");
  if (recipientPublicKey.size() != crypto_box_PUBLICKEYBYTES) return tl::make_unexpected("秘钥错误");
  if (senderPrivateKey.size() != crypto_box_SECRETKEYBYTES) return tl::make_unexpected("秘钥错误");

  std::vector<uint8_t> nonce(crypto_box_NONCEBYTES);
  randombytes_buf(nonce.data(), crypto_box_NONCEBYTES);

  const size_t plaintextSize = plaintext.size();
  std::vector<uint8_t> ciphertext(nonce.size() + plaintextSize + crypto_box_MACBYTES);
  std::copy_n(nonce.begin(), nonce.size(), ciphertext.begin());
  if (crypto_box_easy(ciphertext.data() + nonce.size(), plaintext.data(),
                      plaintextSize, nonce.data(),
                      recipientPublicKey.data(), senderPrivateKey.data()) != 0) {
    return tl::make_unexpected("加密失败");
  }
  return std::move(ciphertext);
}

/**
 * @brief 非对称加密（内存数据，写入到指定输出目标）
 *
 * 使用 X25519 + XChaCha20-Poly1305 加密，自动生成随机 nonce。
 * 输出格式：nonce + 密文 + MAC 标签
 *
 * @param plaintext 待加密的明文数据
 * @param writeSink 密文写入目标
 * @param recipientPublicKey 接收方公钥
 * @param senderPrivateKey 发送方私钥
 * @return 空字符串表示成功，否则为错误信息
 */
inline tl::expected<std::string, std::string> encryptAsymmetric(const std::vector<uint8_t> &plaintext,
                                                                const WriteSink &writeSink,
                                                                const std::vector<uint8_t> &recipientPublicKey,
                                                                const std::vector<uint8_t> &senderPrivateKey) {
  if (!_::ensureInit()) return "初始化失败";
  const auto writer = _::makeWriter(writeSink);
  if (recipientPublicKey.size() != crypto_box_PUBLICKEYBYTES) return "秘钥错误";
  if (senderPrivateKey.size() != crypto_box_SECRETKEYBYTES) return "秘钥错误";

  std::vector<uint8_t> nonce(crypto_box_NONCEBYTES);
  randombytes_buf(nonce.data(), crypto_box_NONCEBYTES);

  const size_t plaintextSize = plaintext.size();
  std::vector<uint8_t> ciphertext(nonce.size() + plaintextSize + crypto_box_MACBYTES);
  std::copy_n(nonce.begin(), nonce.size(), ciphertext.begin());
  if (crypto_box_easy(ciphertext.data() + nonce.size(), plaintext.data(),
                      plaintextSize, nonce.data(),
                      recipientPublicKey.data(), senderPrivateKey.data()) != 0) return "加密失败";
  if (!writer(ciphertext.data(), ciphertext.size())) return "写入失败";
  return {};
}

/**
 * @brief 非对称解密（内存数据，返回明文字节数组）
 *
 * 输入格式应为：nonce + 密文 + MAC 标签
 *
 * @param ciphertext 待解密的密文数据
 * @param recipientPublicKey 接收方公钥
 * @param senderPrivateKey 发送方私钥
 * @return 明文字节数组，或错误信息
 */
inline tl::expected<std::vector<uint8_t>, std::string> decryptAsymmetric(const std::vector<uint8_t> &ciphertext,
                                                                         const std::vector<uint8_t> &recipientPublicKey,
                                                                         const std::vector<uint8_t> &senderPrivateKey) {
  if (!_::ensureInit()) return tl::make_unexpected("初始化失败");

  if (recipientPublicKey.size() != crypto_box_PUBLICKEYBYTES) return tl::make_unexpected("秘钥错误");
  if (senderPrivateKey.size() != crypto_box_SECRETKEYBYTES) return tl::make_unexpected("秘钥错误");

  if (ciphertext.size() < crypto_box_NONCEBYTES + crypto_box_MACBYTES) return tl::make_unexpected("密文数据不完整");

  const auto *nonce = ciphertext.data();
  const auto *encrypted = ciphertext.data() + crypto_box_NONCEBYTES;
  const auto encryptedLen = ciphertext.size() - crypto_box_NONCEBYTES;

  std::vector<uint8_t> plaintext(encryptedLen - crypto_box_MACBYTES);
  if (crypto_box_open_easy(plaintext.data(), encrypted,
                           encryptedLen,
                           nonce,
                           recipientPublicKey.data(), senderPrivateKey.data()) != 0) {
    return tl::make_unexpected("解密失败，密文被篡改或秘钥不匹配");
  }
  return std::move(plaintext);
}

/**
 * @brief 非对称解密（内存数据，写入到指定输出目标）
 *
 * 输入格式应为：nonce + 密文 + MAC 标签
 *
 * @param ciphertext 待解密的密文数据
 * @param writeSink 明文写入目标
 * @param recipientPublicKey 接收方公钥
 * @param senderPrivateKey 发送方私钥
 * @return 空字符串表示成功，否则为错误信息
 */
inline tl::expected<std::string, std::string> decryptAsymmetric(const std::vector<uint8_t> &ciphertext,
                                                                const WriteSink &writeSink,
                                                                const std::vector<uint8_t> &recipientPublicKey,
                                                                const std::vector<uint8_t> &senderPrivateKey) {
  if (!_::ensureInit()) return "初始化失败";
  const auto writer = _::makeWriter(writeSink);
  if (recipientPublicKey.size() != crypto_box_PUBLICKEYBYTES) return "秘钥错误";
  if (senderPrivateKey.size() != crypto_box_SECRETKEYBYTES) return "秘钥错误";

  constexpr size_t HEADER = crypto_box_NONCEBYTES;
  if (ciphertext.size() < HEADER + crypto_box_MACBYTES) return "密文数据不完整";
  const auto *nonce = ciphertext.data();
  const auto *encrypted = ciphertext.data() + HEADER;
  const auto encryptedLen = ciphertext.size() - HEADER;

  std::vector<uint8_t> plaintext(encryptedLen - crypto_box_MACBYTES);
  if (crypto_box_open_easy(plaintext.data(), encrypted,
                           encryptedLen,
                           nonce,
                           recipientPublicKey.data(), senderPrivateKey.data()) != 0) {
    return "解密失败，密文被篡改或秘钥不匹配";
  }
  if (!writer(plaintext.data(), plaintext.size())) return "写入失败";
  return "";
}

// endregion

// region 数字签名（Ed25519）

/**
 * @brief 对内存数据生成 Ed25519 签名
 *
 * 使用 crypto_sign_detached 生成独立签名（签名与数据分离）
 *
 * @param data 待签名的数据
 * @param signingKey 签名私钥
 * @return 签名字节数组（crypto_sign_BYTES 字节），或错误信息
 */
inline tl::expected<std::vector<uint8_t>, std::string> sign(const std::vector<uint8_t> &data,
                                                            const std::vector<uint8_t> &signingKey) {
  if (!_::ensureInit()) return tl::make_unexpected("初始化失败");
  if (signingKey.size() != crypto_sign_SECRETKEYBYTES) return tl::make_unexpected("秘钥错误");

  std::vector<uint8_t> signature(crypto_sign_BYTES);
  uint64_t sigLen;
  if (crypto_sign_detached(signature.data(), &sigLen,
                           data.data(), data.size(),
                           signingKey.data()) != 0) return tl::make_unexpected("签名失败");
  signature.resize(sigLen);
  return std::move(signature);
}

/**
 * @brief 对流式数据生成 Ed25519 签名
 *
 * 使用 crypto_sign 流式接口（init/update/final_create）对大数据量进行签名，
 * 数据分块读取，不会一次性加载到内存
 *
 * @param source 数据读取源
 * @param signingKey 签名私钥（crypto_sign_SECRETKEYBYTES 字节）
 * @return 签名字节数组（crypto_sign_BYTES 字节），或错误信息
 */
inline tl::expected<std::vector<uint8_t>, std::string> sign(const ReadSource &source,
                                                            const std::vector<uint8_t> &signingKey) {
  if (!_::ensureInit()) return tl::make_unexpected("初始化失败");
  if (signingKey.size() != crypto_sign_SECRETKEYBYTES) return tl::make_unexpected("秘钥错误");
  const auto reader = _::makeReader(source);

  crypto_sign_state state;
  if (crypto_sign_init(&state) != 0) return tl::make_unexpected("初始化失败");

  std::vector<uint8_t> buf(_::STREAM_CHUNK_SIZE);
  while (true) {
    const int64_t bytesRead = reader(buf.data(), buf.size());
    if (bytesRead < 0) return tl::make_unexpected("数据读取失败");
    if (bytesRead == 0) break;
    if (crypto_sign_update(&state, buf.data(),
                           static_cast<uint64_t>(bytesRead)) != 0) return tl::make_unexpected("签名失败");
  }

  std::vector<uint8_t> signature(crypto_sign_BYTES);
  uint64_t sigLen;
  if (crypto_sign_final_create(&state, signature.data(), &sigLen,
                               signingKey.data()) != 0) return tl::make_unexpected("签名失败");
  signature.resize(sigLen);
  return std::move(signature);
}

/**
 * @brief 对流式数据生成 Ed25519 签名，并将签名写入指定输出目标
 *
 * 使用 crypto_sign 流式接口对大数据量进行签名，
 * 签名结果同时返回并写入到 writeSink
 *
 * @param source 数据读取源
 * @param writeSink 签名写入目标
 * @param signingKey 签名私钥（crypto_sign_SECRETKEYBYTES 字节）
 * @return 签名字节数组和空错误字符串（成功时），或 nullopt 和错误信息
 */
inline tl::expected<std::vector<uint8_t>, std::string> sign(const ReadSource &source,
                                                            const WriteSink &writeSink,
                                                            const std::vector<uint8_t> &signingKey) {
  if (!_::ensureInit()) return tl::make_unexpected("初始化失败");
  if (signingKey.size() != crypto_sign_SECRETKEYBYTES) return tl::make_unexpected("秘钥错误");
  const auto reader = _::makeReader(source);
  const auto writer = _::makeWriter(writeSink);

  crypto_sign_state state;
  if (crypto_sign_init(&state) != 0) return tl::make_unexpected("初始化失败");

  std::vector<uint8_t> buf(_::STREAM_CHUNK_SIZE);
  while (true) {
    const int64_t bytesRead = reader(buf.data(), buf.size());
    if (bytesRead < 0) return tl::make_unexpected("数据读取失败");
    if (bytesRead == 0) break;
    if (crypto_sign_update(&state, buf.data(),
                           static_cast<uint64_t>(bytesRead)) != 0) return tl::make_unexpected("签名失败");
  }

  std::vector<uint8_t> signature(crypto_sign_BYTES);
  uint64_t sigLen;
  if (crypto_sign_final_create(&state, signature.data(), &sigLen,
                               signingKey.data()) != 0) return tl::make_unexpected("签名失败");
  signature.resize(sigLen);
  if (!writer(signature.data(), signature.size())) return tl::make_unexpected("写入失败");
  return std::move(signature);
}

/**
 * @brief 验证 Ed25519 签名（内存数据）
 *
 * 使用 crypto_sign_verify_detached 验证独立签名
 *
 * @param data 原始数据
 * @param signature 待验证的签名
 * @param verifyKey 验证公钥
 * @return true 表示签名有效，false 表示签名无效或参数不匹配
 */
inline bool verifySign(const std::vector<uint8_t> &data,
                       const std::vector<uint8_t> &signature,
                       const std::vector<uint8_t> &verifyKey) {
  if (!_::ensureInit()) return false;
  if (signature.size() != crypto_sign_BYTES) return false;
  if (verifyKey.size() != crypto_sign_PUBLICKEYBYTES) return false;

  return crypto_sign_verify_detached(signature.data(),
                                     data.data(), data.size(),
                                     verifyKey.data()) == 0;
}

/**
 * @brief 验证 Ed25519 签名（流式数据）
 *
 * 使用 crypto_sign 流式接口对大数据量进行签名验证，
 * 数据分块读取，不会一次性加载到内存
 *
 * @param source 原始数据读取源
 * @param signature 待验证的签名
 * @param verifyKey 验证公钥
 * @return true 表示签名有效，false 表示签名无效或参数不匹配
 */
inline bool verifySign(const ReadSource &source,
                       const std::vector<uint8_t> &signature,
                       const std::vector<uint8_t> &verifyKey) {
  if (!_::ensureInit()) return false;
  if (signature.size() != crypto_sign_BYTES) return false;
  if (verifyKey.size() != crypto_sign_PUBLICKEYBYTES) return false;
  const auto reader = _::makeReader(source);

  crypto_sign_state state;
  if (crypto_sign_init(&state) != 0) return false;

  std::vector<uint8_t> buf(_::STREAM_CHUNK_SIZE);
  while (true) {
    const int64_t bytesRead = reader(buf.data(), buf.size());
    if (bytesRead < 0) return false;
    if (bytesRead == 0) break;
    if (crypto_sign_update(&state, buf.data(), static_cast<uint64_t>(bytesRead)) != 0) return false;
  }

  return crypto_sign_final_verify(&state, signature.data(), verifyKey.data()) == 0;
}

// endregion

} // namespace common::CryptoUtil

#endif // ALKAID_CRYPTO_UTIL_H

#ifndef ALKAID_YAO_PKG_H
#define ALKAID_YAO_PKG_H

#include <filesystem>
#include <string>
#include <tl/expected.hpp>
#include <vector>
#include <zlib-ng.h>

/**
 * @file yao_pkg.h
 * @brief 应用包解析器
 *
 * 应用包文件格式（小端序）：
 * [Header] 包头 28 bytes
 *   4 bytes  : 魔数 "YPKG"
 *   2 bytes  : 主版本号
 *   2 bytes  : 次版本号
 *   4 bytes  : 文件条目数量
 *   8 bytes  : 资源文件总大小（压缩前）
 *   8 bytes  : 资源文件总大小（压缩后，即后面 "文件列表"的总大小）
 *
 * [Entry] 条目列表：每个条目 (N + 41 bytes)
 *   4 bytes  : 文件名长度
 *   N bytes  : 文件名
 *   1 byte   : 是否压缩
 *   8 bytes  : 文件大小（压缩前）
 *   8 bytes  : 文件大小（压缩后）
 *   8 bytes  : 偏移量（相对于 Body 起始位置）
 *   8 bytes  : 修改时间
 *   4 bytes  : 文件的 CRC32 校验值
 *
 * [Body] 文件列表 (每个按条目顺序排列)
 *
 * [Footer] 包尾 (132 bytes)
 *   32 bytes : Ed25519 公钥
 *   32 bytes : Header + Entry 的哈希 (SHA-256)
 *   64 bytes : Ed25519 数字签名 (Header + Entry 的哈希签名)
 *   4 bytes  : 魔数 "YPKG"
 */
namespace alkaid {

class YaoPkg {
  static constexpr uint32_t MAGIC = 0x474B5059; // "YPKG"
  static constexpr int32_t HEADER_SIZE = 28;
  static constexpr int32_t FOOTER_SIZE = 100;
  static constexpr size_t CHUNK_SIZE = 65536;
  static constexpr size_t COMPRESSED_SIZE = 1024;

  struct Provider {
    std::string name;
    std::string identifier;
    std::vector<uint8_t> publicKey;

    bool writeTo(std::ofstream &os) const;

    bool readFrom(std::ifstream &is);
  };

  struct Program {
    std::string name;
    std::string identifier;
    std::string icon;

    bool writeTo(std::ofstream &os) const;

    bool readFrom(std::ifstream &is);
  };

  /// 包头
  struct Header {
    uint32_t magic;        ///< 魔数 59 50 4B 47
    uint16_t majorVersion; ///< 主版本号
    uint16_t minorVersion; ///< 次版本号
    Provider provider;     ///< 发布人信息
    Program program;       ///< 应用信息
    uint32_t fileCount;    ///< 文件条目数量
    uint64_t originalSize; ///< 资源文件总大小（压缩前）
    uint64_t storedSize;   ///< 资源文件总大小（压缩后）
    bool writeTo(std::ofstream &os) const;

    bool readFrom(std::ifstream &is);
  };

  /// 条目描述符
  struct Entry {
    std::string filename;  ///< 文件名
    bool compressed;       ///< 是否压缩
    uint64_t originalSize; ///< 原始大小
    uint64_t storedSize;   ///< 压缩后大小
    uint64_t offset;       ///< 偏移量（相对于文件起始位置）
    uint64_t modTime;      ///< 修改时间
    uint32_t crc32;        ///< 文件数据的 CRC32 校验值
    bool writeTo(std::ofstream &os) const;

    bool readFrom(std::ifstream &is);
  };

  /// 包尾
  struct Footer {
    std::vector<uint8_t> hash;      ///< SHA-256 哈希值，32 字节
    std::vector<uint8_t> signature; ///< Ed25519 数字签名，64 字节
    uint32_t magic;                 ///< 魔数 59 50 4B 47
    bool writeTo(std::ofstream &os) const;

    bool readFrom(std::ifstream &is);
  };

  std::string pkgFilePath_;        ///< 应用包文件路径
  std::vector<uint8_t> publicKey_; ///< 公钥，用于验证签名
  Header header_;                  ///< 包头
  std::vector<Entry> entries_;     ///< 文件条目
  Footer footer_;                  ///< 包尾
  uint64_t fileSize_;              ///< 应用包文件大小
  std::string status_;             ///< 包状态，空表示正常，否则表示错误信息

  std::vector<uint8_t> getMetaBytes();

public:
  /**
   * @brief 构造并加载应用包
   * @param pkgFilePath 应用包文件路径（绝对地址）
   * @param publicKey 公钥，用于验证签名
   * @throws std::runtime_error 文件读取失败、格式错误、签名校验失败时抛出
   * @note 调用前需确保已调用 sodium_init()
   */
  explicit YaoPkg(const std::string &pkgFilePath,
                  const std::vector<uint8_t> &publicKey = std::vector<uint8_t>()
  );

  YaoPkg(const std::string &sourceDir,
         const std::string &outputPath,
         const std::string &name,
         const std::string &identifier,
         const std::string &programName,
         const std::string &programIdentifier,
         const std::string &programIcon,
         const std::vector<uint8_t> &publicKey,
         const std::vector<uint8_t> &privateKey
  );

  /**
   * @brief 读取包中资源文件的内容
   * @param filename 文件名
   * @param offset 文件内偏移量（从文件起始计算），默认为 0
   * @param size 要读取的字节数（0 表示读取全部内容），默认为 0
   * @return 文件内容（存储格式，若文件已压缩则为压缩数据）
   * @throws std::runtime_error 文件不存在、读取失败、CRC32 校验失败时抛出
   */
  tl::expected<std::vector<uint8_t>, std::string> readFile(const std::string &filename, uint64_t offset = 0, uint64_t size = 0);

  const Provider &getProvider() const;

  const Program &getProgram() const;

  ~YaoPkg() = default;

private:
  /**
   * @brief 校验包完整性：计算 Header + Entry 的 SHA-256 哈希，
   *        并与 Footer 中的哈希值和 Ed25519 签名进行比对
   * @throws std::runtime_error 校验失败时抛出
   */
  std::string verifyPackage();
};
} // namespace alkaid

#endif // ALKAID_YAO_PKG_H

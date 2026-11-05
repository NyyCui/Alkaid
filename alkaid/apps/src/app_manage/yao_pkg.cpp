#include "yao_pkg.h"

#include "alkaid_utils/bytes_util.h"
#include "alkaid_utils/compress_util.h"
#include "alkaid_utils/crypto_util.h"
#include "alkaid_utils/file_util.h"

#include <iostream>
#include <utility>
#include <zlib-ng.h>

namespace alkaid {
YaoPkg::YaoPkg(const std::string &sourceDir,
               const std::string &outputPath,
               const std::string &name,
               const std::string &identifier,
               const std::string &programName,
               const std::string &programIdentifier,
               const std::string &programIcon,
               const std::vector<uint8_t> &publicKey,
               const std::vector<uint8_t> &privateKey
)
  : pkgFilePath_(std::move(outputPath))
  , publicKey_(publicKey)
  , header_{MAGIC, 1, 0, {name, identifier, publicKey}, {programName, programIdentifier, programIcon}, 0, 0, 0}
  , footer_{{}, {}, MAGIC}
  , fileSize_(0)
  , status_("") {
  if (!std::filesystem::exists(sourceDir) || !std::filesystem::is_directory(sourceDir)) {
    status_ = std::format("源目录不存在或不是目录：{}", sourceDir);
    return;
  }
  std::ofstream outFile(pkgFilePath_, std::ios::binary);
  if (!outFile.is_open()) {
    status_ = std::format("程序包创建失败：{}", pkgFilePath_);
    return;
  }
  namespace fs = std::filesystem;

  if (!header_.writeTo(outFile)) {
    status_ = "包头写入失败";
    return;
  }

  std::vector<fs::path> filePaths;
  for (const auto &item: fs::recursive_directory_iterator(sourceDir)) {
    if (item.is_regular_file()) {
      const auto &path = item.path();
      filePaths.push_back(path);
      auto filename = fs::relative(path, sourceDir).generic_string();
      uintmax_t currentFileSize = fs::file_size(path);
      header_.originalSize += currentFileSize;
      bool compressed = false;
      // bool compressed = currentFileSize >= COMPRESSED_SIZE;
      uint64_t modTime = static_cast<uint64_t>(FileUtil::getFileModTime(path));
      if (auto &entry = entries_.emplace_back(filename, compressed, currentFileSize, compressed ? 0 : currentFileSize, 0, modTime, 0); !entry.writeTo(outFile)) {
        status_ = "文件索引写入失败";
        return;
      }
    }
  }
  header_.fileCount = static_cast<uint32_t>(entries_.size());

  uint64_t fileOffset = outFile.tellp();
  for (int i = 0; i < filePaths.size(); ++i) {
    auto &entry = entries_[i];
    auto &filePath = filePaths[i];

    std::ifstream inFile(filePath, std::ios::binary);
    if (!inFile.is_open()) {
      status_ = std::format("无法打开资源文件：{}", filePath.string());
      return;
    }
    uint64_t storedSize = 0;
    if (entry.compressed) {
      const auto compressResult = CompressUtil::compressStream(&inFile, &outFile);
      if (!compressResult) {
        status_ = std::format("文件压缩失败：{}", compressResult.error());
        return;
      }
      storedSize = compressResult->compressedSize;
    } else {
      storedSize = entry.originalSize;
      std::vector<uint8_t> chunk(CHUNK_SIZE);
      while (true) {
        inFile.read(reinterpret_cast<char *>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
        const auto bytesRead = static_cast<size_t>(inFile.gcount());
        if (bytesRead == 0) {
          if (inFile.bad()) {
            status_ = std::format("无法读取资源文件：{}", filePath.string());
            return;
          }
          break;
        }
        entry.crc32 = CompressUtil::computeCRC32(chunk.data(), static_cast<uInt>(bytesRead), entry.crc32);
        outFile.write(reinterpret_cast<const char *>(chunk.data()), static_cast<std::streamsize>(bytesRead));
        if (!outFile) {
          status_ = std::format("无法打包资源文件：{}", filePath.string());
          return;
        }
      }
    }
    entry.storedSize = storedSize;
    entry.offset = fileOffset;
    header_.storedSize += storedSize;
    fileOffset = outFile.tellp();
  }
  auto metaBytes = getMetaBytes();
  const auto sha256 = CryptoUtil::sha256(metaBytes);
  if (!sha256) {
    status_ = std::format("Hash值计算失败：{}", sha256.error());
    return;
  }
  auto &sha256Hash = sha256.value();
  const auto signature = CryptoUtil::sign(sha256Hash, privateKey);
  if (!signature) {
    status_ = std::format("数字签名生成失败：{}", signature.error());
    return;
  }
  footer_.hash = std::move(sha256Hash);
  footer_.signature = std::move(signature.value());
  if (!footer_.writeTo(outFile)) {
    status_ = "包尾写入失败";
    return;
  }

  outFile.seekp(0, std::ios::beg);
  if (!header_.writeTo(outFile)) {
    status_ = "包头更新失败";
    return;
  }

  for (auto &entry: entries_) {
    if (!entry.writeTo(outFile)) {
      status_ = "文件索引更新失败";
      return;
    }
  }
  outFile.flush();
  outFile.close();
}

YaoPkg::YaoPkg(const std::string &pkgFilePath, const std::vector<uint8_t> &publicKey)
  : pkgFilePath_(std::move(pkgFilePath))
  , publicKey_(publicKey)
  , header_{MAGIC, 1, 0, {"", "", std::vector<uint8_t>(32, 0)}, {"", "", ""}, 0, 0, 0}
  , footer_{{}, {}, MAGIC}
  , fileSize_(0)
  , status_("") {
  namespace fs = std::filesystem;
  std::ifstream pkgFile(pkgFilePath, std::ios::binary);
  if (!pkgFile.is_open()) {
    status_ = std::format("无法打开程序包文件：{}", pkgFilePath);
    return;
  }
  fileSize_ = fs::file_size(pkgFilePath);
  if (!header_.readFrom(pkgFile)) {
    status_ = std::format("包头读取失败：{}", pkgFilePath);
    return;
  }

  for (uint32_t i = 0; i < header_.fileCount; ++i) {
    auto &entry = entries_.emplace_back("", false, 0, 0, 0, 0, 0);
    if (!entry.readFrom(pkgFile)) {
      status_ = std::format("文件索引读取失败：{}", pkgFilePath);
      return;
    }
  }
  pkgFile.seekg(0 - FOOTER_SIZE, std::ios::end);
  if (!footer_.readFrom(pkgFile)) {
    status_ = std::format("包尾读取失败：{}", pkgFilePath);
    return;
  }
  // status_ = verifyPackage();
}

bool YaoPkg::Provider::writeTo(std::ofstream &os) const {
  const auto nameLen = this->name.length();
  const auto identifierLen = this->identifier.length();
  os.write(reinterpret_cast<const char *>(&nameLen), 4);
  os.write(this->name.data(), nameLen);
  os.write(reinterpret_cast<const char *>(&identifierLen), 4);
  os.write(this->identifier.data(), identifierLen);
  os.write(reinterpret_cast<const char *>(this->publicKey.data()), this->publicKey.size());
  return os.operator bool();
}

bool YaoPkg::Program::writeTo(std::ofstream &os) const {
  const auto nameLen = static_cast<uint32_t>(this->name.length());
  const auto identifierLen = static_cast<uint32_t>(this->identifier.length());
  const auto iconLen = static_cast<uint32_t>(this->icon.length());
  os.write(reinterpret_cast<const char *>(&nameLen), 4);
  os.write(this->name.data(), nameLen);
  os.write(reinterpret_cast<const char *>(&identifierLen), 4);
  os.write(this->identifier.data(), identifierLen);
  os.write(reinterpret_cast<const char *>(&iconLen), 4);
  os.write(this->icon.data(), iconLen);
  return os.operator bool();
}

bool YaoPkg::Header::writeTo(std::ofstream &os) const {
  os.write(reinterpret_cast<const char *>(&this->magic), 4);
  os.write(reinterpret_cast<const char *>(&this->majorVersion), 2);
  os.write(reinterpret_cast<const char *>(&this->minorVersion), 2);
  this->provider.writeTo(os);
  this->program.writeTo(os);
  os.write(reinterpret_cast<const char *>(&this->fileCount), 4);
  os.write(reinterpret_cast<const char *>(&this->originalSize), 8);
  os.write(reinterpret_cast<const char *>(&this->storedSize), 8);
  return os.operator bool();
}

bool YaoPkg::Entry::writeTo(std::ofstream &os) const {
  const auto len = this->filename.length();
  const uint8_t i = this->compressed ? 1 : 0;
  os.write(reinterpret_cast<const char *>(&len), 4);
  os.write(this->filename.data(), len);
  os.write(reinterpret_cast<const char *>(&i), 1);
  os.write(reinterpret_cast<const char *>(&this->originalSize), 8);
  os.write(reinterpret_cast<const char *>(&this->storedSize), 8);
  os.write(reinterpret_cast<const char *>(&this->offset), 8);
  os.write(reinterpret_cast<const char *>(&this->modTime), 8);
  os.write(reinterpret_cast<const char *>(&this->crc32), 4);
  return os.operator bool();
}

bool YaoPkg::Footer::writeTo(std::ofstream &os) const {
  os.write(reinterpret_cast<const char *>(this->hash.data()), 32);
  os.write(reinterpret_cast<const char *>(this->signature.data()), 64);
  os.write(reinterpret_cast<const char *>(&this->magic), 4);
  return os.operator bool();
}

bool YaoPkg::Provider::readFrom(std::ifstream &is) {
  auto nameLenBytes = FileUtil::readFile(is, 4);
  if (!nameLenBytes) return false;
  const auto nameLen = BytesUtil::readInteger<uint32_t>(nameLenBytes.value());
  auto nameBytes = FileUtil::readFile(is, nameLen);
  if (!nameBytes) return false;
  this->name = BytesUtil::readString(nameBytes.value());
  auto identifierLenBytes = FileUtil::readFile(is, 4);
  if (!identifierLenBytes) return false;
  const auto identifierLen = BytesUtil::readInteger<uint32_t>(identifierLenBytes.value());
  auto identifierBytes = FileUtil::readFile(is, identifierLen);
  if (!identifierBytes) return false;
  this->identifier = BytesUtil::readString(identifierBytes.value());
  auto publicKeyBytes = FileUtil::readFile(is, 32);
  if (!publicKeyBytes) return false;
  this->publicKey = publicKeyBytes.value();
  return true;
}

bool YaoPkg::Program::readFrom(std::ifstream &is) {
  auto nameLenBytes = FileUtil::readFile(is, 4);
  if (!nameLenBytes) return false;
  const auto nameLen = BytesUtil::readInteger<uint32_t>(nameLenBytes.value());
  auto nameBytes = FileUtil::readFile(is, nameLen);
  if (!nameBytes) return false;
  this->name = BytesUtil::readString(nameBytes.value());
  auto identifierLenBytes = FileUtil::readFile(is, 4);
  if (!identifierLenBytes) return false;
  const auto identifierLen = BytesUtil::readInteger<uint32_t>(identifierLenBytes.value());
  auto identifierBytes = FileUtil::readFile(is, identifierLen);
  if (!identifierBytes) return false;
  this->identifier = BytesUtil::readString(identifierBytes.value());
  auto iconLenBytes = FileUtil::readFile(is, 4);
  if (!iconLenBytes) return false;
  const auto iconLen = BytesUtil::readInteger<uint32_t>(iconLenBytes.value());
  auto iconBytes = FileUtil::readFile(is, iconLen);
  if (!iconBytes) return false;
  this->icon = BytesUtil::readString(iconBytes.value());
  return true;
}

bool YaoPkg::Header::readFrom(std::ifstream &is) {
  auto headerBytes1 = FileUtil::readFile(is, 8);
  if (!headerBytes1) return false;
  const auto &bytes1 = headerBytes1.value();
  this->magic = BytesUtil::readInteger<uint32_t>(bytes1);
  this->majorVersion = BytesUtil::readInteger<uint16_t>(bytes1, 4);
  this->minorVersion = BytesUtil::readInteger<uint16_t>(bytes1, 6);
  if (!this->provider.readFrom(is) || !this->program.readFrom(is)) return false;
  auto headerBytes2 = FileUtil::readFile(is, 20);
  if (!headerBytes2) return false;
  const auto &bytes2 = headerBytes2.value();
  this->fileCount = BytesUtil::readInteger<uint32_t>(bytes2);
  this->originalSize = BytesUtil::readInteger<uint64_t>(bytes2, 4);
  this->storedSize = BytesUtil::readInteger<uint64_t>(bytes2, 12);
  return true;
}

bool YaoPkg::Entry::readFrom(std::ifstream &is) {
  auto filenameLenBytes = FileUtil::readFile(is, 4);
  if (!filenameLenBytes) return false;
  const auto filenameLen = BytesUtil::readInteger<uint32_t>(filenameLenBytes.value());
  auto entryBytes = FileUtil::readFile(is, 37 + filenameLen);
  if (!entryBytes) return false;
  auto &bytes = entryBytes.value();
  this->filename = BytesUtil::readString(bytes, filenameLen);
  this->compressed = BytesUtil::readInteger<uint8_t>(bytes, filenameLen) != 0;
  this->originalSize = BytesUtil::readInteger<uint64_t>(bytes, filenameLen + 1);
  this->storedSize = BytesUtil::readInteger<uint64_t>(bytes, filenameLen + 9);
  this->offset = BytesUtil::readInteger<uint64_t>(bytes, filenameLen + 17);
  this->modTime = BytesUtil::readInteger<uint64_t>(bytes, filenameLen + 25);
  this->crc32 = BytesUtil::readInteger<uint32_t>(bytes, filenameLen + 33);
  return true;
}

bool YaoPkg::Footer::readFrom(std::ifstream &is) {
  auto footerBytes = FileUtil::readFile(is, FOOTER_SIZE);
  if (!footerBytes) return false;
  const auto &bytes = footerBytes.value();
  const auto &begin = bytes.begin();
  this->hash = {begin, begin + 32};
  this->signature = {begin + 32, begin + 96};
  this->magic = BytesUtil::readInteger<uint32_t>(bytes, 96);
  return true;
}

std::string YaoPkg::verifyPackage() {
  if (header_.magic != MAGIC || footer_.magic != MAGIC) return "无效的程序包格式";
  const auto metaBytes = getMetaBytes();
  auto hash = CryptoUtil::sha256(metaBytes);
  if (!hash) return "程序包校验失败：哈希计算错误";
  if (!CryptoUtil::verifyHash(hash.value(), footer_.hash)) return "程序包校验失败：内容哈希不匹配，包可能已被篡改";
  if (!CryptoUtil::verifySign(hash.value(), footer_.signature, publicKey_)) return "程序包校验失败：签名验证失败，包可能已被篡改";
  return {};
}

tl::expected<std::vector<uint8_t>, std::string> YaoPkg::readFile(const std::string &filename, const uint64_t offset, const uint64_t size) {
  if (!status_.empty()) return tl::unexpected(status_);
  const auto &it = std::ranges::find_if(entries_, [&](const Entry &e) {
    return e.filename == filename;
  });

  if (it == entries_.end()) return tl::unexpected(std::format("资源不存在：{}", filename));

  const Entry &entry = *it;

  if (offset > entry.storedSize) return tl::unexpected(std::format("文件 \"{}\" 读取失败：偏移量 {} 超出文件大小 {}", filename, offset, entry.storedSize));

  uint64_t readSize = size == 0 ? entry.storedSize - offset : size;
  if (offset + readSize > entry.storedSize) {
    readSize = entry.storedSize - offset;
  }
  std::ifstream pkgFile(pkgFilePath_, std::ios::binary);
  if (!pkgFile.is_open()) return tl::unexpected(std::format("无法打开程序包文件：{}", pkgFilePath_));

  const uint64_t absoluteOffset = entry.offset + offset;
  pkgFile.seekg(static_cast<int64_t>(absoluteOffset), std::ios::beg);

  const auto data = FileUtil::readFile(pkgFile, readSize);
  if (!data) return tl::unexpected(std::format("文件 \"{}\" 读取失败：{}", filename, data.error()));
  const auto &bytes = data.value();

  if (entry.compressed) {
    auto result = CompressUtil::decompress(bytes);
    if (!result) return tl::unexpected(std::format("文件 \"{}\" 读取失败：{}", filename, data.error()));
    if (!result.value().result) return tl::unexpected(std::format("文件 \"{}\" 读取失败：{}", filename, data.error()));
    return result.value().result.value();
  }

  return data;
}

const YaoPkg::Provider &YaoPkg::getProvider() const {
  return this->header_.provider;
}

const YaoPkg::Program &YaoPkg::getProgram() const {
  return this->header_.program;
}

std::vector<uint8_t> YaoPkg::getMetaBytes() {
  std::vector<uint8_t> metaBytes;

  BytesUtil::writeInteger(header_.magic, metaBytes);
  BytesUtil::writeInteger(header_.majorVersion, metaBytes);
  BytesUtil::writeInteger(header_.minorVersion, metaBytes);
  BytesUtil::writeInteger(header_.fileCount, metaBytes);
  BytesUtil::writeInteger(header_.originalSize, metaBytes);
  BytesUtil::writeInteger(header_.storedSize, metaBytes);

  for (const auto &[filename, compressed, originalSize, storedSize, offset, modTime, crc32]: entries_) {
    BytesUtil::writeInteger(static_cast<uint32_t>(filename.length()), metaBytes);
    BytesUtil::writeString(filename, metaBytes);
    BytesUtil::writeBool(compressed, metaBytes);
    BytesUtil::writeInteger(originalSize, metaBytes);
    BytesUtil::writeInteger(storedSize, metaBytes);
    BytesUtil::writeInteger(offset, metaBytes);
    BytesUtil::writeInteger(modTime, metaBytes);
    BytesUtil::writeInteger(crc32, metaBytes);
  }
  return metaBytes;
}

} // namespace alkaid

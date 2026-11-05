#ifndef ALKAID_COMPRESS_UTIL_H
#define ALKAID_COMPRESS_UTIL_H

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include <zlib-ng.h>

/**
 * @file compress_util.h
 * @brief 压缩/解压工具
 */
namespace alkaid::CompressUtil {

struct CompressResult {
  std::optional<std::vector<uint8_t>> result; ///< 压缩/解压后的字节数据，流式处理模式下无值
  uint64_t size;                              ///< 原始数据大小
  uint64_t compressedSize;                    ///< 压缩后的大小
  uint32_t crc32;                             ///< 原始数据 CRC32
  uint32_t compressedCrc32;                   ///< 压缩后数据 CRC32
};

// region 枚举定义

/**
 * @brief 压缩格式
 */
enum class Format : int32_t {
  Zlib = 15,      ///< zlib 格式（RFC 1950）
  Gzip = 15 + 16, ///< gzip 格式（RFC 1952）
  Raw = -15       ///< 原始 deflate 格式（RFC 1951）
};

/**
 * @brief 压缩等级
 */
enum class Level : int32_t {
  Default = Z_DEFAULT_COMPRESSION, ///< 默认压缩等级
  None = Z_NO_COMPRESSION,         ///< 不压缩
  BestSpeed = Z_BEST_SPEED,        ///< 最快速度
  BestSize = Z_BEST_COMPRESSION    ///< 最小体积
};

/**
 * @brief 压缩策略
 */
enum class Strategy : int32_t {
  Default = Z_DEFAULT_STRATEGY, ///< 默认策略；适合大多数文本和二进制数据
  Filtered = Z_FILTERED,        ///< 滤波策略；适合图像、音频等数据
  HuffmanOnly = Z_HUFFMAN_ONLY, ///< 仅霍夫曼编码；适合已加密或已压缩的数据（无法再压缩）
  Rle = Z_RLE,                  ///< 行程长度编码；适合大量重复数据（如 BMP 图片）
  Fixed = Z_FIXED               ///< 固定霍夫曼编码；不使用动态霍夫曼表（快速）
};

// endregion

// region 流式读写源类型

/**
 * @brief 流式读取回调：从数据源读取数据到缓冲区
 *
 * 回调签名：int64_t(uint8_t* buffer, size_t len)
 * - buffer: 输出缓冲区
 * - len: 缓冲区最大容量
 * - 返回值: 实际读取的字节数（>=0），返回负数表示读取失败
 */
using ReadCallback = std::function<int64_t(uint8_t *buffer, size_t len)>;

/**
 * @brief 流式写入回调：将数据写入目标
 *
 * 回调签名：bool(const uint8_t* data, size_t len)
 * - data: 要写入的数据
 * - len: 数据大小
 * - 返回值: true 表示写入成功，false 表示写入失败（将中止压缩/解压）
 */
using WriteCallback = std::function<bool(const uint8_t *data, size_t len)>;

/// 流式读取源：支持回调或标准输入流
using ReadSource = std::variant<ReadCallback, std::istream *>;

/// 流式写入目标：支持回调或标准输出流
using WriteSink = std::variant<WriteCallback, std::ostream *>;

// endregion

namespace _ {

/// 流式操作的读写块大小
constexpr size_t STREAM_CHUNK_SIZE = 16384;

/**
 * @brief 将 zng 返回码转换为可读错误信息
 * @param retCode zng 函数返回码
 * @return 可读的错误描述字符串
 */
inline std::string getMsg(const int retCode) {
  switch (retCode) {
    case Z_OK:
      return "Z_OK：操作成功";
    case Z_STREAM_END:
      return "Z_STREAM_END：流处理完成（压缩收尾成功 / 解压读到包结束标记）";
    case Z_NEED_DICT:
      return "Z_NEED_DICT：需要预定义解压字典";
    case Z_ERRNO:
      return "Z_ERRNO：系统调用错误";
    case Z_STREAM_ERROR:
      return "Z_STREAM_ERROR：z_stream状态非法，参数错误或重复销毁";
    case Z_DATA_ERROR:
      return "Z_DATA_ERROR：输入压缩数据损坏、校验失败";
    case Z_MEM_ERROR:
      return "Z_MEM_ERROR：内存分配失败";
    case Z_BUF_ERROR:
      return "Z_BUF_ERROR：缓冲区暂不可用（无输入或输出缓冲区已满，非致命）";
    case Z_VERSION_ERROR:
      return "Z_VERSION_ERROR：头文件与zlib-ng库版本不匹配";
    default:
      return std::string("未知zng返回码：") + std::to_string(retCode);
  }
}

/**
 * @brief 从 ReadSource 创建 ReadCallback
 * @param source 读取源（回调或输入流指针）
 * @return 统一的读取回调
 */
inline ReadCallback makeReader(const ReadSource &source) {
  if (std::holds_alternative<ReadCallback>(source)) {
    return std::get<ReadCallback>(source);
  }
  return [is = std::get<std::istream *>(source)](uint8_t *buf, const size_t max) -> int64_t {
    is->read(reinterpret_cast<char *>(buf), static_cast<std::streamsize>(max));
    return is->gcount();
  };
}

/**
 * @brief 从 WriteSink 创建 WriteCallback
 * @param sink 写入目标（回调或输出流指针）
 * @return 统一的写入回调
 */
inline WriteCallback makeWriter(const WriteSink &sink) {
  if (std::holds_alternative<WriteCallback>(sink)) {
    return std::get<WriteCallback>(sink);
  }
  return [os = std::get<std::ostream *>(sink)](const uint8_t *data, const size_t size) -> bool {
    os->write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
    return os->good();
  };
}

} // namespace _

// region CRC32

/**
 * @brief 增量计算 CRC32 校验值（可多次调用以追加数据）
 * @param data 追加的字节数据
 * @param crc 上一次的 CRC32 值，首次调用传入 0
 * @return 更新后的 CRC32 校验值；空数据直接返回输入的 crc 值
 */
inline uint32_t computeCRC32(const std::vector<uint8_t> &data, const uint32_t crc = 0) {
  if (data.empty()) return crc;
  return zng_crc32(crc, data.data(), static_cast<uint32_t>(data.size()));
}

/**
 * @brief 增量计算 CRC32 校验值（可多次调用以追加数据）
 * @param buf 追加的字节数据
 * @param len 追加的字节数据长度
 * @param crc 上一次的 CRC32 值，首次调用传入 0
 * @return 更新后的 CRC32 校验值；空数据直接返回输入的 crc 值
 */
inline uint32_t computeCRC32(const uint8_t *buf, const uint32_t len, const uint32_t crc = 0) {
  if (buf == nullptr || len == 0) return 0;
  return zng_crc32(crc, buf, len);
}

// endregion

// region 流式压缩/解压

/**
 * @brief 流式压缩：支持回调与标准流的任意组合
 *
 * 支持以下调用方式：
 * - 回调 + 回调：compressStream(readCb, writeCb)
 * - 回调 + 输出流：compressStream(readCb, &ofstream)
 * - 输入流 + 回调：compressStream(&ifstream, writeCb)
 * - 输入流 + 输出流：compressStream(&ifstream, &ofstream)
 *
 * @param source 读取源（ReadCallback 或 std::istream*）
 * @param sink 写入目标（WriteCallback 或 std::ostream*）
 * @param format 压缩格式，默认为 Raw
 * @param level 压缩等级，默认为 Default
 * @param strategy 压缩策略，默认为 Default
 * @return 空字符串表示成功，非空表示错误信息
 */
inline tl::expected<CompressResult, std::string> compressStream(const ReadSource &source,
                                                                const WriteSink &sink,
                                                                const Format format = Format::Raw,
                                                                const Level level = Level::Default,
                                                                const Strategy strategy = Strategy::Default) {
  const auto reader = _::makeReader(source);
  const auto writer = _::makeWriter(sink);

  zng_stream strm{};
  int ret = zng_deflateInit2(&strm, static_cast<int32_t>(level), Z_DEFLATED,
                             static_cast<int32_t>(format), 8,
                             static_cast<int32_t>(strategy));
  if (ret != Z_OK) return tl::unexpected("初始化失败，" + _::getMsg(ret));

  std::array<uint8_t, _::STREAM_CHUNK_SIZE> inBuf{};
  std::array<uint8_t, _::STREAM_CHUNK_SIZE> outBuf{};

  uint32_t crc32 = 0;
  uint32_t compressedCrc32 = 0;

  do {
    const int64_t bytesRead = reader(inBuf.data(), inBuf.size());
    if (bytesRead < 0) {
      zng_deflateEnd(&strm);
      return tl::unexpected("数据读取失败");
    }
    crc32 = computeCRC32(inBuf.data(), bytesRead, crc32);

    strm.next_in = inBuf.data();
    strm.avail_in = static_cast<uint32_t>(bytesRead);
    int flush = bytesRead == 0 ? Z_FINISH : Z_NO_FLUSH;

    do {
      strm.next_out = outBuf.data();
      strm.avail_out = static_cast<uint32_t>(outBuf.size());

      ret = zng_deflate(&strm, flush);
      if (ret == Z_BUF_ERROR) break;
      if (ret == Z_STREAM_ERROR || ret == Z_MEM_ERROR) {
        zng_deflateEnd(&strm);
        return tl::unexpected("压缩失败" + _::getMsg(ret));
      }
      if (bytesRead == 0) flush = Z_NO_FLUSH;

      if (const size_t have = outBuf.size() - strm.avail_out; have > 0) {
        compressedCrc32 = computeCRC32(outBuf.data(), static_cast<uint32_t>(have), compressedCrc32);
        if (!writer(outBuf.data(), have)) {
          zng_deflateEnd(&strm);
          return tl::unexpected("写入失败");
        }
      }
    } while (strm.avail_out == 0);

  } while (ret != Z_STREAM_END);

  zng_deflateEnd(&strm);
  return CompressResult{std::nullopt,
                        strm.total_in,
                        strm.total_out,
                        crc32,
                        compressedCrc32};
}

/**
 * @brief 流式解压：支持回调与标准流的任意组合
 *
 * 支持以下调用方式：
 * - 回调 + 回调：decompressStream(readCb, writeCb)
 * - 回调 + 输出流：decompressStream(readCb, &ofstream)
 * - 输入流 + 回调：decompressStream(&ifstream, writeCb)
 * - 输入流 + 输出流：decompressStream(&ifstream, &ofstream)
 *
 * @param source 读取源（ReadCallback 或 std::istream*）
 * @param sink 写入目标（WriteCallback 或 std::ostream*）
 * @param format 压缩格式（必须与压缩时一致），默认为 Raw
 * @return 空字符串表示成功，非空表示错误信息
 */
inline tl::expected<CompressResult, std::string> decompressStream(const ReadSource &source,
                                                                  const WriteSink &sink,
                                                                  const Format format = Format::Raw) {
  const auto reader = _::makeReader(source);
  const auto writer = _::makeWriter(sink);
  zng_stream strm{};
  int ret = zng_inflateInit2(&strm, static_cast<int32_t>(format));
  if (ret != Z_OK) return tl::unexpected("初始化失败，" + _::getMsg(ret));

  std::array<uint8_t, _::STREAM_CHUNK_SIZE> inBuf{};
  std::array<uint8_t, _::STREAM_CHUNK_SIZE> outBuf{};

  uint32_t crc32 = 0;
  uint32_t compressedCrc32 = 0;
  do {
    const int64_t bytesRead = reader(inBuf.data(), inBuf.size());
    if (bytesRead < 0) {
      zng_inflateEnd(&strm);
      return tl::unexpected("数据读取失败");
    }

    if (bytesRead == 0) {
      zng_inflateEnd(&strm);
      return tl::unexpected("数据不完整");
    }
    compressedCrc32 = computeCRC32(inBuf.data(), bytesRead, compressedCrc32);

    strm.next_in = inBuf.data();
    strm.avail_in = static_cast<uint32_t>(bytesRead);

    do {
      strm.next_out = outBuf.data();
      strm.avail_out = static_cast<uint32_t>(outBuf.size());
      ret = zng_inflate(&strm, Z_NO_FLUSH);

      if (ret == Z_BUF_ERROR) break;

      if (ret == Z_STREAM_ERROR || ret == Z_NEED_DICT ||
          ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
        zng_inflateEnd(&strm);
        return tl::unexpected("解压失败" + _::getMsg(ret));
      }

      if (const size_t have = outBuf.size() - strm.avail_out; have > 0) {
        crc32 = computeCRC32(outBuf.data(), static_cast<uint32_t>(have), crc32);
        if (!writer(outBuf.data(), have)) {
          zng_inflateEnd(&strm);
          return tl::unexpected("写入失败");
        }
      }

    } while (strm.avail_out == 0);

  } while (ret != Z_STREAM_END);

  zng_inflateEnd(&strm);

  return CompressResult{std::nullopt,
                        strm.total_out,
                        strm.total_in,
                        crc32,
                        compressedCrc32};
}

// endregion

// region 字节数组压缩/解压

/**
 * @brief 一次性压缩字节数组
 * @param data 待压缩的字节数据
 * @param format 压缩格式，默认为 Raw
 * @param level 压缩等级，默认为 Default
 * @param strategy 压缩策略，默认为 Default
 * @return 压缩后的字节数据，或错误信息
 */
inline tl::expected<CompressResult, std::string> compress(const std::vector<uint8_t> &data,
                                                          const Format format = Format::Raw,
                                                          const Level level = Level::Default,
                                                          const Strategy strategy = Strategy::Default) {
  std::vector<uint8_t> result;
  size_t offset = 0;
  const auto reader = [&data, &offset](uint8_t *buf, const size_t max) -> int64_t {
    const size_t remaining = data.size() - offset;
    const size_t toRead = std::min(remaining, max);
    if (toRead == 0) return 0;
    std::memcpy(buf, data.data() + offset, toRead);
    offset += toRead;
    return static_cast<int64_t>(toRead);
  };
  const auto writer = [&result](const uint8_t *buf, const size_t size) -> bool {
    result.insert(result.end(), buf, buf + size);
    return true;
  };

  const ReadSource source{reader};
  const WriteSink sink{writer};
  return compressStream(source, sink, format, level, strategy);
}

/**
 * @brief 一次性解压字节数组
 * @param data 待解压的压缩数据
 * @param format 压缩格式（必须与压缩时一致），默认为 Raw
 * @return 解压后的字节数据，或错误信息
 */
inline tl::expected<CompressResult, std::string> decompress(const std::vector<uint8_t> &data,
                                                            const Format format = Format::Raw) {
  std::vector<uint8_t> result;
  size_t offset = 0;
  const auto reader = [&data, &offset](uint8_t *buf, const size_t max) -> int64_t {
    const size_t remaining = data.size() - offset;
    const size_t toRead = std::min(remaining, max);
    if (toRead == 0) return 0;
    std::memcpy(buf, data.data() + offset, toRead);
    offset += toRead;
    return static_cast<int64_t>(toRead);
  };
  const auto writer = [&result](const uint8_t *buf, const size_t size) -> bool {
    result.insert(result.end(), buf, buf + size);
    return true;
  };

  const ReadSource source{reader};
  const WriteSink sink{writer};
  return decompressStream(source, sink, format);
}

// endregion

// region 文件压缩/解压

/**
 * @brief 压缩文件到另一个文件
 * @param inputFile 输入文件路径
 * @param outputFile 输出文件路径
 * @param format 压缩格式，默认为 Raw
 * @param level 压缩等级，默认为 Default
 * @param strategy 压缩策略，默认为 Default
 * @return 空字符串表示成功，非空表示错误信息
 */
inline tl::expected<CompressResult, std::string> compressFile(const std::filesystem::path &inputFile,
                                                              const std::filesystem::path &outputFile,
                                                              const Format format = Format::Raw,
                                                              const Level level = Level::Default,
                                                              const Strategy strategy = Strategy::Default) {
  std::ifstream in(inputFile, std::ios::binary);
  if (!in.is_open()) return tl::unexpected("无法打开输入文件：" + inputFile.string());

  std::ofstream out(outputFile, std::ios::binary);
  if (!out.is_open()) return tl::unexpected("无法打开输出文件：" + outputFile.string());

  return compressStream(&in, &out, format, level, strategy);
}

/**
 * @brief 解压文件到另一个文件
 * @param inputFile 压缩的输入文件路径
 * @param outputFile 解压后的输出文件路径
 * @param format 压缩格式（必须与压缩时一致），默认为 Raw
 * @return 空字符串表示成功，非空表示错误信息
 */
inline tl::expected<CompressResult, std::string> decompressFile(const std::filesystem::path &inputFile,
                                                                const std::filesystem::path &outputFile,
                                                                const Format format = Format::Raw) {
  std::ifstream in(inputFile, std::ios::binary);
  if (!in.is_open()) return tl::unexpected("无法打开输入文件：" + inputFile.string());

  std::ofstream out(outputFile, std::ios::binary);
  if (!out.is_open()) return tl::unexpected("无法打开输出文件：" + outputFile.string());

  return decompressStream(&in, &out, format);
}

// endregion

} // namespace common::CompressUtil

#endif // ALKAID_COMPRESS_UTIL_H

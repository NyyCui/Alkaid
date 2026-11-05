#ifndef CEF_RESOURCE_HANDLER_H
#define CEF_RESOURCE_HANDLER_H
#include "include/cef_resource_handler.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace alkaid {
namespace fs = std::filesystem;

class ResourceHandler : public CefResourceHandler {
  IMPLEMENT_REFCOUNTING(ResourceHandler);
  // 根据文件扩展名获取MIME类型
  static std::string GetMimeTypeFromPath(const fs::path &file_path) {
    std::string ext = file_path.extension().string();
    // 转为小写
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // 常见 MIME 类型映射表
    static const std::unordered_map<std::string, std::string> mime_map = {
        // 文本类
        {".html", "text/html"},
        {".htm", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".json", "application/json"},
        {".xml", "application/xml"},
        {".txt", "text/plain"},
        {".csv", "text/csv"},

        // 图片类
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".bmp", "image/bmp"},
        {".webp", "image/webp"},
        {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"},
        {".tiff", "image/tiff"},

        // 字体类
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        {".ttf", "font/ttf"},
        {".otf", "font/otf"},
        {".eot", "application/vnd.ms-fontobject"},

        // 多媒体类
        {".mp3", "audio/mpeg"},
        {".wav", "audio/wav"},
        {".mp4", "video/mp4"},
        {".webm", "video/webm"},
        {".ogg", "video/ogg"},
        {".mpeg", "video/mpeg"},

        // 其他
        {".pdf", "application/pdf"},
        {".zip", "application/zip"},
        {".rar", "application/x-rar-compressed"},
        {".7z", "application/x-7z-compressed"},
        {".exe", "application/x-msdownload"},
        {".dll", "application/x-msdownload"}};

    auto it = mime_map.find(ext);
    if (it != mime_map.end())
      return it->second;
    else
      return "application/octet-stream"; // 默认二进制流
  }

  fs::path root_dir_;
  size_t offset_;
  std::string data_;
  std::string mime_type_;

public:
  explicit ResourceHandler(const fs::path &root_dir);

  bool Open(CefRefPtr<CefRequest> request, bool &handle_request,
            CefRefPtr<CefCallback> callback) override;

  bool Read(void *data_out, int bytes_to_read, int &bytes_read,
            CefRefPtr<CefResourceReadCallback> callback) override;

  // 获取响应头 - CEF调用此方法获取响应的MIME类型和状态码
  void GetResponseHeaders(CefRefPtr<CefResponse> response,
                          int64_t &response_length,
                          CefString &redirect_url) override;
  // 取消请求
  void Cancel() override;
};
} // namespace alkaid
#endif // CEF_RESOURCE_HANDLER_H

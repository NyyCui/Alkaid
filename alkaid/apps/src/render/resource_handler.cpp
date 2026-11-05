#include "resource_handler.h"

#include "alkaid_log/logging.h"
#include "alkaid_utils/process_util.h"
#include "app_manage/yao_pkg.h"

#include <regex>
#include <zlib-ng.h>

namespace alkaid {
struct UrlParts {
  std::string protocol;
  std::string domain;
  std::string path;
  std::string query; // 不包含 '?'，例如 "name=value&foo=bar"
};

std::optional<UrlParts> parseUrl(const std::string &url) {
  // 正则：协议://域名(可选路径)(可选查询)
  // 查询部分 (\?.*)? 会捕获包括 ? 在内的整个字符串
  std::regex url_regex(
                       R"(^([a-zA-Z][a-zA-Z0-9+.-]*)://([^/?:]+)(/?[^?]*)?(\?.*)?$)");

  std::smatch matches;
  if (std::regex_match(url, matches, url_regex)) {
    UrlParts parts;
    parts.protocol = matches[1].str();
    parts.domain = matches[2].str();
    parts.path = matches[3].matched ? matches[3].str() : "";
    if (parts.path.starts_with("/")) {
      parts.path = parts.path.substr(1);
    }
    if (parts.path.ends_with("/")) {
      parts.path = parts.path.substr(0, parts.path.size() - 1);
    }
    // 如果匹配到查询部分，则去掉开头的 '?'
    parts.query = matches[4].matched ? matches[4].str().substr(1) : "";

    return parts;
  }
  return std::nullopt;
}

ResourceHandler::ResourceHandler(const fs::path &root_dir)
  : root_dir_(root_dir)
  , offset_(0)
  , data_()
  , mime_type_("application/octet-stream") {}

bool ResourceHandler::Open(CefRefPtr<CefRequest> request, bool &handle_request,
                           CefRefPtr<CefCallback> callback
) {
  try {
    handle_request = true;
    const std::string url = request->GetURL().ToString();

    auto parts = parseUrl(url);
    if (!parts.has_value()) {
      return false;
    }
    const auto &urlParts = parts.value();

    // 解析域名：以最后一个下划线为分隔符，前半段为目录名，后半段为应用包名称
    const auto &domain = urlParts.domain;
    const auto lastUnderscorePos = domain.rfind('-');
    if (lastUnderscorePos == std::string::npos) {
      return false;
    }
    const std::string orgIdentifier = domain.substr(0, lastUnderscorePos);
    const std::string programIdentifier = domain.substr(lastUnderscorePos + 1);

    // 构建应用包路径并打开
    const fs::path pkgPath = fs::path(ProcessUtil::homeDir() / APP_PATH) / orgIdentifier / programIdentifier / (programIdentifier + ".yao");
    YaoPkg pkg(pkgPath.string());

    // 资源文件名：URL 中域名之后的路径部分
    std::string resourceName = urlParts.path;
    if (resourceName.empty()) {
      resourceName = "index.html";
    }

    // 读取资源文件，不存在时回退到 index.html
    auto result = pkg.readFile(resourceName);
    if (!result.has_value()) {
      resourceName = "index.html";
      result = pkg.readFile(resourceName);
      if (!result.has_value()) {
        return false;
      }
    }

    const auto &data = result.value();
    data_.assign(reinterpret_cast<const char *>(data.data()), data.size());
    offset_ = 0;
    mime_type_ = GetMimeTypeFromPath(fs::path(resourceName));
  } catch (...) {
    LOGGER_DEBUG("未知异常1");
  }
  return true;
}

bool ResourceHandler::Read(void *data_out, int bytes_to_read, int &bytes_read,
                           CefRefPtr<CefResourceReadCallback> callback
) {
  try {
    if (offset_ >= data_.size()) {
      bytes_read = 0;
      return false; // 表示数据传输完成
    }

    size_t remaining = data_.size() - offset_;
    int read_size = static_cast<int>(std::min<size_t>(bytes_to_read, remaining));
    std::memcpy(data_out, data_.data() + offset_, read_size);
    offset_ += read_size;
    bytes_read = read_size;
  } catch (...) {
    LOGGER_DEBUG("未知异常2");
  }
  return true;
}

void ResourceHandler::GetResponseHeaders(CefRefPtr<CefResponse> response,
                                         int64_t &response_length,
                                         CefString &redirect_url
) {
  response->SetStatus(200);
  response->SetStatusText("OK");
  response->SetMimeType(mime_type_);
  response_length = data_.size();
}

void ResourceHandler::Cancel() {
  // 清理资源
  data_.clear();
  offset_ = 0;
}
} // namespace alkaid

/** @file text_codec.h 严格分块文本编解码，保留跨块字符，不猜测无 BOM 的旧编码。 */
#ifndef ALKAID_TEXT_CODEC_H
#define ALKAID_TEXT_CODEC_H
#include "package_types.h"
#include "file_access.h"
namespace alkaid::TextCodec {
inline bool encoding(const std::string &value, bool reading) {
  return value == "binary" || value == "utf-8" || value == "utf-8-bom" || value == "utf-16le" || value == "utf-16be" ||
    value == "gb18030" || (reading && value == "auto");
}
inline RuntimeError invalid() { return {"FILE_ENCODING", "文本编码不匹配或字符不完整，请选择正确编码；二进制请使用字节接口"}; }
inline RuntimeResult<std::string> wideTo(const std::wstring &wide, UINT page) {
  if (wide.empty()) return std::string{};
  const auto count = WideCharToMultiByte(page,WC_ERR_INVALID_CHARS,wide.data(),static_cast<int>(wide.size()),nullptr,0,nullptr,nullptr);
  if (!count) return invalid();
  std::string result(count,'\0');
  if (!WideCharToMultiByte(page,WC_ERR_INVALID_CHARS,wide.data(),static_cast<int>(wide.size()),result.data(),count,nullptr,nullptr)) return invalid();
  return result;
}
inline RuntimeResult<std::wstring> toWide(const std::string &bytes, UINT page) {
  if (bytes.empty()) return std::wstring{};
  const auto count = MultiByteToWideChar(page,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),nullptr,0);
  if (!count) return invalid();
  std::wstring result(count,L'\0');
  if (!MultiByteToWideChar(page,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),result.data(),count)) return invalid();
  return result;
}
inline RuntimeResult<std::string> decode(std::string bytes, std::string &kind, std::string &carry, bool first, bool eof) {
  if (first) {
    if (bytes.starts_with(std::string("\xff\xfe\0\0",4)) || bytes.starts_with(std::string("\0\0\xfe\xff",4))) return invalid();
    if (kind == "auto") {
      kind = bytes.starts_with("\xff\xfe") ? "utf-16le" : bytes.starts_with("\xfe\xff") ? "utf-16be" :
        bytes.starts_with("\xef\xbb\xbf") ? "utf-8-bom" : "utf-8";
    }
    if ((kind == "utf-8" || kind == "utf-8-bom") && bytes.starts_with("\xef\xbb\xbf")) bytes.erase(0,3);
    if ((kind == "utf-16le" && bytes.starts_with("\xff\xfe")) || (kind == "utf-16be" && bytes.starts_with("\xfe\xff"))) bytes.erase(0,2);
  }
  bytes = carry + bytes; carry.clear();
  if (kind == "utf-16le" || kind == "utf-16be") {
    auto count = bytes.size() / 2;
    const auto unit = [&](std::size_t index) { const auto a = static_cast<unsigned char>(bytes[index*2]), b = static_cast<unsigned char>(bytes[index*2+1]); return static_cast<wchar_t>(kind == "utf-16le" ? a|(b<<8) : (a<<8)|b); };
    if (!eof && count && unit(count-1) >= 0xd800 && unit(count-1) <= 0xdbff) --count;
    carry = bytes.substr(count*2);
    if (eof && !carry.empty()) return invalid();
    std::wstring wide(count,L'\0');
    for (std::size_t i=0;i<count;++i) wide[i] = unit(i);
    auto result = wideTo(wide,CP_UTF8);
    if (const auto value = std::get_if<std::string>(&result); value && value->find('\0') != std::string::npos) return invalid();
    return result;
  }
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto ch = static_cast<unsigned char>(bytes[offset]);
    std::size_t width = 1;
    if (ch >= 128) {
      if (kind == "gb18030") {
        if (ch < 0x81 || ch > 0xfe) return invalid();
        if (offset+1 == bytes.size()) { if (eof) return invalid(); break; }
        const auto second = static_cast<unsigned char>(bytes[offset+1]);
        width = second >= 0x30 && second <= 0x39 ? 4 : 2;
      } else {
        width = ch >= 0xc2 && ch <= 0xdf ? 2 : ch >= 0xe0 && ch <= 0xef ? 3 : ch >= 0xf0 && ch <= 0xf4 ? 4 : 0;
        if (!width) return invalid();
      }
    }
    if (offset+width > bytes.size()) { if (eof) return invalid(); break; }
    offset += width;
  }
  carry = bytes.substr(offset); bytes.resize(offset);
  const auto wide = toWide(bytes,kind == "gb18030" ? 54936 : CP_UTF8);
  if (const auto error = std::get_if<RuntimeError>(&wide)) return *error;
  auto result = wideTo(std::get<std::wstring>(wide),CP_UTF8);
  if (const auto value = std::get_if<std::string>(&result); value && value->find('\0') != std::string::npos) return invalid();
  return result;
}
inline RuntimeResult<std::string> encode(const std::string &text, const std::string &kind, bool first) {
  if (text.find('\0') != std::string::npos) return invalid();
  const auto wide = toWide(text,CP_UTF8);
  if (const auto error = std::get_if<RuntimeError>(&wide)) return *error;
  if (kind == "utf-8" || kind == "utf-8-bom") return (first && kind == "utf-8-bom" ? std::string("\xef\xbb\xbf") : "") + text;
  if (kind == "gb18030") return wideTo(std::get<std::wstring>(wide),54936);
  std::string result = first ? (kind == "utf-16le" ? "\xff\xfe" : "\xfe\xff") : "";
  for (const auto ch : std::get<std::wstring>(wide)) {
    result += static_cast<char>(kind == "utf-16le" ? ch & 255 : ch >> 8);
    result += static_cast<char>(kind == "utf-16le" ? ch >> 8 : ch & 255);
  }
  return result;
}
inline std::string base64(const std::string &bytes) {
  constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result; result.reserve((bytes.size()+2)/3*4);
  for (std::size_t i=0;i<bytes.size();i+=3) {
    const int a=static_cast<unsigned char>(bytes[i]), b=i+1<bytes.size()?static_cast<unsigned char>(bytes[i+1]):0, c=i+2<bytes.size()?static_cast<unsigned char>(bytes[i+2]):0;
    result += alphabet[a>>2]; result += alphabet[((a&3)<<4)|(b>>4)];
    result += i+1<bytes.size()?alphabet[((b&15)<<2)|(c>>6)]:'='; result += i+2<bytes.size()?alphabet[c&63]:'=';
  }
  return result;
}
inline std::optional<std::string> unbase64(const std::string &text) {
  if (text.size()%4) return std::nullopt;
  const auto digit=[](char ch) { return ch>='A'&&ch<='Z'?ch-'A':ch>='a'&&ch<='z'?ch-'a'+26:ch>='0'&&ch<='9'?ch-'0'+52:ch=='+'?62:ch=='/'?63:-1; };
  std::string bytes;
  for (std::size_t i=0;i<text.size();i+=4) {
    const int a=digit(text[i]),b=digit(text[i+1]),c=text[i+2]=='='?0:digit(text[i+2]),d=text[i+3]=='='?0:digit(text[i+3]);
    if (a<0||b<0||c<0||d<0) return std::nullopt;
    bytes += static_cast<char>((a<<2)|(b>>4));
    if (text[i+2]!='=') bytes += static_cast<char>((b<<4)|(c>>2));
    if (text[i+3]!='=') bytes += static_cast<char>((c<<6)|d);
  }
  if (base64(bytes) != text) return std::nullopt;
  return bytes;
}
}
#endif // ALKAID_TEXT_CODEC_H

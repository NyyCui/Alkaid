/** @file plugin_wire.h 工作进程端的有界同步管道帧。 */
#ifndef ALKAID_PLUGIN_WIRE_H
#define ALKAID_PLUGIN_WIRE_H
#include "package_policy.h"
#include <windows.h>

namespace alkaid::PluginWire {
inline constexpr DWORD MAX_FRAME = 256 * 1024;
inline bool transfer(HANDLE pipe, bool writing, void *data, DWORD length) {
  auto *bytes = static_cast<char *>(data);
  while (length) {
    DWORD count = 0;
    const auto ok = writing ? WriteFile(pipe, bytes, length, &count, nullptr) : ReadFile(pipe, bytes, length, &count, nullptr);
    if (!ok || !count || count > length) return false;
    bytes += count;
    length -= count;
  }
  return true;
}
inline Json read(HANDLE pipe) {
  DWORD size = 0;
  if (!transfer(pipe,false,&size,sizeof(size)) || size == 0 || size > MAX_FRAME) return {};
  std::string text(size, '\0');
  return transfer(pipe,false,text.data(),size) ? PackagePolicy::json(text) : Json();
}
inline bool write(HANDLE pipe, const Json &value) {
  auto text = value.dump();
  if (text.empty() || text.size() > MAX_FRAME) return false;
  auto size = static_cast<DWORD>(text.size());
  return transfer(pipe,true,&size,sizeof(size)) && transfer(pipe,true,text.data(),size);
}
}
#endif

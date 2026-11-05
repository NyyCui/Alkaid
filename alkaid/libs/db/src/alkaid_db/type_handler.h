#ifndef ALKAID_TYPE_HANDLER_H
#define ALKAID_TYPE_HANDLER_H

#include "alkaid_utils/local_date_time.h"
#include "sqnice/query.hh"
#include <cstdlib>
#include <optional>
#include <vector>

namespace alkaid::SQL::TypeHandler {

template<std::same_as<std::vector<uint8_t>> T>
T handler(const sqnice::column_value &columnVal) noexcept {
  const auto blob = columnVal.get<sqnice::blob>();
  if (blob.data == nullptr || blob.size == 0) return {};
  return std::vector(static_cast<const uint8_t *>(blob.data),
                     static_cast<const uint8_t *>(blob.data) + blob.size);
}

template<std::same_as<std::optional<std::vector<uint8_t>>> T>
T handler(const sqnice::column_value &columnVal) noexcept {
  const auto blob = columnVal.get<std::optional<sqnice::blob>>();
  if (!blob) return std::nullopt;
  return std::vector(static_cast<const uint8_t *>(blob->data),
                     static_cast<const uint8_t *>(blob->data) + blob->size);
}

template<std::same_as<LocalDateTime> T>
T handler(const sqnice::column_value &columnVal) noexcept {
  const auto ldt = columnVal.get<std::string>();
  if (auto time = LocalDateTime::parse(ldt)) return time.value<>();
  return LocalDateTime::nulldt();
}

template<std::same_as<std::optional<LocalDateTime>> T>
T handler(const sqnice::column_value &columnVal) noexcept {
  const auto ldt = columnVal.get<std::optional<std::string>>();
  if (!ldt) return std::nullopt;
  if (auto time = LocalDateTime::parse(ldt.value())) return time.value<>();
  return LocalDateTime::nulldt();
}

}
#endif //ALKAID_TYPE_HANDLER_H

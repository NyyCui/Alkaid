#ifndef ALKAID_SQL_VAL_BIND_H
#define ALKAID_SQL_VAL_BIND_H
#include "alkaid_db/sql_val.h"
#include "alkaid_log/logging.h"
#include "sqnice/base.hh"
#include "sqnice/query.hh"

namespace alkaid::SQL {

inline sqnice::status bind_helper(sqnice::statement &stmt, const int32_t idx, const Val &val) {
  return val.visit([&]<typename T0>(const T0 &v) -> sqnice::status {
      using T = std::decay_t<T0>;
      if constexpr (std::is_same_v<T, std::nullptr_t>) {
        LOGGER_DEBUG("绑定数据，索引：{}、值：NULL", idx);
        return stmt.bind(idx, nullptr);
      } else if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>) {
        LOGGER_DEBUG("绑定数据，索引：{}、值：{}", idx, v);
        return stmt.bind(idx, v);
      } else if constexpr (std::same_as<T, std::string>) {
        LOGGER_DEBUG("绑定数据，索引：{}、值：{}", idx, v);
        switch (val.like()) {
          case Val::Like::LEFT_LIKE: return stmt.bind(idx, "%" + v);
          case Val::Like::RIGHT_LIKE: return stmt.bind(idx, v + "%");
          case Val::Like::ALL_LIKE: return stmt.bind(idx, "%" + v + "%");
          default: return stmt.bind(idx, v);
        }
      } else if constexpr (std::same_as<T, std::vector<uint8_t>>) {
        LOGGER_DEBUG("绑定数据，索引：{}、值：X'{}'", idx, StrUtil::toHex(v));
        return stmt.bind(idx, sqnice::blob(v.data(), v.size()));
      } else {
        LOGGER_DEBUG("绑定数据，索引：{}、值：{}", idx, v);
        return stmt.bind(idx, v);
      }
    }
  );
}

}
#endif //ALKAID_SQL_VAL_BIND_H

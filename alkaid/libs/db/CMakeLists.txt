set(MODULE_NAME db)

set(INC src)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
set(LIBS
    unofficial::sqlite3::sqlite3
    ${PROJECT_NAME}_log
)

set(SRCS
    src/sqnice/base.cc
    src/sqnice/base.hh
    src/sqnice/blob_stream.cc
    src/sqnice/blob_stream.hh
    src/sqnice/database.cc
    src/sqnice/database.hh
    src/sqnice/functions.cc
    src/sqnice/functions.hh
    src/sqnice/pool.cc
    src/sqnice/pool.hh
    src/sqnice/query.cc
    src/sqnice/query.hh
    src/sqnice/transaction.cc
    src/sqnice/transaction.hh
    src/sqnice/sqnice.hh
    src/sqnice/statement_cache.hh

    src/alkaid_db/ddl.h
    src/alkaid_db/dml.h
    src/alkaid_db/where.h
    src/alkaid_db/database.h
    src/alkaid_db/dql.h
    src/alkaid_db/pages.h
    src/alkaid_db/sql_val.h
    src/alkaid_db/sql_val_bind.h
    src/alkaid_db/type_handler.h
    src/alkaid_db/mapper.h
)

add_library(${PROJECT_NAME}_${MODULE_NAME} STATIC ${SRCS})

target_include_directories(${PROJECT_NAME}_${MODULE_NAME} PUBLIC ${INC})

target_link_libraries(${PROJECT_NAME}_${MODULE_NAME} PUBLIC ${LIBS})

target_compile_definitions(${PROJECT_NAME}_${MODULE_NAME} INTERFACE
                           NOMINMAX # 防止 windows 库与标准库中的 min、max 冲突
                           UNICODE # 启用 windows 的宽字符 API
                           _HAS_EXCEPTIONS=0
)

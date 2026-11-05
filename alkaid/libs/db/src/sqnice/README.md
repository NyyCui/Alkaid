# SQNice

SQNice 是一个 **非常出色的** C++ SQLite API。

它基于 Wongoo Lee 开发的 [sqlite3pp][SQLITE3PP]，但经过我 Jens Alfke（[@snej](https://github.com/snej)）的大量修改。

在我使用 SQLite 的近 20 年（没错，20 年！）里，我积累了许多最佳实践，也深知如何避免其 API 中一些隐蔽的使用陷阱。

通过 **SQNice**，我尝试将这些经验融入库的 API 设计中，使这些最佳实践变得自动化或至少更加简便。我还努力使其符合 C++ 的惯用写法，让简单的事情保持简单。

## 特性

* **构建方面：**

    * 头文件不包含 `<sqlite3.h>`。这缩短了编译时间，避免产生大量宏和全局命名空间符号，也无需了解或使用晦涩的 C API。
    * 头文件仅约 1000 行代码；源代码约 1500 行。
    * 包含 CMake 构建文件。

* **惯用 C++：**

    * 强类型：例如，错误以 `status` 枚举类返回，而非通用的 `int`。

    * 大多数字符串以 `string_view` 传入，无需调用 `c_str()`。
    * 遵循 RAII 原则。`database` 的析构函数关闭数据库，`transaction` 的析构函数回滚更改，`query::iterator` 的析构函数清除语句的执行状态。

    * 默认使用 C++ 异常，但可以在数据库或查询级别按需关闭，以便处理约束违反等预期错误。
    * 通过 RAII 平滑管理嵌套事务，包括在抛出异常时安全析构事务对象。
    * 使用标准 C++ `iterator` 读取查询行。
    * 绑定参数到语句参数以及读取列值非常简单。参数和行看起来像数组。重载和隐式转换在所需类型之间进行数据转换。这是可扩展的，因此你也可以轻松绑定自定义 C++ 类型。（它还避免了 `unsigned` 类型的一些隐蔽问题。）
    * 包含定义自定义 SQL 函数（甚至聚合函数）的惯用 API。它们使用与查询相同的便捷绑定 API。
    * 执行返回单个值的查询非常简单。
    * 线程安全的数据库连接池，支持安全的并发访问。

* **SQLite 功能：**

    * 支持一些很酷但不太为人知的功能，如备份和 blob 流。

    * 通过一个[可选的]设置调用即可配置 WAL 和增量 vacuum 等最佳实践。
    * 超级简单地复用已编译的语句（`sqlite3_stmt`），不会遇到残留绑定或忘记重置的问题。

    * 避免在语句仍处于忙碌状态时关闭数据库可能导致的隐蔽问题。（有一次我和同事们不得不诊断一个由此引发的非常棘手的数据库损坏 bug！）
    * 嵌套事务（保存点）

    * 便捷地获取和设置 pragma 及 limit。
    * 便捷的增量 vacuum API，可选检查文件中空闲空间是否达到最小比例。
    * 数据库加密 API 同时支持 [SQLCipher][SQLCIPHER] 和 [SQLite 加密扩展][SEE]，并适配它们在设置密码与原始密钥时的不同行为。

## 构建

需要 C++20 编译器。目前 SQNice 已在 macOS 上使用 Clang、Linux 上使用 GCC、Windows 上使用 MSVC 进行了测试。

项目包含 CMake 构建脚本，因此如果你的构建系统也是 CMake，只需使用 `add_subdirectory` 调用来添加 sqnice 即可。

否则，只需将 `src/` 中的源文件添加到你的项目中，并将 `include/` 添加到头文件搜索路径。

如果你的系统未安装 SQLite，或者你想静态链接它，`vendor/sqlite` 中有一份源代码副本。这是撰写本文时的最新版本 3.45.3，但你当然可以从 [sqlite.org][SQLITE] 下载自己的版本。

`vendor/sqlcipher` 中还有 [SQLCipher][SQLCIPHER] 4.6 的副本；这是 SQLite 3.45.3 的修改版本，支持加密数据库。（sqnice 也支持官方的 [SQLite 加密扩展][SEE]，但由于它不是开源的，你需要自行购买和下载。）

如果使用 CMake，设置 `USE_LOCAL_SQLITE` 选项可使用附带的 SQLite 源代码构建 sqnice。或设置 `USE_LOCAL_SQLCIPHER` 选项使用附带的 SQLCipher 构建。否则将链接系统中可用的 `sqlite` 头文件和库（例如 Apple 平台上内置的共享库）。

## 使用

大多数情况下，只需 `#include "sqnice/sqnice.hh"` 即可。

所有内容都在 `sqnice` 命名空间中。

主要类包括：

- `database` — 数据库连接。
- `pool` — 连接到单个数据库文件的线程安全连接池。
- `transaction` — 事务或保存点。使用 RAII 确保在异常或错误时中止事务。
- `query` — 编译后的 SQL `SELECT` 语句。通过其 `iterator` 访问行，迭代器生成由 `column_value` 组成的 `row`。
- `command` — 编译后的非 `SELECT` SQL 语句；不返回行。

### 示例

```c++
#include "sqnice/sqnice.hh"
#include <cstdio>
using namespace sqnice;

database db("", open_flags::memory);
db.setup();                                 // [1]
db.execute(R"""(
    CREATE TABLE contacts (
      id INTEGER PRIMARY KEY,
      name TEXT NOT NULL,
      phone TEXT NOT NULL,
      age INTEGER,
      UNIQUE(name, phone) ); )""");
{
    transaction txn(db);
    command ins = db.command("INSERT INTO contacts (name, phone, age) VALUES (?,?,?)");
    ins.execute("Frank", "555-1212", 36);   // [2]
    ins.execute("Tanya", "555-1313", 50);
    txn.commit();                           // [3]
}

query min_age = db.query("SELECT id, name, age FROM contacts WHERE age >= ?1");
for (query::row row : min_age(40)) {        // [4]
    int64_t id = row[0];                    // [5]
    const char* name = row[1];
    unsigned age = row[2];
    std::printf("%2lld: '%s', age %u\n", id, name, age);
}                                           // [6]
```

**说明：**

1. 这是"最佳实践设置"功能。启用 WAL、外键检查、扩展错误码等。（如果你不知道这些是什么，不用担心。）不客气。
2. 这是一种绑定参数的方式：像函数调用一样一次性绑定所有参数。你也可以调用 `ins.bind(1, ...)` 逐个绑定，或 `ins[1] = ...`。
3. `transaction` 必须显式提交，否则在作用域退出时中止。
4. 使用迭代器的惯用 C++ `for` 循环。同样，我们使用函数式风格先绑定参数。
5. 通过索引访问列值。实际类型是 `column_value`，它具有隐式转换，可以赋值给常见的 C++ 类型。
6. 在底层，当迭代器退出作用域时，`sqlite3_stmt` 会自动重置。

### 错误处理

SQNice 默认在出错时抛出 C++ 异常，但也认可有时你需要手动处理错误。因此可以通过调用 `exceptions(false)` 在任何实例上禁用异常，使其方法返回 `status` 枚举作为错误信息。

从 `database` 创建的对象会继承数据库实例的异常设置，因此在创建数据库后关闭其异常基本上会关闭所有相关异常。

> 注意：即使关闭了异常，SQNice 仍会为程序员错误（如非法参数、参数越界或调用处于无效状态的对象，如对已关闭的 `database` 调用 `execute()`）抛出异常。这些代表代码中的 bug，而非可恢复的运行时错误。SQNice 不会在禁用异常的情况下构建。

构造函数无法返回状态码，因此任何可能失败的构造函数在失败时始终会抛出异常。但你可以通过使用默认构造函数来避免这种情况，然后调用常规方法来初始化对象。例如，你可以无参实例化 `database`，调用 `exceptions(false)`，然后调用 `open()` 打开数据库并获取状态码。

类似地，析构函数无法返回状态，也不应抛出异常。因此例如 `database` 的析构函数会在数据库打开时关闭它，但会忽略任何错误。如果你想确保检测到任何错误，可以先调用 `close()`。

### 命令与查询

执行 SQL 命令最简单的方式是使用 `db.execute("...")`。它接受一个 SQL 字符串。你可以用分号分隔多个命令。这对于创建表或索引等简单操作非常有用。

**`command`** 类更加高级。它更适合 `INSERT`、`UPDATE`、`DELETE` 语句。通常通过调用 `db.command("...")` 来创建。数据库维护已编译命令的缓存，因此如果传入相同的 SQL 字符串，你会得到一个已编译的 `command` 实例，速度更快。

`command` 支持语句中的可绑定参数（`?`、`?1`、`:name`、`$name`）。创建对象时参数值全部重置为 `NULL`，因此在执行语句之前应为其绑定值。有三种方式：

- `cmd.bind(1, "foo")` 或 `cmd.bind(":name", "foo")`
- `cmd[1] = "foo"` 或 `cmd[":name"] = "foo"`
- `cmd("foo", 12, 3.14159)` … 这会从 1 开始一次性绑定所有参数。

调用 `execute()` 执行命令后，可以调用其 `last_insert_rowid()` 方法获取 `INSERT` 的行 ID，或调用 `changes()` 查看更改了多少行。

**`query`** 用于 `SELECT` 语句。通常通过调用 `db.query("...")` 来创建。它有与 `command` 类似的可绑定参数，但通过创建 `iterator` 并逐步遍历结果行来执行。你可以使用 `begin()` 和 `end()` 或 `for(:)` 循环来实现。迭代器生成 `row` 对象，它表现为可索引的 `column` 数组，可以赋值给你的变量或传递给函数。

> 注意：SQLite API 要求你知道何时清除语句的绑定或重置其执行状态。SQNice 会为你处理这些：当数据库提供一个 `command` 或 `query` 对象时，缓存语句的绑定已被清除。`command` 和 `query` 的析构函数会重置其执行状态，query 的 `iterator` 的析构函数也是如此。

### 数据类型

绑定参数或获取列值意味着在 C++ 类型与 SQLite 类型 `INTEGER`、`FLOAT`、`TEXT`、`BLOB` 和 `NULL` 之间进行转换。SQLite 可以与以下 C++ 类型进行隐式转换：

- 所有最大 64 位的整数类型，包括无符号类型。
- `bool`
- `float` 和 `double`
- `const char*`、`std::string_view`、`std::string`
- Blob 可以通过 `const void*`、`std::span<std::byte>` 或结构体 `sqnice::blob`（仅包含指针和大小）访问。
- `nullptr_t`，即字面量 `nullptr`，表示 `NULL`。

绑定文本和 blob 参数时，可以告知 SQLite 是否复制该值。复制较慢但更安全；如果不复制，`command` 或 `query` 会保留指向它的指针，你有责任确保该指针在参数被重新绑定或 `command`/`query` 对象被析构之前保持有效。在 `sqnice` 中，你可以通过用 `uncopied(...)` 包装参数值来禁用复制，例如 `q[1] = uncopied(myLongString)`。

在从 SQLite 获取列值时，复制同样重要。例如，如果将 `column` 赋值给 `string_view`，该 `string_view` 指向查询拥有的内存，仅在迭代器前进或析构之前有效。如果这不能满足需求，请改为赋值给 `string`。

你可以编写转换函数来配合自定义数据类型使用；具体细节待定，但可以查看 `query.hh` 中的 `bindable` 和 `column_helper` 类型以获取线索。

### 事务

事务的重要性不仅在于其 ACID 特性，还在于性能。如果不使用事务，SQLite 会为每条执行的语句隐式包装一个事务，这意味着每条语句都会产生提交更改的显著开销。如果有多条语句需要同时执行，强烈建议将它们放在一个事务中。

`transaction` 类代表一个事务。构造函数开始事务，析构函数确保在你未显式提交或中止时中止事务。这意味着你可以从包含 `transaction` 的代码块中 `return` 或 `break`，甚至抛出异常，一切都会为你清理完毕。但你 **确实需要**记住在作用域退出前调用 `txn.commit()`。

（事务有一种自动提交模式，析构函数会在未抛出异常时自动调用 `commit`。不推荐使用这种方式，因为提交可能会失败，而 C++ 析构函数不应抛出异常。）

事务可以嵌套：在一个事务已激活时开启另一个事务是完全没问题的。如果回滚内部事务，只有自其开始以来的更改会被移除。

> 还有一个更底层的事务 API：`db.beginTransaction()` 和 `db.endTransaction()`。这需要你自行确保每次 begin 都有对应的 end。

### 线程安全

> **重要：** 你 **不得**从多个线程同时访问 `database` 及其创建的任何对象（如 `command`、`query` 等）。

最简单的线程安全方案是使用 SQNice 的线程安全 `pool` 类，它管理同一文件上的一组打开的 `database` 实例。当线程需要使用数据库时，从池中"借"一个，用完后归还。

池最多提供四个只读 `database`（可配置）和一个可写的。只需要一个可写的，因为 SQLite 仅支持单个同时写入者。

如果这不能满足需求，还有其他实现线程安全的方式：

- 打开单个 `database`，将其与自定义的 `mutex` 关联，确保每个线程在访问 `database` 或使用任何 `command`、`query` 或 `transaction` 对象时锁定互斥锁。
- 在每个线程上各打开一个 `database`（指向同一文件），确保每个线程只使用自己的 database 及其派生对象。线程局部变量在此场景下很有用。

[SQLITE]:    https://sqlite.org/download.html
[SQLITE3PP]: https://github.com/iwongu/sqlite3pp
[SQLCIPHER]: https://www.zetetic.net/sqlcipher/
[SEE]:       https://sqlite.org/com/see.html
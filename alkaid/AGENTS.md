# AGENTS.md - 瑶光台

## 项目概览

**瑶光台（alkaid）** 是一个基于 CEF（Chromium Embedded Framework）的 Windows 桌面应用程序， 使用 C++20 标准开发。通过自定义 scheme（`alkaid://`）加载本地 HTML 资源，前端技术用于页面展示，通过 CEF 拦截页面弹框、资源访问由 C++ 技术负责系统调用。

## 项目文件结构

> 主要文件和目录描述

```txt
alkaid/
├── apps/ （应用主程序）
│   ├── resources/ （manifest、图标等资源）
│   ├── src/ （应用主程序源码）
│   │   ├── app_manage/ （应用管理模块）
│   │   ├── browser/ （浏览器相关模块）
│   │   └── db/ （数据库实体定义）
│   └── CMakeLists.txt
├── build/ （构建输出目录）
├── libs/ （库模块）
│   ├── utils/ （工具）
│   ├── db/ （数据库）
│   ├── log/ （日志）
│   └── CMakeLists.txt
├── scripts/ （脚本目录）
│   └── download-cef.py （CEF 下载脚本）
├── tests/ （测试资源）
│   └── html/ （前端测试页面）
├── third_party/ （第三方依赖）
│   └── cef/ （CEF 框架）
├── z-doc/ （项目文档与数据库脚本）
├── CMakeLists.txt （CMake 主文件）
├── CMakePresets.json （CMake 预设配置）
└── vcpkg.json （vcpkg 依赖配置）
```

## 技术栈

重构宿主位于 `apps/host/`，纯核心位于 `core/`，包校验和 SQLite 注册表位于 `infra/package/`，独立桌面在相邻的 `../desktop/`。下方旧目录和 vcpkg 配置属于 legacy 构建。新宿主启用 CEF bootstrap 沙箱，包依赖通过 `package-dependencies.lock.json` 锁定，使用 `scripts/prepare-package-deps.py` 显式准备；正常构建不自动联网。

- **语言**：C++20
- **框架**：CEF 156.0.15（Chromium 156.0.8078.80）
- **构建系统**：CMake（>=3.21）
- **包管理器**：vcpkg（清单模式，管理第三方 C/C++ 依赖）
- **目标平台**：Windows 10 - 11, x64

### CMake 预设

| 预设配置 | 说明                                               |
|----------|----------------------------------------------------|
| Default  | 通用基本配置                                       |
| Debug    | Debug 构建，继承 Default，输出到 `build/debug`     |
| Release  | Release 构建，继承 Default，输出到 `build/release` |

### 关键 CMake 选项

- `USE_SANDBOX`：OFF（当前关闭 CEF 沙箱模式）
- `CEF_RUNTIME_LIBRARY_FLAG`：`/MD`（动态链接 MSVC 运行时）
- `VCPKG_TARGET_TRIPLET`：`x64-windows`

## 代码风格与规范

### 命名规范

| 类型                   | 风格                                             | 示例                    |
|:-----------------------|:-------------------------------------------------|:------------------------|
| 类 / 结构体 / 枚举     | PascalCase（大驼峰）                             | `class MyClass`         |
| 函数 / 方法            | lowerCamelCase（小驼峰）                         | `doSomething()`         |
| 局部变量 / 参数        | lowerCamelCase（小驼峰）                         | `int userCount`         |
| 常量 / constexpr       | SCREAMING_SNAKE_CASE                             | `kMaxSize` / `MAX_SIZE` |
| 宏                     | SCREAMING_SNAKE_CASE                             | `#define MY_MACRO`      |
| 类型模板参数           | PascalCase 或 `T`                                | `template <typename T>` |
| 头文件、源文件的文件名 | snake_case（以类名称的小写下划线形式作为文件名） | `my_class`              |
| 命名空间               | snake_case（除特殊说明外，一律使用 snake_case）  | `alkaid_app`            |

### class 和 struct 的选择

| 场景                        | 使用     |
|:----------------------------|:---------|
| 纯数据容器，所有成员 public | `struct` |
| 有行为、有私有成员          | `class`  |

**说明**

CEF 相关的类声明方式：

```c++
class JSDialogHandler : public CefJSDialogHandler {
  // 类体第一行使用 IMPLEMENT_REFCOUNTING，以配合 CefRefPtr<T> 智能指针
  IMPLEMENT_REFCOUNTING(JSDialogHandler);

public:
  // 如有必要则显示定义无参构造
  JSDialogHandler() = default;
    
  // 显示删除复制构造函数
  JSDialogHandler(const JSDialogHandler &) = delete;
    
  // 显示删除拷贝赋值运算符
  JSDialogHandler &operator=(const JSDialogHandler &) = delete;
  // 其他方法
};
```

### 头文件

使用 `#ifndef` / `#define` / `#endif` 传统模式，宏名称使用 ALKAID_<头文件名称大写下划线形式>

如：pak.h 文件

```c++
#ifndef ALKAID_PAK_H
#define ALKAID_PAK_H

#endif // ALKAID_PAK_H
```

## 注释与文档

- **文件头**：包含简要描述
- **函数注释**：说明功能、参数、返回值、异常
- **复杂逻辑**：添加行内注释“是什么”以及解释“为什么”
- 使用 Doxygen 风格，便于生成文档

## 核心约束与边界

- **推荐**:
  - 尽量使用 MSVC 支持的 C++20 标准特性
  - 声明和定义变量、参数时尽量使用 const 关键字
  - 尽量避免数据拷贝
  - 所有 CEF 相关的类应使用 `CefRefPtr<T>` 智能指针，其他需要使用指针的地方优先考虑使用标准库提供的智能指针
  - 由于CEF 的限制（_HAS_EXCEPTIONS=0），对于需要使用异常的地方，优先使用 `tl-expected` 库来操作，尽量避免使用 `try` / `catch` / `throw`。如果必须使用异常，则应捕获并打印日志
  - 命名空间优先使用 `alkaid`，对于内部实现不对外的代码优先使用嵌套的命名空间 `_`
  - 每个类对应一个头文件和源文件
  - 工具相关的代码应写成仅头文件的形式
  - 如果是工具相关的代码且可以写成纯函数形式，则应使用嵌套命名空间，如：alkaid::工具名（PascalCase 模式）
  - 如果是工具相关的代码但不适合写成纯函数形式，则写成 class 的形式（无需源文件，仅使用头文件），则命名空间优先使用 `alkaid`，且无需嵌套命名空间
  - 对于 if/else 控制结构，优先使用括号括起来的语句块，如果只有一行代码，则无需括号直接写成一行
- **必须**:
  - 注释、错误信息、日志等出现描述性文字的地方必须使用中文，专有名词或代码除外。
- **禁止**:
  - 禁止使用原始 `new`/`delete` (CEF 要求除外)。
  - 禁止硬编码文件路径。
  - 禁止使用 `using namespace std;`。
  - 禁止使用 `using namespace alkaid;`。

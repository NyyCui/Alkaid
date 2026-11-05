# 瑶光台（Alkaid）

瑶光台是一个 **Windows 本地应用工作台**。你可以在同一个桌面里记便签、整理工作区文件、运行自己或他人开发的小工具。开发者可以把 HTML、CSS 和 JavaScript 的构建产物打成 `.ypkg`，让网页工具像桌面应用一样安装、打开和更新，并通过 SDK 使用授权的文件、存储、网络和插件能力。

## 可以用来做什么

| 用途 | 你可以做什么 |
| --- | --- |
| 随手记录和编辑文本 | 打开内置的「笺 · 私有便签」，记录待办、命令或临时文本。便签会保存私有草稿，也可以打开和另存本地文本文件。 |
| 整理工作区 | 用内置的「文件」浏览瑶光台工作区、新建文件夹、重命名和复制文件，也可以在 Windows 资源管理器中打开工作区。 |
| 集中运行小工具 | 安装 `.ypkg` 应用后，从桌面图标或应用管理打开。可以拖动、缩放、最小化窗口，并通过 Dock 在运行的应用之间切换。 |
| 管理应用和插件 | 在应用管理中查看发布者和权限，安装、更新、恢复应用的上一版本或卸载。插件管理提供独立的安装、更新和卸载入口。 |
| 把前端项目做成桌面应用 | 在「工具 → 打包工作台」创建或恢复本地发布者，选取构建目录，填写应用信息、上传图标并生成签名包。常用字段直接填表，高级打包提供 JSON 编辑入口。 |
| 开发接口和设备工具 | 参考「航标 · 请求台」调用已授权的 HTTP(S) 端点，或用「接点」配合串口插件枚举端口、发送文本和查看接收字节。示例需要单独打包安装。 |

桌面可以自定义壁纸、配色和时钟。点击顶部关闭按钮时，可以选择最小化到任务栏或退出，并记住选择。「能力查看器」随桌面内置，用来查看运行时和当前可用能力。

## 安装和开始使用

适用平台为 **Windows 10 / 11 x64**。当前交付为未签名开发包，打开时 Windows 可能提示未知发布者。安装包和 SHA-256 校验文件由构建脚本生成在 `build/installer/`。

1. 运行 `Alkaid-Setup-x64.exe`，分别选择程序目录和数据目录。两个目录需分开，数据目录应可写。
2. 打开瑶光台，双击桌面上的「笺 · 私有便签」或「文件」，即可体验内置应用。
3. 有应用包时，进入「应用管理 → 安装应用」，选中 `.ypkg`，核对发布者和权限后确认安装。

覆盖安装会沿用原数据位置，卸载瑶光台会保留数据目录。默认程序位置为 `%LOCALAPPDATA%\Programs\Alkaid`，默认数据位置为 `%LOCALAPPDATA%\Alkaid\Data`。

Windows 安装包的签名状态与 `.ypkg` 签名分别管理。打包工作台生成的应用和插件包仍使用本地发布者签名，安装时由瑶光台验签。

## 开发应用和插件

- **前端应用**通过 SDK 调用宿主。平台提供应用私有存储、用户选定文件的读写、受授权的网络请求，以及应用关闭前的保存协商。每个应用有独立的资源来源和数据空间。
- **原生插件**通过独立工作进程提供扩展能力。应用在清单中声明插件依赖和调用权限，宿主按授权和绑定版本调用。
- **示例源码**位于 `samples/notes`（便签）、`samples/network`（请求台）、`samples/serial`（接点）和 `plugins/serial`（串口插件）。本机 HTTP 回环与串口枚举已有验证记录。公网请求、受信 TLS 和串口真机收发的验收范围见开发进度。

已安装版本使用打包工作台生成 `.ypkg` 时，需要让 Node.js 和 Python 在系统 `PATH` 中可用。

[SDK 使用说明](packages/sdk/README.md) · [YPKG v3 与核心契约](z-doc/M1-核心契约-v1.md) · [私有存储与文件接口](z-doc/私有存储与文本文件接口.md)

## 从源码构建

宿主使用 C++20 和 CEF，应用及发布者登记使用 SQLite。源代码构建需要 Visual Studio 2022 C++ 工具链、CMake、Node.js 和 Python。桌面源码位于同级目录 `../desktop/`。

在本目录运行：

```powershell
python scripts/prepare-cef.py
python scripts/prepare-package-deps.py
npm --prefix packages/sdk ci
.\scripts\build-host.ps1
```

运行 `build/host/run/alkaid_host.exe` 可启动开发版。运行目录中的 DLL、网页和 CEF 资源需要一起保留。开发运行目录默认使用 `build/host/run/data-v3/`。调试端口默认关闭，传入 `--alkaid-debug` 才会开启本机 9227 端口。

构建 Windows 安装包时，准备 Inno Setup 的 `ISCC.exe` 和 Visual C++ x64 可再发行运行库目录，再执行：

```powershell
.\scripts\build-installer.ps1 -IsccPath '<ISCC.exe 路径>' -CrtDirectory '<VC x64 运行库目录>'
```

仅构建和测试运行核心时，可使用 `cmake --preset Core`、`cmake --build build/core --config Release` 和 `ctest --test-dir build/core -C Release --output-on-failure`。

| 路径 | 内容 |
| --- | --- |
| `apps/host/` | Windows 宿主、CEF 窗口与桌面桥接 |
| `core/`、`contracts/` | 运行核心与共享调用契约 |
| `infra/` | 包管理、数据存储和网络代理 |
| `packages/sdk/` | 前端 SDK |
| `samples/`、`plugins/` | 示例应用和原生插件 |
| `../desktop/` | 桌面界面源码 |
| `scripts/`、`tools/installer/` | 构建、打包、安装程序与回归验证 |

[项目规划与开发计划](z-doc/项目规划与开发计划-v3.md) · [重构开发进度](z-doc/重构开发进度.md)

# YPKG v2 当前实现规范

对应运行时 `2.0.0-dev.5`。这是已实施的开发预览版规范；目标方案中的扩展字段不能直接当作当前支持能力。

后续包规范、打包工具和插件交付的规划见 [项目规划与开发计划 v3](项目规划与开发计划-v3.md)。本文描述 dev.5 的已验收格式，当前 dev.6 网络扩展的开发状态见 [重构开发进度](重构开发进度.md)。下一版清单与兼容规则将在 M1 定稿。

## 前端与平台边界

前端只请求选包、展示已验证信息、收集确认并读取任务状态。系统文件选择器、读取源包、验签、信任记录、版本写入、SQLite 激活、更新和卸载均由宿主执行。网页不能向安装接口提交任意本机路径，也不能自行声明平台管理身份。

打包和签名在开发侧进行。运行时不生成发布者密钥，不保存发布者私钥，也不提供执行包内 EXE、DLL 或 shell 的能力。

## 容器与清单

`.ypkg` 是 ZIP 容器，支持普通文件的 STORE / DEFLATE。容器只包含以下三份元数据和资源，不写入目录条目：

```text
manifest.json
integrity.json
signature.json
assets/index.html
assets/其余静态资源
```

`manifest.json` 必须包含且只包含以下字段。开发侧模板可以省略 `publisherKeyId`，打包工具会从私钥对应公钥计算并填入。

```json
{
  "manifestVersion": 2,
  "appId": "com.example.inspector",
  "name": "独立应用示例",
  "version": "1.0.0",
  "publisherKeyId": "sha256:公钥的64位小写十六进制摘要",
  "runtime": { "apiMajor": 1, "minVersion": "2.0.0" },
  "entry": "index.html",
  "instanceMode": "single",
  "permissions": []
}
```

当前固定入口为 `index.html`、单实例、协议主版本为 1。版本采用三个无前导零的非负整数组成的 `major.minor.patch`，不支持预发布或构建后缀。`minVersion` 不能高于运行时的兼容基线 `2.0.0`。`appId` 使用小写点分段标识，不得使用平台的 `org.alkaid.*` 命名空间。

权限数组可为空，或包含不重复的 `storage.kv`、`files.private`、`files.userSelected.read`、`files.userSelected.write`。安装预览展示全部权限和新增授权；未知权限、重复项及管理权限均拒绝。权限与激活版本原子提交，更新也须确认；显式回退不得扩权，自动恢复沿用当前授权上限。网络代理尚未接入。具体能力见 [私有存储与文本文件接口](私有存储与文本文件接口.md)。窗口配置、自定义入口、SPA 路径回退和网络声明尚未支持，hash 路由可以由前端自行使用。

完整性清单包含每一份资源，不允许遗漏、额外文件或重复条目：

```json
{"files":[{"path":"assets/index.html","size":1234,"sha256":"资源原始字节的SHA256"}]}
```

所有 JSON 必须是有效 UTF-8，拒绝重复键和超过 16 层的嵌套。元数据每份最多 256 KiB；包最多 32 MiB；最多 512 份资源；单文件最多 8 MiB；解压后总量最多 32 MiB；单条目压缩比最多 200 倍。路径限制为 ASCII 字母、数字、`_ - . /`，总长最多 240 字符。拒绝绝对路径、`..`、盘符、UNC、备用数据流、Windows 设备名、尾部点、链接、重解析条目、重复路径和大小写冲突。

资源扩展名目前允许 HTML、CSS、JS/MJS、JSON、TXT、SVG、PNG、JPG/JPEG、GIF、WEBP、ICO、WOFF/WOFF2、MAP。缺失资源返回 404，不把缺失脚本退回首页。

## 签名与信任

签名输入精确为：

```text
ASCII("YPKG-V2\n")
+ uint32LE(manifest 原始 UTF-8 字节长度) + manifest 原始字节
+ uint32LE(integrity 原始 UTF-8 字节长度) + integrity 原始字节
```

算法为 ECDSA P-256 / SHA-256。`signature.json` 为：

```json
{"algorithm":"ECDSA-P256-SHA256","publicKey":"04加上X和Y坐标的130位小写十六进制","signature":"IEEE-P1363格式r和s的128位小写十六进制"}
```

公钥指纹是这 65 字节未压缩公钥的 SHA-256，前缀为 `sha256:`。运行时使用 Windows CNG 验签。签名清单按原始字节签署，不依赖跨语言 JSON 重排。

包自带公钥不自动代表受信任。未知发布者必须在桌面核对完整指纹并勾选确认；信任与应用激活在同一 SQLite 事务提交。更新和卸载后重新安装均要求保持原发布者绑定，不能用另一个密钥接管同名应用的数据。同一应用版本对应不同包摘要时拒绝安装，降级需使用显式的“恢复”操作。

目前没有发布者吊销管理界面、密钥轮换协议或应用商店证书体系。

## 持久化与恢复

默认保存于程序目录下的 `data/`：

```text
packages/registry.sqlite              唯一激活注册表、信任与操作记录
packages/store.lock                   防止多个运行时/工具并发维护同一目录
packages/staging/<operationId>/package.ypkg
packages/versions/<sha256>/package.ypkg
profiles/a-<应用ID摘要前32字符>/        应用自己的浏览器数据
private/a-<应用ID摘要前32字符>/content.sqlite  宿主管理的私有 KV 和文本
```

当前保存不可变原始包，安装和宿主启动时校验并解码成只读内存资源。页面请求只查已验证的资源表，不重新解析 ZIP，也不由 URL 拼接本机路径。这样避免了 ZIP 文件系统解压攻击面；按需解压、大资源流式读取与 Range 支持留待后续实现。

安装状态为：原生选择 → 后台验证 → 等待确认 → 持久化 `prepared` → 写入并刷新暂存包 → 同卷移动到版本目录 → 持久化 `ready` → SQLite 事务切换激活版本和信任 → `committed`。源包始终保留。SQLite 使用 WAL 与 `synchronous=FULL`。注册表 v2 新增 `apps.grants`；由 v1 增量迁移并保留现有记录，原先空权限应用授权为 `[]`。

重启时中断任务标记为失败，不激活未提交版本；未引用目录被清理，清理失败下次重试。已激活包损坏时，只有上一版本仍完整且发布者受信任，才恢复上一版本；否则应用显示“不可用”。这不是页面健康检查：脚本错误、SDK 握手超时等触发的自动回退尚未实现。

更新、恢复、卸载都要求应用先关闭，提交期间禁止重启该应用。更新保留上一版本；显式恢复可切换两份已验证版本。卸载取消激活并清理无引用程序包，保留应用数据、发布者绑定和历史版本摘要。当前没有删除应用数据的选项。

开发预览版最多激活 16 个安装应用，启动预加载总预算为 256 MiB，超过预算的应用标为不可用。没有宣称通过低磁盘空间、断电、所有杀毒软件和多设备环境验收；已验证的是本机进程强制中断恢复。

## SDK 工作流

这些接口只对宿主授予的平台管理角色开放，且要求 `apps.manage`：

```js
const { taskId } = await client.apps.prepareInstall(); // 原生选包；立即返回任务
const task = await client.apps.getTask(taskId);
// status === 'awaitingConsent' 后展示 task.preview，并由用户确认。
await client.apps.confirmInstall(taskId, true); // true 仅在用户确认信任后传入
await client.apps.cancelInstall(taskId);        // 只允许在待确认阶段取消
await client.apps.listTasks();                 // 最近32项任务；可恢复待确认界面
await client.apps.uninstall(appId);            // 返回另一个任务标识，默认保留数据
await client.apps.rollback(appId);             // 返回任务标识，恢复上一版本
```

任务状态为 `running / awaitingConsent / succeeded / failed / cancelled`，包含 `phase` 和中文 `message`；失败带结构化 `error`。提交阶段不能取消。收到 taskId 不是安装成功，必须查询到 `succeeded`。重复确认同一安装任务返回同一 taskId，不重复写入。已提交或中断的操作记录跨重启保留；未确认预览只保存在内存中，重启后需重新选包。

## 开发侧使用

```powershell
python scripts/prepare-package-deps.py
node scripts/ypkg.mjs keygen build/dev-keys/publisher.private.pem
node scripts/ypkg.mjs pack samples/inspector/manifest.json build/host/web/samples/inspector build/dev-keys/publisher.private.pem build/my-app.ypkg
build/host/infra/package/Release/alkaid_package_tool.exe verify build/my-app.ypkg
```

打包使用 Node 的签名接口和 Python 标准库 ZIP 写入器，运行时不依赖 Node 或 Python。`keygen` 和 ZIP 输出均拒绝覆盖已有文件。私钥只留在开发目录，不放入 `run/` 或应用包；版本管理忽略 `*.private.pem`。

本次提供 `build/examples/inspector-1.0.0.ypkg` 与 `inspector-1.1.0.ypkg`，对应指纹在同目录 `publisher-fingerprint.txt`。它们使用本机开发密钥，供安装、更新、恢复和卸载体验，不是发布者认证证书。

独立注册表工具支持 `list/inspect/install/uninstall/rollback`，要求显式传入数据目录。它不能与宿主同时操作同一目录；正常使用走桌面确认流程。工具的故障阶段参数供隔离测试使用，不进入宿主接口。

## 验证入口

- `node scripts/test-packages.mjs`：真实 C++ 验签、恶意包、发布者和版本约束、四阶段进程中断及内容损坏恢复。
- `scripts/test-package-ui.mjs`：需要显式调试宿主、隔离测试目录和 `build/package-ui-verification/current.json`；通过 `select-test-package.ps1` 操作该测试实例的原生文件选择器。
- `--alkaid-debug --alkaid-data-dir=<绝对路径>`：开发验证使用独立数据目录。未开启调试模式时忽略数据目录覆盖参数，默认启动不监听调试端口。

依赖版本、来源与许可证见 `third_party/package/NOTICE.md`；正式发布前还需完成包解析模糊测试、更多恶意归档测试、页面故障恢复、权限扩展和系统兼容性验收。

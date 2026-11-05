# 瑶光台 SDK

SDK 负责请求关联、超时、结果校验和文档销毁清理。系统能力由 C++ 宿主执行；普通浏览器没有运行时，调用明确失败。

当前开发版本为 `2.0.0-dev.5`，协议版本为 `1`。构建时由 `scripts/build-desktop.py` 编译 SDK 并随应用交付；尚未发布 npm 包。方法目录由 `contracts/runtime-api.json` 生成，参数与结果由 SDK 和宿主分别校验。

```js
import { AlkaidClient } from './sdk/index.js';
import { createCefTransport } from './src/bridge.js';

const client = new AlkaidClient(createCefTransport());
const info = await client.runtime.getInfo();
const capabilities = await client.runtime.getCapabilities();
addEventListener('pagehide', () => client.dispose(), { once: true });
```

## 已实现接口

| 方法 | 参数 | 结果 | 调用权限 |
|---|---|---|---|
| `runtime.getInfo()` | 无 | `protocolVersion`、`runtimeVersion`、`appId` | 当前受信应用 |
| `runtime.getCapabilities()` | 无 | 当前实际可用方法名称数组 | 当前受信应用 |
| `apps.list()` | 无 | `InstalledApp[]` | 平台管理角色及 `apps.manage` |
| `apps.launch(appId)` | 目标应用 ID | `InstalledApp` | 平台管理角色及 `apps.manage` |
| `apps.focus(appId)` | 目标应用 ID | `InstalledApp` | 平台管理角色及 `apps.manage` |
| `apps.close(appId)` | 目标应用 ID | `InstalledApp` | 平台管理角色及 `apps.manage` |
| `apps.prepareInstall()` | 无，打开原生选包器 | `TaskRef` | 平台管理角色及 `apps.manage` |
| `apps.confirmInstall(taskId, trustPublisher=false)` | 预览任务与信任确认 | `TaskRef` | 平台管理角色及 `apps.manage` |
| `apps.cancelInstall(taskId)` | 待确认任务 | `TaskRef` | 平台管理角色及 `apps.manage` |
| `apps.getTask(taskId)` | 任务 ID | `PackageTask` | 平台管理角色及 `apps.manage` |
| `apps.listTasks()` | 无 | 最近 32 项 `PackageTask[]` | 平台管理角色及 `apps.manage` |
| `apps.uninstall(appId, { clearData }?)` | 已关闭的安装应用。默认保留数据，显式启用后先清理私有数据和应用配置，再卸载 | `TaskRef` | 平台管理角色及 `apps.manage` |
| `apps.clearData(appId)` | 已关闭的安装应用，清理私有键值、私有文件与应用配置 | `TaskRef` | 平台管理角色及 `apps.manage` |
| `apps.clearBrowserData(appId)` | 已关闭的安装应用，登记其 CEF 配置目录清理。重启后执行，完成前应用暂停启动 | `TaskRef` | 平台管理角色及 `apps.manage` |
| `apps.rollback(appId)` | 已关闭的安装应用 | `TaskRef` | 平台管理角色及 `apps.manage` |
| `runtime.exit()` | 无 | `null`，已受理退出协商；可能被应用取消 | 平台管理角色及 `platform.exit` |

```ts
interface InstalledApp {
  appId: string;
  name: string;
  version: string;
  kind: 'desktop' | 'bundled' | 'installed';
  state: 'stopped' | 'starting' | 'running' | 'closing' | 'unavailable';
  canRollback?: boolean;
  error?: string;
}
```

普通应用默认可查询自身运行信息和能力；声明并获批存储/文件权限后可使用相应接口，不会因为传入桌面的 `appId` 而获得管理权限。`apps.*` 参数中的 `appId` 只是被操作的目标；调用者身份来自宿主绑定的 browser/frame 与当前文档会话。信封不允许额外的身份字段，目标参数不允许附带路径、角色或其他字段。

## 生命周期语义

- 启动是异步的，返回 `starting` 表示已受理，不能把它显示成已经运行。只有真实浏览器创建回调才进入 `running`。
- 同一应用只允许一个窗口：启动中重复打开复用这次启动；运行中重复打开显示并聚焦已有窗口。
- 聚焦尚未运行的应用返回 `APP_NOT_RUNNING`。关闭中的应用不能重启，返回 `APP_BUSY`。
- 关闭未运行的应用是幂等操作。其他状态关闭后返回 `closing`，直到 CEF 关闭回调才成为 `stopped`。
- 应用登记 `window.onCloseRequested` 后，原生关闭、管理关闭和平台退出先交给前端保存/确认；返回 true 才同意。返回 false、异常或 30 秒不响应均保留窗口。未登记的旧应用直接关闭。
- 关闭普通应用不退出平台。关闭桌面或调用 `runtime.exit()` 会询问所有运行应用，全部同意后才关闭窗口；任一应用取消，则已经同意的应用也会收到 AbortSignal 并恢复交互。退出协商期间拒绝新启动和包修改操作。
- 重启平台不自动重开普通应用，但保留各自的浏览器 profile。私有 KV 和文本数据由独立宿主数据库持久化，文件句柄不跨文档或进程恢复。
- 桌面通过一秒轮询 `apps.list()` 同步列表。关闭协商使用独立的生命周期接口，配置提交通过 `client.events.subscribe()` 订阅。

目标应用不存在返回 `APP_NOT_FOUND`，对桌面使用普通应用管理操作返回 `PROTECTED_APP`，权限不足返回 `PERMISSION_DENIED`，平台退出中返回 `RUNTIME_STOPPING`。接口调用完成不保证未来状态不变，UI 以重新取得的状态为准。

## 本地验证

安装接口立即返回任务标识，不代表安装已经成功。`getTask` 返回 `running / awaitingConsent / succeeded / failed / cancelled` 和中文阶段描述；待确认时附带宿主验证过的应用版本、发布者指纹与权限预览。未知发布者默认不信任，只有用户明确确认后才能传入 `trustPublisher=true`。包任务不接受网页传入本机路径；提交阶段不能取消。完整包格式、任务及恢复语义见 [YPKG v2 实现规范](../../z-doc/YPKG-v2实现规范.md)。

```powershell
node --test packages/sdk/test/client.test.ts packages/sdk/test/streams.test.ts tests/desktop/bridge.test.mjs
# 先以 --alkaid-debug 启动构建出的宿主。
node scripts/test-host.mjs build/app-verification/basic
node scripts/test-app-runtime.mjs build/app-verification --exit
```

真实窗口测试覆盖重复启动、权限拒绝、资源与数据隔离、刷新重连、独立关闭、关闭中交错操作和统一退出。`--seed-storage` 与下次宿主启动后的 `--expect-storage` 配合验证跨进程持久性；`--native-exit` 用桌面窗口关闭路径验证统一退出。

## 配置

`client.config.app` 提供 `get`、`set`、`remove` 和事务式 `batch`。作用域由宿主绑定为当前应用，参数中无需填写应用 ID。受信桌面可使用同样的 `client.config.platform` 接口访问平台配置。两个作用域各自持久化，与 `client.storage` 私有数据分开。

```js
await client.config.app.set('appearance', { accent: 'forest' });
const { found, value } = await client.config.app.get('appearance');
```

`found` 区分缺失配置和保存的 JSON `null`。单个 JSON 值最多 64 KiB，批处理最多 64 项，任一操作失败会回滚整批。

用 `client.events.subscribe('config.app.changed', listener)` 订阅当前应用的配置提交。受信桌面也可订阅 `config.platform.changed`。监听器接收本次提交涉及的键名数组，可按需重新读取最新值。批处理提交一次通知，失败时没有通知。订阅返回异步退订函数，文档导航或关闭时由宿主清理。

```js
const unsubscribe = await client.events.subscribe('config.app.changed', keys => {
  if (keys.includes('appearance')) refreshAppearance();
});
await unsubscribe();
```

## 插件调用

`client.plugin.call(pluginId, method, params, { timeoutMs, signal })` 提交 JSON 参数。宿主依据当前文档所属应用核对插件依赖及安装授权。缺少任一项返回 `PERMISSION_DENIED`。有绑定版本时，宿主在首次调用启动工作进程。进程不可用时返回 `PLUGIN_UNAVAILABLE`。默认超时 5 秒，最长 30 秒，可用 `AbortSignal` 取消等待。在途请求超时或取消时，宿主给插件 300 毫秒确认收尾。已确认的进程可复用，超时未确认的进程会被回收。

`client.plugin.on(pluginId, event, listener)` 订阅绑定版本的签名清单中声明的事件，返回异步退订函数。监听器收到事件的 JSON 数据。插件实例由首次方法调用启动，启动后可以在调用期间及空闲时主动发送事件。调用期间每次调用最多读取 32 条事件，空闲读取由宿主后台调度。

## 私有存储与文本文件

新增 `storage.get/set/remove/batch`、`files.readPrivateText/writePrivateText/removePrivate`、`dialog.openFile/saveFile`、`files.readText/writeText/close`。参数、返回值、权限、配额和取消语义见 [完整接口说明](../../z-doc/私有存储与文本文件接口.md)。

原生选择器取消返回 `null`；打开授予只读句柄，保存授予可写句柄，两者都不返回路径。选择器等待上限为 5 分钟，与普通调用的 15 秒超时独立。`storage.get` 返回 `{found,value}`，私有文本不存在返回 `null`。批处理失败整批回滚，读写成功才更新界面成功状态。

安装清单必须声明使用的权限；安装确认展示本次新增授权。可安装示例源码为 `samples/notes/`，交付包为 `build/examples/notes-1.1.0.ypkg`。


## 分块文件与编码

`readText`、`writeText` 和私有文本方法保持原调用形式，但新版 SDK 内部自动分块。`readText(handle, {encoding: 'auto', maxBytes, signal, onProgress})` 默认整文件预算 16 MiB，可设为 0～64 MiB；预算同时约束源字节和解码后的 UTF-8 字节，不是实际进程内存总量。更大文件使用 `readTextChunks` 或 `readChunks` 逐块消费。`readTextChunks` 的 `maxBytes` 不参与限制；逐块调用的内存由消费者控制。

`writeText` 支持 `utf-8 / utf-8-bom / utf-16le / utf-16be / gb18030`；默认 UTF-8。`auto` 仅用于读取：识别 Unicode BOM，否则按 UTF-8 严格解码，GB18030 需显式选择。`readBytes/writeBytes` 处理 Uint8Array，`readChunks/writeChunks` 处理异步迭代字节流；不把二进制转成文本。

```js
const source = await client.dialog.openFile();
const target = source && await client.dialog.saveFile();
try {
  if (source && target) {
    const controller = new AbortController(); // 取消按钮调用 controller.abort()
    await client.files.writeChunks(target.handle,
      client.files.readChunks(source.handle, { signal: controller.signal }),
      { signal: controller.signal, onProgress: updateProgress });
  }
} finally {
  if (source) await client.files.close(source.handle);
  if (target) await client.files.close(target.handle);
}
```

每个数据块都等待宿主确认再发下一块，避免同时堆积整个文件。`onProgress` 为 `{processedBytes,totalBytes,encoding?}`：读取按源字节计数；文本写入按输入 UTF-8 字节计数，不等于转换后的磁盘大小。未知长度的字节写入 `totalBytes=0`。取消在块间与提交前生效；已提交的写入不能撤销。无论取消、源迭代失败还是提前结束读取，SDK 都释放流。配额和底层 RPC 见 [文件接口](../../z-doc/私有存储与文本文件接口.md)。

## 关闭前保存

```js
const stopHandlingClose = await client.window.onCloseRequested(async ({reason, signal}) => {
  lockEditor();
  signal.addEventListener('abort', unlockEditor, {once: true});
  try {
    await flushPrivateDraft();
    if (signal.aborted) return false;
    const allow = await showCloseChoice({reason, signal});
    if (!allow) unlockEditor();
    return allow;
  } catch (error) {
    showSaveError(error);
    unlockEditor();
    return false;
  }
});
// 仅在不再负责保护本页编辑内容时调用：await stopHandlingClose();
```

`reason` 为 `window / manager / platform`。返回 true 后保持编辑冻结，直到窗口关闭或 signal 被取消；不能把“自己同意了”当成全平台已经退出。signal 取消时还应关闭确认 UI、终止未提交操作并恢复编辑。处理器抛错时 SDK 自动拒绝关闭，界面错误展示由应用负责。每个文档使用一个长期持有的 AlkaidClient，销毁时 dispose；不要为每次操作创建会登记关闭处理器的临时客户端。

不依赖 `beforeunload` 保存：协商获准后宿主完成实际关闭，忽略额外的 beforeunload 拦截。退出返回 null 仅表示请求受理；桌面使用 `runtime.onExitCancelled(message => showMessage(message))` 展示取消原因。原生窗口和平台关闭测试入口为 `scripts/verify-stream-ui.ps1`。

CEF 已进入销毁阶段的强制浏览器关闭（例如调试器 Page.close，或页面直接 window.close）不能再撤回，宿主会完成销毁，避免半关闭状态；若桌面已被销毁则结束整个平台。应用应使用正常原生窗口关闭路径，保存协商不等于拦截调试器强制关闭、任务管理器结束进程或系统断电。

/** 瑶光台前端 SDK：传输、超时和生命周期管理，不执行本机系统操作。 */
import { contract, type MethodName } from './method_catalog.ts';
import { AlkaidError } from './errors.ts';
import { FileApi } from './file_client.ts';
import { NetworkApi, validEndpoint } from './network_client.ts';
export type { NetworkEndpoint, NetworkOptions, NetworkStream, NetworkResponse } from './network_client.ts';
export { AlkaidError } from './errors.ts';
export type { TextEncoding, TransferOptions, ReadTextOptions, WriteTextOptions } from './file_client.ts';

let clientSequence = 0;

/** 由 Renderer 适配层提供；不得在普通浏览器中模拟系统调用成功。 */
export interface BridgeTransport {
  send(message: string): void;
  cancel?(requestId: string): void;
  subscribe(listener: (message: string) => void): () => void;
}

export interface RuntimeInfo {
  protocolVersion: number;
  runtimeVersion: string;
  appId: string;
}

export interface InstalledApp {
  appId: string;
  name: string;
  version: string;
  kind: 'desktop' | 'bundled' | 'installed';
  state: 'stopped' | 'starting' | 'running' | 'closing' | 'unavailable';
  canRollback?: boolean;
  pluginBindings?: Record<string, string>;
  publisherName?: string;
  icon?: string;
  publisherKeyId?: string;
  declaredPermissions?: string[];
  grantedPermissions?: string[];
  networkEndpoints?: import('./network_client.ts').NetworkEndpoint[];
  error?: string;
  browserCleanupPending?: boolean;
  browserCleanupFailed?: boolean;
  workspaceActive?: boolean;
  workspaceBounds?: { x: number; y: number; width: number; height: number; minWidth: number; minHeight: number; resizable: boolean };
}
export interface TaskRef { taskId: string; }
export interface AuditEntry { time: number; operation: string; appId: string; target: string; code: string; }
export type JsonValue = null | boolean | number | string | JsonValue[] | { [key: string]: JsonValue };
export interface StorageValue { found: boolean; value: JsonValue; }
export interface FileGrant { handle: string; name: string; writable: boolean; }
export type StorageOperation = { type: 'set'; key: string; value: JsonValue } | { type: 'remove'; key: string };
export interface CloseRequest { reason: 'window' | 'manager' | 'platform'; signal: AbortSignal; }
export interface PackagePreview {
  appId: string; name: string; version: string; publisherKeyId: string; publisherTrusted: boolean;
  currentVersion: string; permissions: string[]; fileCount: number; packageBytes: number;
  addedPermissions?: string[];
  networkEndpoints?: import('./network_client.ts').NetworkEndpoint[];
}
export interface PackageTask extends TaskRef {
  kind: 'install' | 'uninstall' | 'rollback' | 'pack' | 'pluginInstall' | 'pluginUninstall' | 'permissions' | 'clearData' | 'clearBrowserData' | 'publisherBackup';
  appId: string;
  status: 'running' | 'awaitingInput' | 'awaitingConsent' | 'succeeded' | 'failed' | 'cancelled';
  phase: string;
  message: string;
  packageName?: string;
  packageVersion?: string;
  publisherName?: string;
  sourceName?: string;
  outputPath?: string;
  preview?: PackagePreview | PluginPackagePreview;
  error?: { code: string; message: string };
}
export interface InstalledPlugin {
  pluginId: string; name: string; version: string; publisherKeyId: string; error: string;
}
export interface PluginPackagePreview {
  type: 'plugin'; pluginId: string; name: string; version: string; publisherKeyId: string; publisherTrusted: boolean;
  currentVersion: string; capabilities: string[]; events: string[]; fileCount: number; packageBytes: number;
}

interface PendingCall {
  resolve(value: unknown): void;
  reject(error: AlkaidError): void;
  timer: ReturnType<typeof setTimeout>;
  validate(value: unknown): boolean;
  cleanup?: () => void;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function validInfo(value: unknown): boolean {
  return isRecord(value) && value.protocolVersion === contract.protocolVersion &&
    typeof value.runtimeVersion === 'string' && typeof value.appId === 'string';
}

function validApp(value: unknown): boolean {
  return isRecord(value) && typeof value.appId === 'string' && typeof value.name === 'string' &&
    typeof value.version === 'string' && ['desktop', 'bundled', 'installed'].includes(value.kind as string) &&
    ['stopped', 'starting', 'running', 'closing', 'unavailable'].includes(value.state as string) &&
    (value.canRollback === undefined || typeof value.canRollback === 'boolean') &&
    (value.pluginBindings === undefined || (isRecord(value.pluginBindings) &&
      Object.values(value.pluginBindings).every(version => typeof version === 'string'))) &&
    ['publisherName', 'publisherKeyId', 'icon'].every(key => value[key] === undefined || typeof value[key] === 'string') &&
    ['declaredPermissions', 'grantedPermissions'].every(key => value[key] === undefined ||
      (Array.isArray(value[key]) && value[key].every(permission => typeof permission === 'string'))) &&
    (value.networkEndpoints === undefined || (Array.isArray(value.networkEndpoints) && value.networkEndpoints.every(validEndpoint))) &&
    (value.error === undefined || typeof value.error === 'string');
}
function validTaskRef(value: unknown): boolean {
  return isRecord(value) && typeof value.taskId === 'string' && /^[a-f0-9]{64}$/.test(value.taskId);
}
function validTask(value: unknown): boolean {
  if (!isRecord(value) || !validTaskRef(value) || !['install', 'uninstall', 'rollback', 'pack', 'pluginInstall', 'pluginUninstall', 'permissions', 'clearData', 'clearBrowserData', 'publisherBackup'].includes(value.kind as string) ||
      !['running', 'awaitingInput', 'awaitingConsent', 'succeeded', 'failed', 'cancelled'].includes(value.status as string) ||
      typeof value.appId !== 'string' || typeof value.phase !== 'string' || typeof value.message !== 'string') return false;
  if (value.error !== undefined && (!isRecord(value.error) || typeof value.error.code !== 'string' || typeof value.error.message !== 'string')) return false;
  if (value.status === 'awaitingConsent' && value.preview === undefined) return false;
  if (value.preview !== undefined) {
    const p = value.preview;
    if (value.kind === 'pluginInstall') {
      return isRecord(p) && p.type === 'plugin' &&
        ['pluginId','name','version','publisherKeyId','currentVersion'].every(key => typeof p[key] === 'string') &&
        typeof p.publisherTrusted === 'boolean' && Array.isArray(p.capabilities) && p.capabilities.every(x => typeof x === 'string') &&
        Array.isArray(p.events) && p.events.every(x => typeof x === 'string') &&
        Number.isSafeInteger(p.fileCount) && Number.isSafeInteger(p.packageBytes);
    }
    if (!isRecord(p) || !['appId','name','version','publisherKeyId','currentVersion'].every(key => typeof p[key] === 'string') ||
        typeof p.publisherTrusted !== 'boolean' || !Array.isArray(p.permissions) || !p.permissions.every(x => typeof x === 'string') ||
        (p.addedPermissions !== undefined && (!Array.isArray(p.addedPermissions) || !p.addedPermissions.every(x => typeof x === 'string'))) ||
        (p.networkEndpoints !== undefined && (!Array.isArray(p.networkEndpoints) || !p.networkEndpoints.every(validEndpoint))) ||
        !Number.isSafeInteger(p.fileCount) || !Number.isSafeInteger(p.packageBytes)) return false;
  }
  return true;
}
function validApps(value: unknown): boolean {
  return Array.isArray(value) && value.every(validApp);
}
function validGrant(value: unknown): boolean {
  return value === null || (isRecord(value) && typeof value.handle === 'string' && /^[a-f0-9]{64}$/.test(value.handle) &&
    typeof value.name === 'string' && typeof value.writable === 'boolean');
}
const validNull = (value: unknown): boolean => value === null;
export type ConfigurationEvent = 'config.app.changed' | 'config.platform.changed';

/** 每个文档只创建一个客户端，页面销毁时调用 dispose。 */
export class AlkaidClient {
  private readonly transport?: BridgeTransport;
  private readonly timeoutMs: number;
  // 请求标识只用于关联响应，不作为权限令牌；无安全上下文时仍能明确报告运行时不可用。
  private readonly prefix = globalThis.crypto?.randomUUID?.() ?? `client-${Date.now()}-${++clientSequence}`;
  private sequence = 0;
  private readonly pending = new Map<string, PendingCall>();
  private readonly unsubscribe?: () => void;
  private disposed = false;
  private closeHandler?: (request: CloseRequest) => Promise<boolean> | boolean;
  private readonly closeRequests = new Map<string, AbortController>();
  private readonly exitListeners = new Set<(message: string) => void>();
  private eventSequence = 0;
  private readonly eventListeners = new Map<string, { event: string; listener: (payload: Record<string, unknown>) => void }>();

  constructor(transport?: BridgeTransport, options: { timeoutMs?: number } = {}) {
    this.transport = transport;
    this.timeoutMs = options.timeoutMs ?? 15000;
    if (!Number.isInteger(this.timeoutMs) || this.timeoutMs <= 0 || this.timeoutMs > 15000) {
      throw new AlkaidError('INVALID_REQUEST', '请求超时必须为 1 至 15000 毫秒的整数');
    }
    this.unsubscribe = transport?.subscribe(message => this.receive(message));
  }

  readonly runtime = {
    getInfo: (): Promise<RuntimeInfo> => this.invoke('runtime.getInfo', validInfo),
    getCapabilities: (): Promise<string[]> => this.invoke('runtime.getCapabilities', value =>
      Array.isArray(value) && value.every(item => typeof item === 'string')),
    exit: (): Promise<null> => this.invoke('runtime.exit', value => value === null),
    onExitCancelled: (listener: (message: string) => void): (() => void) => {
      this.exitListeners.add(listener); return () => { this.exitListeners.delete(listener); };
    },
  };

  readonly apps = {
    list: (): Promise<InstalledApp[]> => this.invoke('apps.list', validApps),
    layoutWorkspace: (params: { appId: string; x: number; y: number; width: number; height: number;
      visible: boolean; focus: boolean; persist: boolean; minimize: boolean }): Promise<null> =>
      this.invoke('apps.layoutWorkspace', validNull, params),
    launch: (appId: string): Promise<InstalledApp> => this.appAction('apps.launch', appId),
    focus: (appId: string): Promise<InstalledApp> => this.appAction('apps.focus', appId),
    close: (appId: string): Promise<InstalledApp> => this.appAction('apps.close', appId),
    localPublisher: (name?: string): Promise<{ name: string; keyId: string } | null> =>
      this.invoke('apps.localPublisher', value => value === null ||
        (isRecord(value) && typeof value.name === 'string' && typeof value.keyId === 'string'),
      name === undefined ? {} : { name }),
    publisherBackup: (password: string, restore: boolean): Promise<TaskRef> =>
      this.invoke('apps.publisherBackup', validTaskRef, { password, restore }),
    preparePack: (manifest: string, localPublisher = false, interactive = false): Promise<TaskRef> =>
      this.invoke('apps.preparePack', validTaskRef, { manifest, localPublisher, ...(interactive ? { interactive: true } : {}) }),
    advancePack: (taskId: string): Promise<TaskRef> => this.invoke('apps.advancePack', validTaskRef, { taskId }),
    cancelPack: (taskId: string): Promise<TaskRef> => this.invoke('apps.cancelPack', validTaskRef, { taskId }),
    revealPack: (taskId: string): Promise<TaskRef> => this.invoke('apps.revealPack', validTaskRef, { taskId }),
    installPacked: (taskId: string): Promise<TaskRef> => this.invoke('apps.installPacked', validTaskRef, { taskId }),
    prepareInstall: (): Promise<TaskRef> => this.invoke('apps.prepareInstall', validTaskRef),
    confirmInstall: (taskId: string, trustPublisher = false): Promise<TaskRef> =>
      this.invoke('apps.confirmInstall', validTaskRef, { taskId, trustPublisher }),
    cancelInstall: (taskId: string): Promise<TaskRef> => this.invoke('apps.cancelInstall', validTaskRef, { taskId }),
    getTask: (taskId: string): Promise<PackageTask> => this.invoke('apps.getTask', validTask, { taskId }),
    listTasks: (): Promise<PackageTask[]> => this.invoke('apps.listTasks', value => Array.isArray(value) && value.every(validTask)),
    listAudit: (): Promise<AuditEntry[]> => this.invoke('apps.listAudit', value => Array.isArray(value) &&
      value.every(entry => isRecord(entry) && Number.isSafeInteger(entry.time) &&
        ['operation', 'appId', 'target', 'code'].every(key => typeof entry[key] === 'string'))),
    uninstall: (appId: string, options?: { clearData: boolean }): Promise<TaskRef> =>
      this.invoke('apps.uninstall', validTaskRef, options ? { appId, clearData: options.clearData } : { appId }),
    clearData: (appId: string): Promise<TaskRef> => this.invoke('apps.clearData', validTaskRef, { appId }),
    clearBrowserData: (appId: string): Promise<TaskRef> => this.invoke('apps.clearBrowserData', validTaskRef, { appId }),
    setPermissions: (appId: string, permissions: string[]): Promise<TaskRef> =>
      this.invoke('apps.setPermissions', validTaskRef, { appId, permissions }),
    rollback: (appId: string): Promise<TaskRef> => this.invoke('apps.rollback', validTaskRef, { appId }),
  };
  readonly plugins = {
    list: (): Promise<InstalledPlugin[]> => this.invoke('plugins.list', value => Array.isArray(value) &&
      value.every(plugin => isRecord(plugin) && ['pluginId','name','version','publisherKeyId','error'].every(key => typeof plugin[key] === 'string'))),
    prepareInstall: (): Promise<TaskRef> => this.invoke('plugins.prepareInstall', validTaskRef),
    uninstall: (pluginId: string): Promise<TaskRef> => this.invoke('plugins.uninstall', validTaskRef, { pluginId }),
  };

  readonly workspace = {
    list: (path: string): Promise<{ name: string; directory: boolean; bytes: number }[]> =>
      this.invoke('workspace.files', value => Array.isArray(value) && value.every(item =>
        isRecord(item) && typeof item.name === 'string' && typeof item.directory === 'boolean' && Number.isSafeInteger(item.bytes)),
      { operation: 'list', path }),
    change: (operation: 'mkdir' | 'rename' | 'copy', path: string, name: string): Promise<null> =>
      this.invoke('workspace.files', validNull, { operation, path, name }),
    reveal: (): Promise<null> => this.invoke('workspace.files', validNull, { operation: 'reveal', path: '' }),
  };
  readonly storage = {
    get: (key: string): Promise<StorageValue> => this.invoke('storage.get', value =>
      isRecord(value) && typeof value.found === 'boolean' && 'value' in value, { key }),
    set: (key: string, value: JsonValue): Promise<null> => this.invoke('storage.set', validNull, { key, value }),
    remove: (key: string): Promise<null> => this.invoke('storage.remove', validNull, { key }),
    batch: (operations: StorageOperation[]): Promise<null> => this.invoke('storage.batch', validNull, { operations }),
  };
  private configuration(scope: 'app' | 'platform') {
    return {
      get: (key: string): Promise<StorageValue> => this.invoke(`config.${scope}.get`, value =>
        isRecord(value) && typeof value.found === 'boolean' && 'value' in value, { key }),
      set: (key: string, value: JsonValue): Promise<null> => this.invoke(`config.${scope}.set`, validNull, { key, value }),
      remove: (key: string): Promise<null> => this.invoke(`config.${scope}.remove`, validNull, { key }),
      batch: (operations: StorageOperation[]): Promise<null> => this.invoke(`config.${scope}.batch`, validNull, { operations }),
    };
  }
  readonly config = { app: this.configuration('app'), platform: this.configuration('platform') };
  readonly events = {
    subscribe: async (event: ConfigurationEvent, listener: (keys: string[]) => void): Promise<() => Promise<void>> => {
      if (typeof listener !== 'function') throw new AlkaidError('INVALID_REQUEST', '事件监听器必须是函数');
      return this.subscribeEvent(event, payload => {
        if (Array.isArray(payload.keys) && payload.keys.every(key => typeof key === 'string')) listener(payload.keys);
      });
    },
  };
  /** 原生选择器可等待五分钟；取消返回 null，页面从不接触系统路径。打开只授予读取，保存授予写入。 */
  readonly dialog = {
    openFile: (): Promise<FileGrant | null> => this.invoke('dialog.openFile', validGrant, {}, 300000),
    saveFile: (): Promise<FileGrant | null> => this.invoke('dialog.saveFile', validGrant, {}, 300000),
  };
  readonly files = new FileApi(<T>(method: MethodName, validate: (value: unknown) => boolean, params: Record<string, unknown>) => this.invoke<T>(method,validate,params));
  readonly net = new NetworkApi(<T>(method: MethodName, validate: (value: unknown) => boolean, params: Record<string, unknown>, signal?: AbortSignal) => this.invoke<T>(method,validate,params,this.timeoutMs,signal));
  readonly plugin = {
    on: async (pluginId: string, event: string, listener: (data: JsonValue) => void): Promise<() => Promise<void>> => {
      if (typeof listener !== 'function' || typeof pluginId !== 'string' || !/^[a-z][a-z0-9-]*(\.[a-z][a-z0-9-]*)+$/.test(pluginId) ||
          typeof event !== 'string' || !/^[a-z][a-zA-Z0-9_]{0,63}$/.test(event))
        throw new AlkaidError('INVALID_REQUEST', '插件标识、事件名或监听器不合法');
      return this.subscribeEvent(`plugin.${pluginId}.${event}`, payload => {
        if ('data' in payload) listener(payload.data as JsonValue);
      });
    },
    call: (pluginId: string, method: string, params: JsonValue = null, options: { timeoutMs?: number; signal?: AbortSignal } = {}): Promise<JsonValue> => {
      const timeout = options.timeoutMs ?? Math.min(this.timeoutMs, 5000);
      if (!Number.isInteger(timeout) || timeout < 1 || timeout > 30000)
        return Promise.reject(new AlkaidError('INVALID_REQUEST', '插件调用超时必须为 1 至 30000 毫秒'));
      return this.invoke('plugin.call', value => value !== undefined, { pluginId, method, params }, timeout, options.signal);
    },
  };
  readonly window = {
    minimize: (): Promise<null> => this.invoke("window.minimize", validNull),
    toggleMaximize: (): Promise<null> => this.invoke("window.toggleMaximize", validNull),
    onCloseRequested: async (handler: (request: CloseRequest) => Promise<boolean> | boolean): Promise<() => Promise<void>> => {
      if (this.closeHandler) throw new AlkaidError('INVALID_REQUEST', '同一客户端只能登记一个关闭处理器');
      this.closeHandler = handler;
      try { await this.invoke('window.setCloseHandler', validNull, { enabled: true }); }
      catch (error) { this.closeHandler = undefined; throw error; }
      return async () => {
        if (this.closeHandler !== handler) return;
        await this.invoke('window.setCloseHandler', validNull, { enabled: false });
        this.closeHandler = undefined;
        for (const controller of this.closeRequests.values()) controller.abort();
        this.closeRequests.clear();
      };
    },
  };

  private async subscribeEvent(event: string, listener: (payload: Record<string, unknown>) => void): Promise<() => Promise<void>> {
    const subscriptionId = `${this.prefix}-event-${++this.eventSequence}`;
    this.eventListeners.set(subscriptionId, { event, listener });
    try { await this.invoke('events.subscribe', validNull, { event, subscriptionId }); }
    catch (error) {
      this.eventListeners.delete(subscriptionId);
      throw error;
    }
    return async () => {
      this.eventListeners.delete(subscriptionId);
      if (!this.disposed) await this.invoke('events.unsubscribe', validNull, { subscriptionId });
    };
  }

  private appAction(method: MethodName, appId: string): Promise<InstalledApp> {
    if (typeof appId !== 'string' || !appId.length || appId.length > 128) {
      return Promise.reject(new AlkaidError('INVALID_REQUEST', '应用标识必须为 1 至 128 个字符'));
    }
    return this.invoke(method, validApp, { appId });
  }

  /** 文档销毁会使全部 Promise 失败，避免一直等待已经不存在的页面。 */
  dispose(): void {
    if (this.disposed) return;
    if (this.closeHandler) void this.invoke('window.setCloseHandler', validNull, { enabled: false }).catch(() => {});
    this.closeHandler = undefined;
    for (const controller of this.closeRequests.values()) controller.abort();
    this.closeRequests.clear(); this.exitListeners.clear();
    for (const subscriptionId of this.eventListeners.keys())
      void this.invoke('events.unsubscribe', validNull, { subscriptionId }).catch(() => {});
    this.eventListeners.clear();
    this.disposed = true;
    this.failAll(new AlkaidError('SESSION_EXPIRED', '页面会话已经关闭'));
    this.unsubscribe?.();
  }

  private invoke<T>(method: MethodName, validate: (value: unknown) => boolean, params: Record<string, unknown> = {}, timeoutMs = this.timeoutMs, signal?: AbortSignal): Promise<T> {
    if (signal?.aborted) return Promise.reject(new AlkaidError('ABORTED', '系统调用已取消'));
    if (this.disposed) return Promise.reject(new AlkaidError('SESSION_EXPIRED', '页面会话已经关闭'));
    if (!this.transport) return Promise.reject(new AlkaidError('RUNTIME_UNAVAILABLE', '当前页面未连接瑶光台运行时'));
    if (this.pending.size >= contract.maxPendingPerSession) {
      return Promise.reject(new AlkaidError('RESOURCE_EXHAUSTED', '当前页面未完成请求过多'));
    }
    if (this.sequence === Number.MAX_SAFE_INTEGER) {
      return Promise.reject(new AlkaidError('RESOURCE_EXHAUSTED', '当前页面请求标识已耗尽'));
    }
    const requestId = `${this.prefix}-${++this.sequence}`;
    let message: string;
    try { message = JSON.stringify({ protocolVersion: contract.protocolVersion, requestId, method, params, timeoutMs }); }
    catch { return Promise.reject(new AlkaidError('INVALID_REQUEST', '参数必须可以序列化为 JSON')); }
    if (new TextEncoder().encode(message).byteLength > contract.maxPayloadBytes)
      return Promise.reject(new AlkaidError('PAYLOAD_TOO_LARGE', '请求报文超过大小限制'));
    return new Promise<T>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.take(requestId);
        this.transport?.cancel?.(requestId);
        reject(new AlkaidError('TIMEOUT', '系统调用超时，操作可能仍在执行'));
      }, timeoutMs);
      const cancel = () => {
        const call=this.take(requestId);
        if(call) {this.transport?.cancel?.(requestId);call.reject(new AlkaidError('ABORTED','系统调用已取消，已发送的网络操作不能撤销'));}
      };
      signal?.addEventListener('abort',cancel,{once:true});
      this.pending.set(requestId, { resolve: value => resolve(value as T), reject, timer, validate, cleanup:()=>signal?.removeEventListener('abort',cancel) });
      try {
        this.transport!.send(message);
      } catch {
        const call = this.take(requestId);
        call?.reject(new AlkaidError('TRANSPORT_ERROR', '无法向瑶光台发送请求'));
      }
    });
  }

  private take(requestId: string): PendingCall | undefined {
    const call = this.pending.get(requestId);
    if (call) {
      clearTimeout(call.timer);
      call.cleanup?.();
      this.pending.delete(requestId);
    }
    return call;
  }

  private receive(message: string): void {
    if (this.disposed) return;
    if (typeof message !== 'string' || new TextEncoder().encode(message).byteLength > contract.maxPayloadBytes) {
      this.failAll(new AlkaidError('INVALID_RESPONSE', '宿主响应超过限制或格式无效'));
      return;
    }
    let response: unknown;
    try { response = JSON.parse(message); }
    catch {
      this.failAll(new AlkaidError('INVALID_RESPONSE', '宿主响应不是有效的 JSON'));
      return;
    }
    if (isRecord(response) && typeof response.event === 'string') { this.event(response.event, response.payload); return; }
    if (!isRecord(response) || typeof response.requestId !== 'string') {
      this.failAll(new AlkaidError('INVALID_RESPONSE', '宿主响应缺少请求标识'));
      return;
    }
    const call = this.take(response.requestId);
    // 迟到、重复及其他文档的响应不得影响有效请求。
    if (!call) return;
    if (response.ok === true && !('error' in response) && call.validate(response.result)) {
      call.resolve(response.result);
    } else if (response.ok === false && !('result' in response) && isRecord(response.error) &&
        typeof response.error.code === 'string' && typeof response.error.message === 'string' &&
        (response.error.retryable === undefined || typeof response.error.retryable === 'boolean') &&
        (response.error.traceId === undefined || typeof response.error.traceId === 'string')) {
      call.reject(new AlkaidError(response.error.code, response.error.message,
        response.error.retryable === true, response.error.traceId as string | undefined));
    } else {
      call.reject(new AlkaidError('INVALID_RESPONSE', '宿主响应不符合接口契约'));
    }
  }

  private failAll(error: AlkaidError): void {
    for (const call of this.pending.values()) {
      clearTimeout(call.timer);
      call.cleanup?.();
      call.reject(error);
    }
    this.pending.clear();
  }
  private event(name: string, payload: unknown): void {
    if (!isRecord(payload)) return;
    if (typeof payload.subscriptionId === 'string') {
      const registered = this.eventListeners.get(payload.subscriptionId);
      if (registered?.event === name) registered.listener(payload);
      return;
    }
    if (name === 'runtime.exitCancelled' && typeof payload.message === 'string') {
      for (const listener of this.exitListeners) listener(payload.message);
      return;
    }
    const id = payload.requestId;
    if (typeof id !== 'string' || !/^[a-f0-9]{64}$/.test(id)) return;
    if (name === 'window.closeCancelled') {
      this.closeRequests.get(id)?.abort(); this.closeRequests.delete(id); return;
    }
    if (name !== 'window.closeRequested' || !['window', 'manager', 'platform'].includes(payload.reason as string) || !this.closeHandler || this.closeRequests.has(id)) return;
    const controller = new AbortController(); this.closeRequests.set(id, controller);
    const handler = this.closeHandler;
    void (async () => {
      let allow = false;
      try { allow = await handler({ reason: payload.reason as CloseRequest['reason'], signal: controller.signal }) === true; }
      catch { allow = false; }
      if (!controller.signal.aborted && !this.disposed) await this.invoke('window.resolveClose', validNull, { requestId: id, allow }).catch(() => {});
      // 同意后继续保留 signal：平台退出屏障被其他应用取消时，通知已同意的页面恢复交互。
    })();
  }
}

export { contract };

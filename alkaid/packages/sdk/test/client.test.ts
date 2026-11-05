/** SDK 传输契约测试，测试适配器不代表 CEF 已接入。 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { AlkaidClient, contract, type BridgeTransport } from '../src/index.ts';

test('文件选择器使用独立期限，取消返回空结果且不传入系统路径', async () => {
  const f = fixture(25);
  const pending = f.client.dialog.openFile();
  assert.equal(f.messages[0].timeoutMs, 300000);
  assert.deepEqual(f.messages[0].params, {});
  f.answer(0, null);
  assert.equal(await pending, null);
  f.client.dispose();
});

test('存储区分不存在和 JSON null，文件响应严格校验句柄', async () => {
  const f = fixture();
  const pending = f.client.storage.get('preferences');
  f.answer(0, { found: true, value: null });
  assert.deepEqual(await pending, { found: true, value: null });
  const file = f.client.dialog.saveFile();
  f.answer(1, { handle: 'C:/real-path.txt', name: 'real-path.txt', writable: true });
  await assert.rejects(file, { code: 'INVALID_RESPONSE' });
  f.client.dispose();
});

test('超大和不可序列化参数在发送前失败，不泄漏待处理请求', async () => {
  const f = fixture();
  await assert.rejects(f.client.storage.set('huge', 'x'.repeat(contract.maxPayloadBytes)), { code: 'PAYLOAD_TOO_LARGE' });
  const cycle: Record<string, any> = {}; cycle.self = cycle;
  await assert.rejects(f.client.storage.set('cycle', cycle), { code: 'INVALID_REQUEST' });
  assert.equal(f.messages.length, 0);
  f.client.dispose();
});

function fixture(timeoutMs = 1000) {
  const messages: Record<string, unknown>[] = [];
  const listeners = new Set<(message: string) => void>();
  const transport: BridgeTransport = {
    send(message) { messages.push(JSON.parse(message)); },
    subscribe(listener) { listeners.add(listener); return () => { listeners.delete(listener); }; },
  };
  const client = new AlkaidClient(transport, { timeoutMs });
  const emit = (value: unknown) => { for (const listener of listeners) listener(JSON.stringify(value)); };
  const answer = (index: number, result: unknown) => emit({ requestId: messages[index].requestId, ok: true, result });
  return { messages, listeners, transport, client, emit, answer };
}

test('未连接运行时明确失败，不模拟成功', async () => {
  const client = new AlkaidClient();
  await assert.rejects(client.runtime.getInfo(), { code: 'RUNTIME_UNAVAILABLE' });
  client.dispose();
});

test('稳定方法名称、无自报身份、乱序结果正确对应', async () => {
  const f = fixture();
  const first = f.client.runtime.getInfo();
  const second = f.client.runtime.getCapabilities();
  assert.equal(f.messages[0].method, 'runtime.getInfo');
  assert.equal(f.messages[0].protocolVersion, 1);
  assert.equal('appId' in f.messages[0], false);
  f.answer(1, ['runtime.getInfo']);
  f.answer(0, { appId: 'com.example.notes', runtimeVersion: '2.0.0', protocolVersion: 1 });
  assert.deepEqual(await second, ['runtime.getInfo']);
  assert.equal((await first).appId, 'com.example.notes');
  f.client.dispose();
});

test('宿主权限失败保留结构化错误', async () => {
  const f = fixture();
  const pending = f.client.runtime.exit();
  f.emit({ requestId: f.messages[0].requestId, ok: false,
    error: { code: 'PERMISSION_DENIED', message: '无权退出平台', traceId: 't1' } });
  await assert.rejects(pending, { code: 'PERMISSION_DENIED', traceId: 't1' });
  f.client.dispose();
});

test('超时后迟到响应不完成后续请求', async () => {
  const f = fixture(10);
  await assert.rejects(f.client.runtime.getInfo(), { code: 'TIMEOUT' });
  const second = f.client.runtime.getCapabilities();
  f.answer(0, { appId: 'old', runtimeVersion: '2.0.0', protocolVersion: 1 });
  f.answer(1, []);
  assert.deepEqual(await second, []);
  f.client.dispose();
});

test('销毁清理所有请求和监听器并拒绝新调用', async () => {
  const f = fixture();
  const first = f.client.runtime.getInfo();
  const second = f.client.apps.list();
  f.client.dispose();
  f.client.dispose();
  assert.equal(f.listeners.size, 0);
  await assert.rejects(first, { code: 'SESSION_EXPIRED' });
  await assert.rejects(second, { code: 'SESSION_EXPIRED' });
  await assert.rejects(f.client.runtime.getInfo(), { code: 'SESSION_EXPIRED' });
});

test('同步发送失败清理请求', async () => {
  const f = fixture();
  f.transport.send = () => { throw new Error('测试发送故障'); };
  await assert.rejects(f.client.runtime.getInfo(), { code: 'TRANSPORT_ERROR' });
  f.client.dispose();
});

test('已知请求的错误类型和矛盾响应均被拒绝', async () => {
  const f = fixture();
  const first = f.client.apps.list();
  f.answer(0, { apps: [] });
  await assert.rejects(first, { code: 'INVALID_RESPONSE' });
  const second = f.client.runtime.exit();
  f.emit({ requestId: f.messages[1].requestId, ok: true, result: null, error: {} });
  await assert.rejects(second, { code: 'INVALID_RESPONSE' });
  f.client.dispose();
});

test('损坏的 JSON 响应使等待请求明确失败', async () => {
  const f = fixture();
  const pending = f.client.runtime.getInfo();
  for (const listener of f.listeners) listener('{');
  await assert.rejects(pending, { code: 'INVALID_RESPONSE' });
  f.client.dispose();
});

test('并发请求限额在完成后释放', async () => {
  const f = fixture();
  const pending = Array.from({ length: contract.maxPendingPerSession }, () => f.client.runtime.exit());
  await assert.rejects(f.client.runtime.exit(), { code: 'RESOURCE_EXHAUSTED' });
  pending.forEach((_, index) => f.answer(index, null));
  await Promise.all(pending);
  const next = f.client.runtime.exit();
  f.answer(pending.length, null);
  await next;
  f.client.dispose();
});

test('宿主协议版本不兼容时握手失败', async () => {
  const f = fixture();
  const pending = f.client.runtime.getInfo();
  f.answer(0, { appId: 'app', runtimeVersion: '3.0.0', protocolVersion: 2 });
  await assert.rejects(pending, { code: 'INVALID_RESPONSE' });
  f.client.dispose();
});

test('应用操作发送目标参数且保留真实启动中状态', async () => {
  const f = fixture();
  const start = f.client.apps.launch('org.alkaid.inspector');
  assert.equal(f.messages[0].method, 'apps.launch');
  assert.deepEqual(f.messages[0].params, { appId: 'org.alkaid.inspector' });
  assert.equal('appId' in f.messages[0], false);
  f.answer(0, { appId: 'org.alkaid.inspector', name: '能力查看器', version: '1.0.0', kind: 'bundled', state: 'starting' });
  assert.equal((await start).state, 'starting');
  const focus = f.client.apps.focus('org.alkaid.inspector');
  f.emit({ requestId: f.messages[1].requestId, ok: false, error: { code: 'APP_NOT_RUNNING', message: '应用尚未运行' } });
  await assert.rejects(focus, { code: 'APP_NOT_RUNNING' });
  const close = f.client.apps.close('org.alkaid.inspector');
  f.answer(2, { appId: 'org.alkaid.inspector', name: '能力查看器', version: '1.0.0', kind: 'bundled', state: 'closing' });
  assert.equal((await close).state, 'closing');
  f.client.dispose();
});

test('拒绝无效目标标识与未知运行状态', async () => {
  const f = fixture();
  await assert.rejects(f.client.apps.launch(''), { code: 'INVALID_REQUEST' });
  await assert.rejects(f.client.apps.close('x'.repeat(129)), { code: 'INVALID_REQUEST' });
  assert.equal(f.messages.length, 0);
  const list = f.client.apps.list();
  f.answer(0, [{ appId: 'a', name: 'a', version: '1', kind: 'bundled', state: 'imaginary' }]);
  await assert.rejects(list, { code: 'INVALID_RESPONSE' });
  f.client.dispose();
});

test('安装返回任务标识，默认不信任未知发布者且不发送本机路径', async () => {
  const f = fixture();
  const id = 'a'.repeat(64);
  const prepare = f.client.apps.prepareInstall();
  assert.deepEqual(f.messages[0].params, {});
  f.answer(0, { taskId: id });
  assert.deepEqual(await prepare, { taskId: id });
  const confirm = f.client.apps.confirmInstall(id);
  assert.deepEqual(f.messages[1].params, { taskId: id, trustPublisher: false });
  f.emit({ requestId: f.messages[1].requestId, ok: false, error: { code: 'PUBLISHER_UNTRUSTED', message: '尚未确认信任' } });
  await assert.rejects(confirm, { code: 'PUBLISHER_UNTRUSTED' });
  f.client.dispose();
});

test('缺少预览的确认状态和未知任务状态被拒绝', async () => {
  const f = fixture();
  const id = 'b'.repeat(64);
  const invalidPreview = f.client.apps.getTask(id);
  f.answer(0, { taskId: id, kind: 'install', appId: 'com.example.a', status: 'awaitingConsent', phase: 'awaitingConsent', message: '等待确认' });
  await assert.rejects(invalidPreview, { code: 'INVALID_RESPONSE' });
  const invalidStatus = f.client.apps.listTasks();
  f.answer(1, [{ taskId: id, kind: 'install', appId: 'com.example.a', status: 'done', phase: 'done', message: '' }]);
  await assert.rejects(invalidStatus, { code: 'INVALID_RESPONSE' });
  f.client.dispose();
});

test('分步打包可选择启用，旧调用参数保持兼容并接收等待输入状态', async () => {
  const f = fixture();
  const legacy = f.client.apps.preparePack('{}', true);
  assert.deepEqual(f.messages[0].params, { manifest: '{}', localPublisher: true });
  f.answer(0, { taskId: 'a'.repeat(64) }); await legacy;
  const staged = f.client.apps.preparePack('{}', true, true);
  assert.deepEqual(f.messages[1].params, { manifest: '{}', localPublisher: true, interactive: true });
  f.answer(1, { taskId: 'a'.repeat(64) }); await staged;
  const query = f.client.apps.getTask('a'.repeat(64));
  f.answer(2, { taskId: 'a'.repeat(64), kind: 'pack', appId: 'com.example.app', status: 'awaitingInput', phase: 'output', message: '请选择保存位置' });
  assert.equal((await query).status, 'awaitingInput');
  f.client.dispose();
});

/** 验证桌面安装时解析插件版本，以及移除必需插件后的启动拦截。 */
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { resolve, join } from 'node:path';
import { readFile, writeFile, unlink } from 'node:fs/promises';
import { keygen, pack, signedEntries, zip } from './ypkg.mjs';
import { connect, waitFor } from './cef-session.mjs';

const [pid, mode, input] = process.argv.slice(2);
const appId = 'com.example.plugin-required';
const pluginId = 'com.example.serial';
if (pid === '--fixtures') {
  const root = resolve(mode);
  const key = join(root, 'fixture.pem');
  await keygen(key);
  try {
    const app = JSON.parse(await readFile('samples/inspector/manifest.json', 'utf8'));
    Object.assign(app, { appId, permissions: [`plugin.${pluginId}`],
      dependencies: [{ pluginId, minVersion: '1.0.0', maxVersionExclusive: '2.0.0', required: true }] });
    const manifest = join(root, 'app.json');
    await writeFile(manifest, JSON.stringify(app));
    await pack(manifest, 'build/host/web/samples/inspector', key, join(root, 'app.ypkg'));
    const plugin = { manifestVersion: 3, type: 'plugin', pluginId, name: '插件恢复测试',
      version: '1.0.0', publisher: app.publisher, protocolMajor: 1, architecture: 'win-x64', entry: 'worker.exe',
      capabilities: ['echo', 'crash', 'slow', 'badEvent', 'cooperative', 'armIdle'], events: ['activity'] };
    const worker = await readFile('build/host/infra/package/Release/alkaid_plugin_probe.exe');
    await zip(join(root, 'plugin.ypkg'), signedEntries(plugin, [{ name: 'assets/worker.exe', data: worker }], await readFile(key)));
  } finally { await unlink(key); }
  process.exit(0);
}
assert.match(pid ?? '', /^[1-9]\d*$/);
assert.ok(['install', 'missing'].includes(mode));
const desktop = await connect('alkaid://desktop/');
let app;
const installed = async () => (await desktop.sdk('c.apps.list()')).find(item => item.appId === appId);
try {
  if (mode === 'install') {
    const start = await desktop.sdk('c.apps.prepareInstall()');
    assert.ok(start.taskId, JSON.stringify(start));
    execFileSync('pwsh', ['-NoProfile', '-File', 'scripts/select-test-package.ps1',
      '-HostProcessId', pid, '-PackagePath', resolve(input)], { timeout: 20000, windowsHide: true });
    const task = await waitFor(async () => {
      const item = await desktop.sdk(`c.apps.getTask('${start.taskId}')`);
      return item.status === 'running' ? null : item;
    });
    assert.equal(task.status, 'awaitingConsent', JSON.stringify(task));
    const confirmed = await desktop.sdk(`c.apps.confirmInstall('${start.taskId}', true)`);
    assert.equal(confirmed.taskId, start.taskId, JSON.stringify(confirmed));
    const entry = await waitFor(installed);
    assert.deepEqual(entry.pluginBindings, { [pluginId]: '1.0.0' });
    const launched = await desktop.sdk(`c.apps.launch('${appId}')`);
    assert.ok(!launched.error, JSON.stringify(launched));
    const origin = createHash('sha256').update(appId).digest('hex').slice(0, 32);
    app = await connect(`alkaid://a-${origin}/`);
    const denied = await desktop.sdk(`c.plugin.on('${pluginId}', 'activity', () => {})`);
    assert.equal(denied.error, 'PERMISSION_DENIED');
    const unknown = await app.sdk(`c.plugin.on('${pluginId}', 'unlisted', () => {})`);
    assert.equal(unknown.error, 'EVENT_NOT_FOUND');
    await app.evaluate(`(async () => {
      const { AlkaidClient } = await import('/sdk/index.js');
      const { createCefTransport } = await import('/src/bridge.js');
      window.pluginProbeClient = new AlkaidClient(createCefTransport());
      window.pluginProbeEvents = [];
      window.stopPluginProbe = await pluginProbeClient.plugin.on('${pluginId}', 'activity',
        data => pluginProbeEvents.push(data));
    })()`);
    const echo = () => app.sdk(`c.plugin.call('${pluginId}', 'echo', { message: '你好，插件' })`);
    const first = await echo();
    assert.equal(first.version, '1.0.0', JSON.stringify(first));
    assert.deepEqual(first.params, { message: '你好，插件' });
    assert.deepEqual(first.security, { restricted: true, integrity: 4096, workspaceWritable: false });
    await waitFor(() => app.evaluate('pluginProbeEvents.length === 1'));
    assert.deepEqual(await app.evaluate('pluginProbeEvents'), [{ message: '你好，插件' }]);
    assert.ok(Number.isInteger(first.pid) && first.pid !== Number(pid));
    assert.equal((await echo()).pid, first.pid);
    await waitFor(() => app.evaluate('pluginProbeEvents.length === 2'));
    const armed = await app.sdk(`c.plugin.call('${pluginId}', 'armIdle')`);
    assert.equal(armed.pid, first.pid);
    // 仅观察页面事件，期间不发出任何插件调用。
    await waitFor(() => app.evaluate('pluginProbeEvents.length === 3'), '空闲时未收到插件主动事件');
    assert.deepEqual(await app.evaluate('({ idle: pluginProbeEvents[2].idle, bytes: pluginProbeEvents[2].bytes.length })'),
      { idle: true, bytes: 80 * 1024 });
    const forged = await app.sdk(`c.plugin.call('${pluginId}', 'badEvent')`);
    assert.equal(forged.error, 'PLUGIN_UNAVAILABLE');
    assert.equal(await app.evaluate('pluginProbeEvents.length'), 3);
    await app.evaluate('stopPluginProbe()');
    const afterForged = await echo();
    assert.notEqual(afterForged.pid, first.pid);
    assert.equal((await app.sdk(`c.plugin.call('${pluginId}', 'armIdle')`)).pid, afterForged.pid);
    await app.evaluate('new Promise(resolve => setTimeout(resolve, 600))');
    assert.equal(await app.evaluate('pluginProbeEvents.length'), 3);
    assert.equal((await echo()).pid, afterForged.pid);
    assert.equal((await app.sdk(`c.plugin.call('${pluginId}', 'missing')`)).error, 'METHOD_NOT_FOUND');
    const failureCount = async operation => (await desktop.sdk('c.apps.listAudit()')).filter(entry =>
      entry.operation === operation && entry.appId === appId && entry.target === pluginId && entry.code === 'PLUGIN_UNAVAILABLE').length;
    const callFailures = await failureCount('pluginCall');
    assert.equal((await app.sdk(`c.plugin.call('${pluginId}', 'crash')`)).error, 'PLUGIN_UNAVAILABLE');
    assert.equal(await failureCount('pluginCall'), callFailures + 1);
    const restarted = await echo();
    assert.equal(restarted.version, '1.0.0');
    assert.notEqual(restarted.pid, afterForged.pid);
    const cooperativeTimeout = await app.sdk(`c.plugin.call('${pluginId}', 'cooperative', null, {timeoutMs: 300})`);
    assert.equal(cooperativeTimeout.error, 'TIMEOUT');
    const afterTimeout = await echo();
    assert.equal(afterTimeout.pid, restarted.pid);
    assert.equal(afterTimeout.cancelled, 1);
    const abort = await app.sdk(`(async () => {
      const controller = new AbortController();
      setTimeout(() => controller.abort(), 200);
      return c.plugin.call('${pluginId}', 'cooperative', null, {signal: controller.signal});
    })()`);
    assert.equal(abort.error, 'ABORTED');
    const afterAbort = await echo();
    assert.equal(afterAbort.pid, restarted.pid);
    assert.equal(afterAbort.cancelled, 2);
    const timeout = await app.sdk(`c.plugin.call('${pluginId}', 'slow', null, {timeoutMs: 500})`);
    assert.equal(timeout.error, 'TIMEOUT');
    assert.notEqual((await echo()).pid, restarted.pid);
    const idleFailures = await failureCount('pluginExit');
    const idle = await app.sdk(`c.plugin.call('${pluginId}', 'armIdle', 'exit')`);
    assert.ok(Number.isInteger(idle.pid));
    // 等待宿主空闲轮询发现退出，不用下一次插件调用触发检测。
    await waitFor(async () => await failureCount('pluginExit') === idleFailures + 1, '空闲退出未记录审计');
    assert.equal((await installed()).state, 'running');
    const recovered = await echo();
    assert.equal(recovered.version, '1.0.0');
    assert.notEqual(recovered.pid, idle.pid);
    assert.deepEqual(recovered.params, { message: '你好，插件' });
    console.log('调用中与空闲退出均已记录，应用保持运行并按原绑定版本恢复插件');
  } else {
    const entry = await waitFor(installed);
    assert.equal(entry.state, 'unavailable');
    assert.match(entry.error, /com\.example\.serial/);
    const result = await desktop.sdk(`c.apps.launch('${appId}')`);
    assert.equal(result.error, 'APP_UNAVAILABLE');
  }
  console.log(`真实插件版本绑定回归通过：${mode}`);
} finally {
  app?.socket.close();
  await desktop.sdk('c.runtime.exit()').catch(() => {});
  desktop.socket.close();
}

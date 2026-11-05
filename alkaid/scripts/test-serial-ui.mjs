/** 在真实 CEF 和受限插件进程中验证串口枚举及无设备场景。 */
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { resolve } from 'node:path';
import { connect, waitFor } from './cef-session.mjs';

const [pid, packagePath] = process.argv.slice(2);
assert.match(pid ?? '', /^[1-9]\d*$/);
const pluginId = 'com.alkaid.serial';
const appId = 'com.example.serial-validation';
const desktop = await connect('alkaid://desktop/');
let app;
try {
  const task = await desktop.sdk('c.plugins.prepareInstall()');
  assert.ok(task.taskId, JSON.stringify(task));
  execFileSync('pwsh', ['-NoProfile', '-File', 'scripts/select-test-text.ps1', '-HostProcessId', pid,
    '-Title', '选择 YPKG 插件包', '-FilePath', resolve(packagePath)], { timeout: 20000, windowsHide: true });
  const preview = await waitFor(async () => {
    const result = await desktop.sdk(`c.apps.getTask('${task.taskId}')`);
    return result.status === 'running' ? null : result;
  });
  assert.equal(preview.status, 'awaitingConsent', JSON.stringify(preview));
  assert.deepEqual(preview.preview.capabilities, ['list', 'open', 'read', 'write', 'close']);
  const confirmed = await desktop.sdk(`c.apps.confirmInstall('${task.taskId}', true)`);
  assert.equal(confirmed.taskId, task.taskId, JSON.stringify(confirmed));
  const installed = await waitFor(async () => {
    const result = await desktop.sdk(`c.apps.getTask('${task.taskId}')`);
    return result.status === 'running' ? null : result;
  });
  assert.equal(installed.status, 'succeeded', JSON.stringify(installed));
  const launch = await desktop.sdk(`c.apps.launch('${appId}')`);
  assert.ok(!launch.error, JSON.stringify(launch));
  const origin = createHash('sha256').update(appId).digest('hex').slice(0, 32);
  app = await connect(`alkaid://a-${origin}/`);
  const result = await app.sdk(`c.plugin.call('${pluginId}', 'list', {})`);
  assert.ok(Array.isArray(result.ports) && result.ports.every(port => /^COM[1-9]\d{0,3}$/.test(port)), JSON.stringify(result));
  const unused = Array.from({ length: 100 }, (_, index) => `COM${9999 - index}`).find(port => !result.ports.includes(port));
  assert.ok(unused);
  const open = port => app.sdk(`c.plugin.call('${pluginId}', 'open', {port:${JSON.stringify(port)},baudRate:115200})`);
  assert.equal((await open('C:/outside')).error, 'INVALID_REQUEST');
  const missing = await open(unused);
  assert.equal(missing.error, 'SERIAL_NOT_FOUND', JSON.stringify(missing));
  for (const [method, params] of [
    ['read', { handle: 'expired', maxBytes: 128, waitMs: 100 }],
    ['write', { handle: 'expired', bytes: [65] }],
    ['close', { handle: 'expired' }],
  ]) {
    const value = await app.sdk(`c.plugin.call('${pluginId}', '${method}', ${JSON.stringify(params)})`);
    assert.equal(value.error, 'HANDLE_EXPIRED', JSON.stringify(value));
  }
  console.log(`串口插件验收通过。Windows 枚举：${result.ports.join(', ') || '空'}，缺失端口：${unused}`);
} finally {
  app?.socket.close();
  await desktop.sdk('c.runtime.exit()').catch(() => {});
  desktop.socket.close();
}

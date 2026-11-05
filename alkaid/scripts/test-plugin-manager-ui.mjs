/** 验证桌面插件管理、发布者确认和运行中依赖拦截。 */
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { resolve, join } from 'node:path';
import { writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { connect, waitFor } from './cef-session.mjs';

const [pid, root] = process.argv.slice(2);
assert.match(pid ?? '', /^[1-9]\d*$/);
assert.ok(root);
const desktop = await connect('alkaid://desktop/');
const pluginId = 'com.example.serial';
const appId = 'com.example.plugin-dependent';
const list = async () => desktop.sdk('c.plugins.list()');
const app = async () => (await desktop.sdk('c.apps.list()')).find(item => item.appId === appId);
const click = id => desktop.evaluate(`document.getElementById(${JSON.stringify(id)}).click()`);

async function choose(version) {
  await waitFor(() => desktop.evaluate("!document.getElementById('install-plugin').disabled"));
  await click('install-plugin');
  execFileSync('pwsh', ['-NoProfile', '-File', 'scripts/select-test-text.ps1', '-HostProcessId', pid,
    '-Title', '选择 YPKG 插件包', '-FilePath', resolve(root, `plugin-${version}.ypkg`)], { timeout: 20000, windowsHide: true });
  await waitFor(() => desktop.evaluate("document.getElementById('install-dialog').open"));
  assert.equal(await desktop.evaluate("document.getElementById('install-title').textContent"), '确认插件信息');
}
try {
  await desktop.evaluate(`document.querySelector('[data-open="apps"]').click();document.querySelector('[data-open="plugins"]').click()`);
  await waitFor(() => desktop.evaluate("document.getElementById('plugin-status').textContent.includes('还没有')"));
  await choose('1.0.0');
  assert.equal(await desktop.evaluate("document.getElementById('confirm-install').disabled"), true);
  await click('trust-publisher');
  await click('confirm-install');
  await waitFor(async () => (await list())[0]?.version === '1.0.0');
  assert.deepEqual((await app()).pluginBindings, { [pluginId]: '1.0.0' });
  assert.ok(!(await desktop.sdk(`c.apps.launch('${appId}')`)).error);
  await waitFor(async () => (await app()).state === 'running');
  await choose('1.1.0');
  await click('confirm-install');
  await waitFor(() => desktop.evaluate("document.getElementById('package-confirm-error').textContent.includes('请先关闭')"));
  assert.equal((await list())[0].version, '1.0.0');
  await desktop.sdk(`c.apps.close('${appId}')`);
  await waitFor(async () => (await app()).state === 'stopped');
  await click('confirm-install');
  await waitFor(async () => (await list())[0]?.version === '1.1.0');
  assert.deepEqual((await app()).pluginBindings, { [pluginId]: '1.1.0' });
  await waitFor(() => desktop.evaluate("document.getElementById('plugin-list').textContent.includes('v1.1.0')"));
  const screenshot = await desktop.command('Page.captureScreenshot', { format: 'png' });
  await writeFile(join(root, 'plugins.png'), Buffer.from(screenshot.data, 'base64'));
  await choose('1.2.0');
  await click('confirm-install');
  const failed = await waitFor(async () => (await desktop.sdk('c.apps.listTasks()'))
    .find(task => task.kind === 'pluginInstall' && task.error?.code === 'PLUGIN_HANDSHAKE_FAILED'));
  assert.equal(failed.status, 'failed');
  assert.equal((await list())[0].version, '1.1.0');
  assert.deepEqual((await app()).pluginBindings, { [pluginId]: '1.1.0' });
  await waitFor(() => desktop.evaluate("document.getElementById('plugin-status').textContent.includes('握手失败')"));
  assert.ok(!(await desktop.sdk(`c.apps.launch('${appId}')`)).error);
  const origin = createHash('sha256').update(appId).digest('hex').slice(0, 32);
  const running = await connect(`alkaid://a-${origin}/`);
  try {
    const response = await running.sdk(`c.plugin.call('${pluginId}', 'echo', { retained: true })`);
    assert.equal(response.version, '1.1.0', JSON.stringify(response));
    assert.deepEqual(response.params, { retained: true });
  } finally { running.socket.close(); }
  await desktop.sdk(`c.apps.close('${appId}')`);
  await waitFor(async () => (await app()).state === 'stopped');
  await desktop.evaluate(`document.querySelector('[data-package-kind="plugin"]').click()`);
  await click('confirm-manage');
  await waitFor(async () => (await list()).length === 0);
  assert.deepEqual((await app()).pluginBindings, {});
  assert.deepEqual(desktop.errors, []);
  console.log('真实桌面插件安装、更新、卸载和绑定刷新通过');
} finally {
  await desktop.sdk('c.runtime.exit()').catch(() => {});
  desktop.socket.close();
}

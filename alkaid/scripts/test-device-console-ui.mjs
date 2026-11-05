/** 用真实签名包和原生选择器验证设备控制页面。 */
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { resolve, join } from 'node:path';
import { writeFile } from 'node:fs/promises';
import { connect, waitFor } from './cef-session.mjs';

const [pid, root] = process.argv.slice(2);
assert.match(pid ?? '', /^[1-9]\d*$/);
const desktop = await connect('alkaid://desktop/');
let app;
async function install(method, title, file) {
  const task = await waitFor(async () => {
    const result = await desktop.sdk(method);
    if (result.error === 'RUNTIME_NOT_READY') return null;
    assert.ok(result.taskId, JSON.stringify(result));
    return result;
  });
  execFileSync('pwsh', ['-NoProfile', '-File', 'scripts/select-test-text.ps1', '-HostProcessId', pid,
    '-Title', title, '-FilePath', resolve(root, file)], { timeout: 20000, windowsHide: true });
  const getTask = () => desktop.sdk(`c.apps.getTask('${task.taskId}')`);
  const preview = await waitFor(async () => {
    const state = await getTask();
    return state.status === 'running' ? null : state;
  });
  assert.equal(preview.status, 'awaitingConsent', JSON.stringify(preview));
  await waitFor(() => desktop.evaluate("document.getElementById('install-dialog').open"));
  await desktop.evaluate(`(() => {
    const trust = document.getElementById('trust-publisher');
    if (!trust.checked && !document.getElementById('trust-row').hidden) trust.click();
    document.getElementById('confirm-install').click();
  })()`);
  const done = await waitFor(async () => {
    const state = await getTask();
    return state.status === 'running' ? null : state;
  });
  assert.equal(done.status, 'succeeded', JSON.stringify(done));
  await waitFor(() => desktop.evaluate("!document.getElementById('install-dialog').open"));
}
try {
  await install('c.plugins.prepareInstall()', '选择 YPKG 插件包', 'serial.ypkg');
  await install('c.apps.prepareInstall()', '选择 YPKG 应用包', 'device-console.ypkg');
  const id = 'com.alkaid.device-console';
  await desktop.evaluate(`document.querySelector('[data-open="apps"]').click()`);
  await waitFor(() => desktop.evaluate(`document.querySelector('[data-app-detail="${id}"]')?.click() || document.getElementById('app-detail-dialog').open`));
  assert.match(await desktop.evaluate("document.getElementById('app-detail-title').textContent"), /接点/);
  assert.equal(await desktop.evaluate("document.getElementById('app-detail-id').textContent"), id);
  assert.equal(await desktop.evaluate("document.getElementById('app-detail-publisher').textContent"), '瑶光台');
  assert.match(await desktop.evaluate("document.getElementById('app-detail-fingerprint').textContent"), /^sha256:[a-f0-9]{64}$/);
  assert.match(await desktop.evaluate("document.getElementById('app-detail-permissions').textContent"), /com\.alkaid\.serial.*已授权/);
  assert.match(await desktop.evaluate("document.getElementById('app-detail-plugins').textContent"), /com\.alkaid\.serial.*v1\.0\.0/);
  const detail = await desktop.command('Page.captureScreenshot', { format: 'png' });
  await writeFile(join(root, 'app-details.png'), Buffer.from(detail.data, 'base64'));
  const grantText = () => desktop.evaluate("document.getElementById('app-detail-permissions').textContent");
  const editGrant = async () => {
    await desktop.evaluate(`document.getElementById('edit-app-permissions').click();
      document.querySelector('#app-detail-permissions input').click();
      document.getElementById('save-app-permissions').click();`);
    await waitFor(() => desktop.evaluate("document.getElementById('app-permissions-actions').hidden"));
  };
  await editGrant();
  await waitFor(async () => (await grantText()).includes('未授权'));
  await desktop.evaluate("document.getElementById('close-app-detail').click()");
  const origin = createHash('sha256').update(id).digest('hex').slice(0, 32);
  const openApp = async () => {
    const launch = await desktop.sdk(`c.apps.launch('${id}')`);
    assert.ok(!launch.error, JSON.stringify(launch));
    assert.equal(launch.pluginBindings['com.alkaid.serial'], '1.0.0');
    return connect(`alkaid://a-${origin}/`);
  };
  const denied = await openApp();
  try {
    const result = await denied.sdk("c.plugin.call('com.alkaid.serial', 'list', {})");
    assert.equal(result.error, 'PERMISSION_DENIED');
    const busy = await desktop.sdk(`c.apps.setPermissions('${id}', ['plugin.com.alkaid.serial'])`);
    assert.equal(busy.error, 'APP_BUSY');
  } finally { denied.socket.close(); }
  await desktop.sdk(`c.apps.close('${id}')`);
  await waitFor(async () => (await desktop.sdk('c.apps.list()')).find(app => app.appId === id)?.state === 'stopped');
  await waitFor(() => desktop.evaluate(`document.querySelector('[data-app-detail="${id}"]')?.click() || document.getElementById('app-detail-dialog').open`));
  await editGrant();
  await waitFor(async () => (await grantText()).includes('已授权'));
  await desktop.evaluate("document.getElementById('close-app-detail').click()");
  const injected = await desktop.sdk(`c.apps.setPermissions('${id}', ['storage.kv'])`);
  assert.ok(injected.taskId, JSON.stringify(injected));
  const rejected = await waitFor(async () => {
    const task = await desktop.sdk(`c.apps.getTask('${injected.taskId}')`);
    return task.status === 'running' ? null : task;
  });
  assert.equal(rejected.error?.code, 'PERMISSION_DENIED');
  app = await openApp();
  await waitFor(() => app.evaluate("!document.getElementById('refresh').disabled"));
  const ports = await app.evaluate("Array.from(document.querySelectorAll('#ports option'), item => item.value)");
  assert.ok(ports.every(port => /^COM[1-9]\d{0,3}$/.test(port)));
  assert.equal(await app.evaluate("document.getElementById('read').disabled"), true);
  const screenshot = await app.command('Page.captureScreenshot', { format: 'png' });
  await writeFile(join(root, 'device-console.png'), Buffer.from(screenshot.data, 'base64'));
  const missing = Array.from({ length: 100 }, (_, index) => `COM${9999 - index}`).find(port => !ports.includes(port));
  assert.ok(missing);
  await app.evaluate(`(() => {
    const port = document.getElementById('port');
    port.value = ${JSON.stringify(missing)};
    port.dispatchEvent(new Event('input'));
    document.getElementById('open').click();
  })()`);
  await waitFor(() => app.evaluate("!document.getElementById('error').hidden && document.getElementById('error').textContent.includes('串口不存在')"));
  assert.equal(await app.evaluate("document.getElementById('send').disabled"), true);
  await app.evaluate("document.getElementById('refresh').click()");
  await waitFor(() => app.evaluate("document.getElementById('error').hidden && !document.getElementById('refresh').disabled"));
  assert.deepEqual(app.errors, []);
  console.log(`设备控制真实 CEF 回归通过，枚举端口：${ports.join(', ') || '空'}`);
} finally {
  app?.socket.close();
  await desktop.sdk('c.runtime.exit()').catch(() => {});
  desktop.socket.close();
}

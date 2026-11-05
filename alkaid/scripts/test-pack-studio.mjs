/** 通过真实 CEF 和系统选择器验证桌面打包工作台。 */
import assert from 'node:assert/strict';
import { access, writeFile } from 'node:fs/promises';
import { resolve, join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { connect, waitFor } from './cef-session.mjs';

const [pid, output, phase] = process.argv.slice(2);
assert.match(pid ?? '', /^[1-9]\d*$/);
assert.ok(output, '请指定测试产物目录');
const desktop = await connect('alkaid://desktop/');
const restart = phase === 'restart';
const restore = phase === 'restore';
const target = join(resolve(output), restore ? 'studio-restored.ypkg' : restart ? 'studio-update.ypkg' : 'studio.ypkg');
const backup = join(resolve(output), 'publisher.ypkey');
function select(title, path) {
  execFileSync('pwsh', ['-NoProfile', '-File', 'scripts/select-test-text.ps1',
    '-HostProcessId', pid, '-Title', title, '-FilePath', path], { timeout: 20000, windowsHide: true });
}
async function publisherBackup(password, restoring, code) {
  await desktop.evaluate(`document.getElementById('publisher-backup').open = true;
    document.getElementById('publisher-backup-password').value = ${JSON.stringify(password)};
    document.getElementById('publisher-backup-form').requestSubmit();`);
  select(restoring ? '选择发布者备份' : '保存发布者备份', backup);
  const task = await waitFor(async () => {
    const latest = (await desktop.sdk('c.apps.listTasks()')).find(item => item.kind === 'publisherBackup');
    return latest && latest.status !== 'running' ? latest : null;
  });
  if (code) assert.equal(task.error?.code, code);
  else assert.equal(task.status, 'succeeded', JSON.stringify(task));
  await waitFor(() => desktop.evaluate("!document.getElementById('submit-publisher-backup').disabled"));
  await desktop.evaluate("document.getElementById('publisher-backup').open = false");
}
try {
  await desktop.evaluate(`document.querySelector('[data-open="tools"]').click();document.getElementById('open-pack').click()`);
  assert.ok(await desktop.evaluate("document.getElementById('pack-dialog').open"));
  if (restore) {
    await waitFor(() => desktop.evaluate("!document.getElementById('local-publisher-form').hidden"));
    await publisherBackup('incorrect test password', true, 'PUBLISHER_BACKUP_INVALID');
    assert.equal(await desktop.sdk('c.apps.localPublisher()'), null);
    await publisherBackup('example backup passphrase', true);
  } else if (!restart) {
    await waitFor(() => desktop.evaluate("!document.getElementById('local-publisher-form').hidden"));
    await desktop.evaluate(`document.getElementById('local-publisher-name').value = '本地测试发布者';
      document.getElementById('local-publisher-form').requestSubmit();`);
  }
  await waitFor(() => desktop.evaluate("document.getElementById('local-publisher-status').textContent.includes('本地测试发布者')"));
  const publisher = await desktop.sdk('c.apps.localPublisher()');
  assert.deepEqual(Object.keys(publisher).sort(), ['keyId', 'name']);
  assert.match(publisher.keyId, /^sha256:[a-f0-9]{64}$/);
  assert.equal((await desktop.sdk("c.apps.localPublisher('另一个发布者')")).error, 'PUBLISHER_EXISTS');
  await desktop.evaluate(`document.getElementById('pack-app-id').value = 'Bad.ID';
    document.getElementById('pack-app-version').value = '01.0';
    document.getElementById('pack-app-entry').value = '../index.html';
    document.getElementById('submit-pack').click();`);
  assert.deepEqual(await desktop.evaluate(`Array.from(document.querySelectorAll('#pack-app-fields [aria-invalid="true"]'), field => field.id)`),
    ['pack-app-name', 'pack-app-id', 'pack-app-version', 'pack-app-entry']);
  assert.equal(await desktop.evaluate('document.activeElement.id'), 'pack-app-name');
  assert.equal((await desktop.sdk('c.apps.listTasks()')).some(task => task.kind === 'pack'), false);
  await desktop.evaluate(`document.getElementById('pack-app-name').value = '打包验收';
    document.getElementById('pack-app-id').value = 'com.example.studio';
    document.getElementById('pack-app-version').value = '${restart ? '1.1.0' : '1.0.0'}';
    document.getElementById('pack-app-entry').value = 'index.html';`);
  if (phase === 'icon') {
    const { root } = await desktop.command('DOM.getDocument');
    const { nodeId } = await desktop.command('DOM.querySelector', { nodeId: root.nodeId, selector: '#pack-app-icon' });
    await desktop.command('DOM.setFileInputFiles', { nodeId, files: [resolve('build/host/web/assets/alkaid-256.png')] });
    await waitFor(() => desktop.evaluate("!document.getElementById('pack-icon-preview').hidden"));
  }
  const screenshot = await desktop.command('Page.captureScreenshot', { format: 'png' });
  await writeFile(join(output, 'studio.png'), Buffer.from(screenshot.data, 'base64'));
  await desktop.evaluate("document.getElementById('submit-pack').click()");
  select('选择打包资源目录', resolve(output));
  await waitFor(() => desktop.evaluate("document.getElementById('pack-status').textContent.includes('找不到入口') && !document.getElementById('submit-pack').disabled"));
  const missing = (await desktop.sdk('c.apps.listTasks()')).find(task => task.kind === 'pack');
  assert.equal(missing.phase, 'source');
  assert.equal(missing.status, 'awaitingInput');
  await desktop.evaluate("document.getElementById('submit-pack').click()");
  select('选择打包资源目录', resolve('build/host/web/samples/inspector'));
  await waitFor(() => desktop.evaluate("document.getElementById('submit-pack').textContent === '选择保存位置'"),
    '资源选择后未停留在保存位置说明');
  const waitingOutput = (await desktop.sdk('c.apps.listTasks()')).find(task => task.kind === 'pack');
  assert.equal(waitingOutput.status, 'awaitingInput');
  assert.equal(waitingOutput.phase, 'output');
  assert.equal(waitingOutput.taskId, missing.taskId);
  await desktop.evaluate("document.getElementById('close-pack').click();document.querySelector('[data-open=tasks]').click();document.querySelector('.task-row button').click()");
  assert.equal(await desktop.evaluate("document.getElementById('pack-dialog').open"), true);
  const outputScreenshot = await desktop.command('Page.captureScreenshot', { format: 'png' });
  await writeFile(join(output, 'studio-output.png'), Buffer.from(outputScreenshot.data, 'base64'));
  await desktop.evaluate("document.getElementById('submit-pack').click()");
  execFileSync('pwsh', ['-NoProfile', '-File', 'scripts/select-test-text.ps1',
    '-HostProcessId', pid, '-Title', '保存 YPKG 包', '-Cancel'], { timeout: 20000, windowsHide: true });
  await waitFor(() => desktop.evaluate("document.getElementById('submit-pack').textContent === '选择保存位置' && !document.getElementById('submit-pack').disabled"));
  assert.equal((await desktop.sdk('c.apps.getTask(' + JSON.stringify(waitingOutput.taskId) + ')')).status, 'awaitingInput');
  await desktop.evaluate("document.getElementById('submit-pack').click()");
  select('保存 YPKG 包', target);
  await waitFor(() => desktop.evaluate("document.getElementById('submit-pack').textContent === '生成安装包' && !document.getElementById('submit-pack').disabled"));
  assert.equal((await desktop.sdk('c.apps.getTask(' + JSON.stringify(waitingOutput.taskId) + ')')).phase, 'ready');
  await assert.rejects(access(target), '确认生成前不应创建输出文件');
  await desktop.evaluate("document.getElementById('submit-pack').click()");
  const task = await waitFor(async () => {
    const tasks = await desktop.sdk('c.apps.listTasks()');
    return tasks.find(task => task.kind === 'pack' && ['succeeded', 'failed', 'cancelled'].includes(task.status));
  }, '打包任务未结束');
  assert.equal(task.status, 'succeeded', JSON.stringify(task));
  await waitFor(() => desktop.evaluate("document.getElementById('pack-status').textContent.includes('验签通过')"));
  await access(target);
  const verify = path => JSON.parse(execFileSync(resolve('build/host/infra/package/Release/alkaid_package_tool.exe'),
    ['verify', path], { encoding: 'utf8', windowsHide: true }));
  assert.equal(verify(target).publisherKeyId, publisher.keyId);
  if (restart || restore) assert.equal(verify(join(resolve(output), 'studio.ypkg')).publisherKeyId, publisher.keyId);
  await desktop.evaluate("document.getElementById('pack-install').click()");
  await waitFor(() => desktop.evaluate("document.getElementById('install-dialog').open"));
  assert.ok(await desktop.evaluate("document.getElementById('package-name').textContent.includes('com.example.studio')"));
  if (phase === 'icon') {
    await desktop.evaluate(`if (!document.getElementById('trust-row').hidden) document.getElementById('trust-publisher').click();
      document.getElementById('confirm-install').click();`);
    await waitFor(() => desktop.evaluate(`!!document.querySelector('#desktop-apps [data-app-id="com.example.studio"] [data-glyph=custom] img')`));
    const installed = (await desktop.sdk('c.apps.list()')).find(app => app.appId === 'com.example.studio');
    assert.match(installed.icon, /^UklGR/);
  } else await desktop.evaluate("document.getElementById('cancel-install').click()");
  await waitFor(() => desktop.evaluate("!document.getElementById('install-dialog').open"));
  await desktop.evaluate("document.getElementById('open-pack').click()");
  await desktop.evaluate("document.getElementById('pack-back').click()");
  await waitFor(() => desktop.evaluate("!document.getElementById('pack-form-content').hidden"));
  if (phase === 'backup') await publisherBackup('example backup passphrase', false);
  if (restart) assert.ok((await desktop.sdk('c.apps.listAudit()')).some(entry =>
    entry.operation === 'publisherBackup' && entry.code === 'OK'));
  if (restore) {
    await desktop.evaluate("document.getElementById('close-pack').click();document.getElementById('open-audit').click()");
    await waitFor(() => desktop.evaluate("document.getElementById('audit-records').textContent.includes('PUBLISHER_BACKUP_INVALID')"));
  }
  assert.deepEqual(desktop.errors, []);
  console.log('桌面打包工作台端到端通过');
} finally {
  await desktop.sdk('c.runtime.exit()').catch(() => {});
  desktop.socket.close();
}

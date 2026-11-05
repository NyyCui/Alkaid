/** 真实 CEF 中验证随附应用和自定义壁纸。 */
import assert from 'node:assert/strict';
import { copyFile, mkdir, writeFile, readFile, unlink } from 'node:fs/promises';
import { resolve, join } from 'node:path';
import { connect, waitFor } from './cef-session.mjs';

const [phase, root] = process.argv.slice(2);
assert.ok(['setup', 'restart', 'removed'].includes(phase));
const desktop = await connect('alkaid://desktop/');
const click = (session, id) => session.evaluate(`document.getElementById('${id}').click()`);
let inspector, files;
async function open(id, origin, title) {
  const result = await desktop.sdk(`c.apps.launch('${id}')`);
  assert.ok(!result.error, JSON.stringify(result));
  const app = await connect(`alkaid://${origin}/`);
  await waitFor(() => desktop.evaluate(`!document.getElementById('app-workspace').hidden &&
    document.getElementById('workspace-title').textContent === ${JSON.stringify(title)}`));
  return app;
}
try {
  if (phase === 'setup') {
    inspector = await open('org.alkaid.inspector', 'inspector', '能力查看器');
    assert.equal((await inspector.sdk("c.workspace.list('')")).error, 'PERMISSION_DENIED');
    await desktop.command('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-reduced-motion', value: 'no-preference' }] });
    assert.equal(await desktop.evaluate(`document.getElementById('workspace-minimize').click();
      !!document.querySelector('.workspace-exit-preview[data-motion=minimize]')`), true);
    await waitFor(() => desktop.evaluate("document.getElementById('app-workspace').hidden"));
    await desktop.evaluate("document.querySelector('.app-dock[data-app-id=\"org.alkaid.inspector\"]').click()");
    await waitFor(() => desktop.evaluate("!document.getElementById('app-workspace').hidden"));
    await desktop.evaluate("document.querySelector('.menubar [data-open=settings]').click()");
    const source = resolve(root, 'wallpaper.png');
    await copyFile('build/host/web/assets/alkaid-256.png', source);
    const { root: documentNode } = await desktop.command('DOM.getDocument');
    const { nodeId } = await desktop.command('DOM.querySelector', { nodeId: documentNode.nodeId, selector: '#wallpaper-file' });
    await desktop.command('DOM.setFileInputFiles', { nodeId, files: [source] });
    await waitFor(() => desktop.evaluate("document.getElementById('wallpaper-image').naturalWidth === 256"));
    await unlink(source);
    await desktop.evaluate("document.querySelector('[data-close=settings]').click()");
    await waitFor(() => desktop.evaluate("!document.getElementById('app-workspace').hidden"));
    assert.deepEqual(await desktop.evaluate(`[
      document.querySelector('.menubar').offsetHeight,
      document.getElementById('workspace-handle').offsetHeight]`), [36, 30]);
    await desktop.evaluate(`globalThis.workspaceHiddenOnClose = false;
      globalThis.workspaceCloseMotion = false;
      addEventListener('alkaid:runtime-event', ({ detail }) => {
        if (detail.event === 'workspace.hidden' && detail.payload.appId === 'org.alkaid.inspector') {
          globalThis.workspaceHiddenOnClose = document.getElementById('app-workspace').hidden;
          globalThis.workspaceCloseMotion = !!document.querySelector('.workspace-exit-preview[data-motion=close]');
        }
      });`);
    await click(desktop, 'workspace-close');
    await waitFor(() => desktop.evaluate('globalThis.workspaceHiddenOnClose'), '关闭事件未立即隐藏窗口壳');
    assert.equal(await desktop.evaluate('globalThis.workspaceCloseMotion'), true);
    await waitFor(async () => (await desktop.sdk('c.apps.list()')).find(item => item.appId === 'org.alkaid.inspector')?.state === 'stopped');

    const workspace = join(root, 'data', 'workspace');
    await mkdir(workspace, { recursive: true });
    await writeFile(join(workspace, '源文件.txt'), '原始内容');
    files = await open('org.alkaid.files', 'files', '文件');
    assert.equal(await desktop.evaluate("document.querySelectorAll('#desktop-apps [data-glyph=folder] svg, #desktop-apps [data-glyph=inspect] svg').length"), 2);
    assert.equal((await files.sdk("c.workspace.list('..')")).error, 'INVALID_REQUEST');
    await click(files, 'mkdir');
    await files.evaluate("document.getElementById('name').value = '项目'; document.getElementById('name-form').requestSubmit()");
    await waitFor(() => files.evaluate("document.getElementById('items').textContent.includes('项目')"));
    assert.equal(await files.sdk("c.workspace.change('copy', '源文件.txt', '副本.txt')"), null);
    assert.equal(await readFile(join(workspace, '副本.txt'), 'utf8'), '原始内容');
    assert.equal(await files.sdk("c.workspace.change('rename', '副本.txt', '改名.txt')"), null);
    assert.equal((await files.sdk("c.workspace.change('rename', '改名.txt', '源文件.txt')")).error, 'WORKSPACE_EXISTS');
  } else if (phase === 'restart') {
    await waitFor(() => desktop.evaluate("document.getElementById('wallpaper-image').naturalWidth === 256"));
    files = await open('org.alkaid.files', 'files', '文件');
    const items = await files.sdk("c.workspace.list('')");
    assert.deepEqual(items.map(item => item.name).sort(), ['改名.txt', '源文件.txt', '项目'].sort());
    await desktop.evaluate("document.querySelector('.menubar [data-open=settings]').click()");
    await click(desktop, 'clear-wallpaper');
    await waitFor(() => desktop.evaluate("document.getElementById('wallpaper-image').hidden"));
  } else {
    await waitFor(() => desktop.evaluate("!document.getElementById('choose-wallpaper').disabled"));
    assert.equal(await desktop.evaluate("document.getElementById('wallpaper-image').hidden"), true);
  }
  assert.deepEqual(desktop.errors, []);
  console.log('内置桌面回归通过', phase);
} finally {
  inspector?.socket.close();
  files?.socket.close();
  await desktop.sdk('c.runtime.exit()').catch(() => {});
  desktop.socket.close();
}

/** 真实桌面窗口、入口、表单和键盘行为验证，使用独立测试宿主。 */
import assert from 'node:assert/strict';
import { writeFile, mkdir } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { connect, waitFor } from './cef-session.mjs';

const [pid, output = 'build/ui-verification'] = process.argv.slice(2);
assert.match(pid ?? '', /^[1-9]\d*$/);
await mkdir(output, { recursive: true });
const desktop = await connect('alkaid://desktop/');
const results = [];
const click = id => desktop.evaluate(`document.getElementById(${JSON.stringify(id)}).click()`);
const native = restore => JSON.parse(execFileSync('pwsh', ['-NoProfile', '-File', 'scripts/inspect-desktop-window.ps1',
  '-HostProcessId', pid, ...(restore ? ['-RestoreFromTray'] : [])], { encoding: 'utf8', windowsHide: true, timeout: 15000 }));
async function run(name, action) { await action(); results.push(name); console.log(`通过：${name}`); }
async function key(key, code) {
  const event = { key, code: key, windowsVirtualKeyCode: code };
  await desktop.command('Input.dispatchKeyEvent', { ...event, type: 'keyDown', ...(key === 'Enter' ? { text: '\r' } : {}) });
  await desktop.command('Input.dispatchKeyEvent', { ...event, type: 'keyUp' });
}
async function screenshot(name) {
  await new Promise(resolve => setTimeout(resolve, 350));
  const shot = await desktop.command('Page.captureScreenshot', { format: 'png' });
  await writeFile(join(output, `${name}.png`), Buffer.from(shot.data, 'base64'));
}
try {
  await run('原生标题区移除，品牌图标及可拖动区域接入', async () => {
    const window = native(true);
    assert.ok(window.clientTopInset < 15, JSON.stringify(window));
    assert.ok(window.bigIcon, '任务栏与 Alt-Tab 使用 ICON_BIG');
    assert.ok(window.trayAttached);
    assert.equal(window.titleHit, 2, '顶部空白区域应命中 HTCAPTION');
  });
  await run('最小化、托盘双击唤回、最大化和还原', async () => {
    await click('host-minimize');
    assert.equal(native().minimized, true);
    assert.equal(native(true).minimized, false);
    await click('host-maximize');
    assert.equal(native().maximized, true);
    await click('host-maximize');
    assert.equal(native().maximized, false);
  });
  await screenshot('desktop-final');
  await run('桌面空白处右键打开瑶光台菜单', async () => {
    const { x, y } = await desktop.evaluate('({x: Math.round(innerWidth / 2), y: Math.round(innerHeight / 2)})');
    await desktop.command('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'right', clickCount: 1 });
    await desktop.command('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'right', clickCount: 1 });
    await waitFor(() => desktop.evaluate("!document.getElementById('app-context-menu').hidden"));
    assert.deepEqual(await desktop.evaluate("Array.from(document.querySelectorAll('#app-context-menu button'), button => button.textContent)"),
      ['打开应用管理', '刷新桌面', '更换壁纸…']);
    await screenshot('desktop-context');
    await desktop.evaluate("document.querySelector('#app-context-menu button').click()");
    assert.equal(await desktop.evaluate("document.getElementById('panel-apps').hidden"), false);
    await desktop.evaluate(`document.querySelector('[data-close="apps"]').click()`);
  });
  await run('桌面悬浮保留图标底色，选中仅高亮名称', async () => {
    const icon = 'document.querySelector(\'#desktop-apps [data-app-id="org.alkaid.files"]\')';
    await waitFor(() => desktop.evaluate(`!!${icon}`));
    const state = () => desktop.evaluate(`(() => {
      const icon = ${icon};
      const glyph = icon.querySelector('.application-glyph');
      const label = icon.querySelector('.desktop-icon-label');
      return { background: getComputedStyle(glyph).backgroundImage, outline: getComputedStyle(glyph).outlineStyle,
        label: getComputedStyle(label).backgroundColor, weight: getComputedStyle(label).fontWeight,
        selected: icon.classList.contains('selected') };
    })()`);
    const initial = await state();
    const { x, y } = await desktop.evaluate(`(() => {
      const rect = ${icon}.getBoundingClientRect();
      return { x: Math.round(rect.x + rect.width / 2), y: Math.round(rect.y + rect.height / 2) };
    })()`);
    await desktop.command('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y });
    await waitFor(() => desktop.evaluate(`${icon}.matches(':hover')`));
    assert.equal((await state()).background, initial.background);
    await desktop.command('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', clickCount: 1 });
    await desktop.command('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', clickCount: 1 });
    await waitFor(async () => (await state()).selected);
    const selected = await state();
    assert.equal(selected.background, initial.background);
    assert.equal(selected.outline, 'none');
    assert.equal(selected.label, initial.label);
    assert.ok(Number(selected.weight) > Number(initial.weight));
    await screenshot('icon-selection');
  });
  await run('窗口按钮使用三色圆点，应用列表显示内置图标', async () => {
    assert.deepEqual(await desktop.evaluate(`Array.from(document.querySelectorAll('.menubar .host-controls button'), button => button.id)`),
      ['host-close', 'host-minimize', 'host-maximize']);
    await desktop.evaluate(`document.querySelector('[data-open="apps"]').click()`);
    assert.equal(await desktop.evaluate(`document.querySelectorAll('#apps-list .app-list-glyph svg').length`), 2);
    await screenshot('apps');
    await desktop.evaluate(`document.querySelector('[data-close="apps"]').click()`);
  });
  await run('键盘打开菜单，Esc 关闭并恢复焦点', async () => {
    for (const selector of ['.system-menu', '.app-more']) {
      if (selector === '.app-more') await desktop.evaluate(`document.querySelector('[data-open="apps"]').click()`);
      const summary = `document.querySelector('${selector} summary')`;
      await desktop.evaluate(`${summary}.focus()`);
      await key('Enter', 13);
      assert.equal(await desktop.evaluate(`${summary}.parentElement.open`), true);
      await key('Tab', 9);
      assert.equal(await desktop.evaluate(`${summary}.parentElement.contains(document.activeElement) && document.activeElement !== ${summary}`), true);
      await key('Escape', 27);
      assert.equal(await desktop.evaluate(`!${summary}.parentElement.open && document.activeElement === ${summary}`), true);
    }
    await desktop.evaluate(`document.querySelector('[data-close="apps"]').click()`);
  });
  await run('应用搜索、筛选及更多菜单', async () => {
    await desktop.evaluate(`document.querySelector('[data-open="apps"]').click();document.getElementById('app-search').value='不存在的应用';document.getElementById('app-search').dispatchEvent(new Event('input'))`);
    assert.equal(await desktop.evaluate(`document.getElementById('search-empty').hidden`), false);
    await desktop.evaluate(`document.getElementById('app-search').value='inspector';document.getElementById('app-search').dispatchEvent(new Event('input'))`);
    assert.equal(await desktop.evaluate(`document.querySelectorAll('.managed-app:not([hidden])').length`), 1);
    await desktop.evaluate(`const row=document.querySelector('.managed-app:not([hidden])');row.querySelector('.app-more summary').click();row.querySelector('[data-app-detail]').click()`);
    assert.equal(await desktop.evaluate(`document.getElementById('app-detail-dialog').open`), true);
    await screenshot('app-detail');
    await click('close-app-detail');
    await desktop.evaluate(`document.querySelector('[data-close="apps"]').click()`);
  });
  await run('工具、插件、设置和任务入口可打开和关闭', async () => {
    for (const panel of ['tools', 'plugins', 'settings', 'tasks']) {
      await desktop.evaluate(`document.querySelector('[data-open="${panel}"]').click()`);
      assert.equal(await desktop.evaluate(`document.getElementById('panel-${panel}').hidden`), false);
      await screenshot(panel);
      await desktop.evaluate(`document.querySelector('[data-close="${panel}"]').click()`);
    }
  });
  await run('高级模板使用统一确认框，Esc 取消保留编辑内容', async () => {
    await click('open-pack');
    await desktop.evaluate(`document.getElementById('pack-advanced').open=true;document.getElementById('pack-manifest').value='自定义草稿';document.getElementById('pack-plugin-template').click()`);
    assert.equal(await desktop.evaluate(`document.getElementById('confirm-dialog').open`), true);
    await screenshot('confirmation');
    await desktop.command('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Escape', code: 'Escape', windowsVirtualKeyCode: 27 });
    await desktop.command('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Escape', code: 'Escape', windowsVirtualKeyCode: 27 });
    await waitFor(() => desktop.evaluate(`!document.getElementById('confirm-dialog').open`));
    assert.equal(await desktop.evaluate(`document.getElementById('pack-manifest').value`), '自定义草稿');
    await desktop.evaluate(`document.getElementById('pack-advanced').open=false`);
    await click('close-pack');
  });
  await run('标题区关闭按钮进入退出确认，取消后可继续使用', async () => {
    await click('host-close');
    assert.equal(await desktop.evaluate(`document.getElementById('exit-dialog').open`), true);
    await click('cancel-exit');
    assert.equal(await desktop.evaluate(`document.getElementById('exit-dialog').open`), false);
  });
  await run('减少动态效果设置关闭过渡及动画', async () => {
    await desktop.command('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-reduced-motion', value: 'reduce' }] });
    assert.equal(await desktop.evaluate(`getComputedStyle(document.getElementById('pack-dialog')).transitionDuration`), '0s');
    await desktop.command('Emulation.setEmulatedMedia', { features: [] });
  });
  assert.deepEqual(desktop.errors, []);
  await writeFile(join(output, 'experience-report.json'), JSON.stringify({ results, errors: desktop.errors }, null, 2));
} finally { desktop.socket.close(); }

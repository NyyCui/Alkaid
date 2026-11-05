/** 使用真实 Windows 窗口验证普通位置与尺寸、重启和屏幕外恢复。 */
import assert from 'node:assert/strict';
import { readFile, writeFile } from 'node:fs/promises';
import { connect, waitFor } from './cef-session.mjs';

const [pid, phase, evidence] = process.argv.slice(2);
assert.match(pid ?? '', /^[1-9]\d*$/);
assert.ok(['move', 'restart', 'offscreen'].includes(phase) && evidence);
const desktop = await connect('alkaid://desktop/');
const id = 'org.alkaid.inspector';
const state = async () => (await desktop.sdk('c.apps.list()')).find(app => app.appId === id)?.state;
async function open() {
  const result = await desktop.sdk(`c.apps.launch('${id}')`);
  assert.ok(!result.error, JSON.stringify(result));
  await waitFor(async () => await state() === 'running');
  const session = await connect('alkaid://inspector/');
  session.socket.close();
  await waitFor(() => desktop.evaluate("!document.getElementById('app-workspace').hidden"));
}
async function close() {
  await desktop.sdk(`c.apps.close('${id}')`);
  await waitFor(async () => await state() === 'stopped');
  await waitFor(() => desktop.evaluate("document.getElementById('app-workspace').hidden"));
}
function bounds() {
  return desktop.evaluate("document.getElementById('workspace-content').getBoundingClientRect().toJSON()");
}
function same(actual, expected) {
  for (const key of ['x', 'y', 'width', 'height'])
    assert.ok(Math.abs(actual[key] - expected[key]) <= 2, `${key}: ${actual[key]} != ${expected[key]}`);
}
try {
  await open();
  if (phase === 'move') {
    const moved = { x: 86, y: 123, width: 820, height: 540 };
    assert.equal(await desktop.sdk(`c.apps.layoutWorkspace(${JSON.stringify({
      appId: id, ...moved, visible: true, focus: true, persist: true, minimize: false,
    })})`), null);
    await close();
    await open();
    same(await bounds(), moved);
    await desktop.evaluate("document.getElementById('workspace-maximize').click()");
    assert.ok((await bounds()).width > moved.width);
    await close();
    await writeFile(evidence, JSON.stringify({ moved }, null, 2));
  } else {
    const report = JSON.parse(await readFile(evidence, 'utf8'));
    const restored = await bounds();
    if (phase === 'restart') same(restored, report.moved);
    else {
      const [width, height] = await desktop.evaluate('[innerWidth, innerHeight]');
      assert.ok(restored.x >= 0 && restored.y >= 0);
      assert.ok(restored.x + restored.width <= width);
      assert.ok(restored.y + restored.height <= height);
    }
    report[phase] = restored;
    await writeFile(evidence, JSON.stringify(report, null, 2));
  }
  console.log(`真实窗口布局回归通过：${phase}`);
} finally {
  await desktop.sdk('c.runtime.exit()').catch(() => {});
  desktop.socket.close();
}

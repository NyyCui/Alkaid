import assert from 'node:assert/strict';
import { connect, waitFor } from './cef-session.mjs';

const phase = process.argv[2];
assert.ok(['save', 'restart'].includes(phase), '请选择 save 或 restart');
const desktop = await connect('alkaid://desktop/');
const palette = () => desktop.evaluate('document.documentElement.dataset.palette');
const defaults = { palette: 'cream', clockFormat: '24', seconds: false };
const custom = { palette: 'mist', clockFormat: '12', seconds: true };

async function assertClockAndConfig(settings) {
  const clock = await desktop.evaluate('document.querySelector("#clock").textContent');
  assert.equal(/\d{2}:\d{2}:\d{2}/.test(clock), settings.seconds);
  assert.equal(/上午|下午/.test(clock), settings.clockFormat === '12');
  const config = await desktop.sdk("c.config.platform.get('desktop.appearance')");
  assert.deepEqual(config.value, settings);
}

try {
  await desktop.evaluate('document.querySelector(\'[data-open="settings"]\').click()');
  if (phase === 'save') {
    assert.equal(await palette(), 'cream');
    await desktop.evaluate(`document.querySelector('input[value="mist"]').click();
      document.querySelector('input[name=clockFormat][value="12"]').click();
      document.querySelector('#clock-seconds').click();`);
    assert.equal(await palette(), 'cream');
    await desktop.evaluate('document.querySelector("#save-settings").click()');
    await waitFor(async () => await palette() === 'mist', '保存后未应用配色');
    await assertClockAndConfig(custom);
  } else {
    assert.equal(await palette(), 'mist');
    await assertClockAndConfig(custom);
    assert.equal(await desktop.evaluate(`document.querySelector('input[name="palette"]:checked').value
      + document.querySelector('input[name=clockFormat]:checked').value + document.querySelector('#clock-seconds').checked`), 'mist12true');
    await desktop.evaluate('document.querySelector("#reset-settings").click()');
    assert.equal(await palette(), 'mist');
    await desktop.evaluate('document.querySelector("#save-settings").click()');
    await waitFor(async () => await palette() === 'cream', '保存后未恢复默认配色');
    await assertClockAndConfig(defaults);
  }
  assert.deepEqual(desktop.errors, []);
  console.log('桌面设置验证通过', phase);
} finally {
  try { await desktop.sdk('c.runtime.exit()'); } finally { desktop.socket.close(); }
}

/** 在真实 CEF 会话验证平台与应用配置作用域，以及重启后的数据。 */
import assert from 'node:assert/strict';
import { connect, waitFor } from './cef-session.mjs';

const phase = process.argv[2];
assert.ok(['write', 'restart', 'future-schema'].includes(phase));
const desktop = await connect('alkaid://desktop/');
let app;
const present = value => ({ found: true, value });
const absent = { found: false, value: null };
async function listen(session, event) {
  await session.evaluate(`(async () => {
    const { AlkaidClient } = await import('/sdk/index.js');
    const { createCefTransport } = await import('/src/bridge.js');
    window.configProbeClient = new AlkaidClient(createCefTransport());
    window.configProbeEvents = [];
    window.stopConfigProbe = await configProbeClient.events.subscribe(${JSON.stringify(event)},
      keys => configProbeEvents.push(keys));
  })()`);
}
try {
  const launch = await desktop.sdk("c.apps.launch('org.alkaid.inspector')");
  assert.ok(!launch.error, JSON.stringify(launch));
  app = await connect('alkaid://inspector/');
  const denied = await app.sdk("c.config.platform.set('theme', 'forged')");
  assert.equal(denied.error, 'PERMISSION_DENIED');
  assert.ok(!(await app.sdk('c.runtime.getCapabilities()')).includes('config.platform.set'));
  if (phase === 'write') {
    const noPlatformEvents = await app.sdk("c.events.subscribe('config.platform.changed', () => {})");
    assert.equal(noPlatformEvents.error, 'PERMISSION_DENIED');
    const unknown = await app.sdk("c.events.subscribe('config.unknown', () => {})");
    assert.equal(unknown.error, 'EVENT_NOT_FOUND');
    await listen(desktop, 'config.platform.changed');
    await listen(app, 'config.app.changed');
    assert.deepEqual(await desktop.sdk("c.config.platform.get('theme')"), absent);
    assert.equal(await desktop.sdk("c.config.platform.set('theme', {accent:'forest'})"), null);
    assert.equal(await desktop.sdk("c.config.app.set('theme', 'desktop')"), null);
    assert.deepEqual(await app.sdk("c.config.app.get('theme')"), absent);
    assert.equal(await app.sdk("c.config.app.batch([{type:'set',key:'theme',value:'inspector'},{type:'set',key:'optional',value:null}])"), null);
    await waitFor(() => app.evaluate('configProbeEvents.length === 1'));
    assert.deepEqual(await desktop.evaluate('configProbeEvents'), [['theme']]);
    assert.deepEqual(await app.evaluate('configProbeEvents'), [['theme', 'optional']]);
    const invalid = await app.sdk("c.config.app.batch([{type:'remove',key:'theme'},{type:'set',key:'../outside',value:true}])");
    assert.equal(invalid.error, 'INVALID_REQUEST');
    assert.deepEqual(await app.sdk("c.config.app.get('theme')"), present('inspector'));
    await app.evaluate('stopConfigProbe()');
    assert.equal(await app.sdk("c.config.app.set('theme', 'inspector')"), null);
    assert.deepEqual(await app.evaluate('configProbeEvents'), [['theme', 'optional']]);
  } else if (phase === 'restart') {
    assert.deepEqual(await desktop.sdk("c.config.platform.get('theme')"), present({accent:'forest'}));
    assert.deepEqual(await desktop.sdk("c.config.app.get('theme')"), present('desktop'));
    assert.deepEqual(await app.sdk("c.config.app.get('theme')"), present('inspector'));
    assert.deepEqual(await app.sdk("c.config.app.get('optional')"), present(null));
    assert.equal(await app.sdk("c.config.app.remove('optional')"), null);
    assert.deepEqual(await app.sdk("c.config.app.get('optional')"), absent);
  } else {
    const newer = await desktop.sdk("c.config.platform.set('theme', 'cannot-write')");
    assert.equal(newer.error, 'CONFIG_SCHEMA_UNSUPPORTED');
    assert.deepEqual(await app.sdk("c.config.app.get('theme')"), present('inspector'));
  }
  console.log(`真实配置回归通过：${phase}`);
} finally {
  app?.socket.close();
  await desktop.sdk('c.runtime.exit()').catch(() => {});
  desktop.socket.close();
}

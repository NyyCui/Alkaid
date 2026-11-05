/** 验证真实 CEF 下的插件调用权限与缺失实例行为。 */
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { connect } from './cef-session.mjs';

const desktop = await connect('alkaid://desktop/');
const instances = [];
try {
  for (const [appId, expected] of [
    ['com.example.plugin-granted', 'PLUGIN_UNAVAILABLE'],
    ['com.example.plugin-declared', 'PERMISSION_DENIED'],
  ]) {
    const launch = await desktop.sdk(`c.apps.launch(${JSON.stringify(appId)})`);
    assert.ok(!launch.error, JSON.stringify(launch));
    const origin = createHash('sha256').update(appId).digest('hex').slice(0, 32);
    const app = await connect(`alkaid://a-${origin}/`);
    instances.push(app);
    const result = await app.sdk("c.plugin.call('com.example.serial', 'open', { port: 'COM3' })");
    assert.equal(result.error, expected, JSON.stringify(result));
    const undeclared = await app.sdk("c.plugin.call('com.example.other', 'open')");
    assert.equal(undeclared.error, 'PERMISSION_DENIED');
    const invalid = await app.sdk("c.plugin.call('com.example.serial', 'other.open')");
    assert.equal(invalid.error, 'INVALID_REQUEST');
    const oversized = await app.sdk("c.plugin.call('com.example.serial', 'open', 'x'.repeat(246000))");
    assert.equal(oversized.error, 'INVALID_REQUEST');
    assert.deepEqual(app.errors, []);
  }
  const manager = await desktop.sdk("c.plugin.call('com.example.serial', 'open')");
  assert.equal(manager.error, 'PERMISSION_DENIED');
  console.log('真实插件调用入口回归通过');
} finally {
  for (const app of instances) app.socket.close();
  await desktop.sdk('c.runtime.exit()').catch(() => {});
  desktop.socket.close();
}

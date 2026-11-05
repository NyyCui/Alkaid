/** 在已安装签名测试包的 CEF 宿主中验证页面到 WinHTTP 的调用链。 */
import assert from 'node:assert/strict';
import { writeFile } from 'node:fs/promises';
import { connect, waitFor } from './cef-session.mjs';

const [origin, output, mode = 'local'] = process.argv.slice(2);
assert.match(origin ?? '', /^alkaid:\/\/a-[a-f0-9]{32}\/$/);
assert.ok(output, '需要指定测试报告路径');
assert.ok(['local', 'public'].includes(mode));
const desktop = await connect('alkaid://desktop/');
let app;
try {
  await desktop.evaluate(`document.querySelector('[data-open="apps"]').click()`);
  await waitFor(() => desktop.evaluate(`document.querySelector('[data-app-detail="com.example.network"]')?.click() || document.getElementById('app-detail-dialog').open`));
  const permissions = await desktop.evaluate("document.getElementById('app-detail-permissions').textContent");
  assert.match(permissions, mode === 'public' ? /https:\/\/httpbin\.org\// : /http:\/\/127\.0\.0\.1:\d+\//);
  assert.match(permissions, /GET\/HEAD\/POST · 已授权/);
  await desktop.evaluate("document.getElementById('close-app-detail').click()");
  const launch = await desktop.sdk("c.apps.launch('com.example.network')");
  assert.ok(!launch.error, JSON.stringify(launch));
  app = await connect(origin);

  async function request(path, status, content) {
    await app.evaluate(`(() => {
      document.querySelector('#path').value = ${JSON.stringify(path)};
      document.querySelector('#send').click();
    })()`);
    const result = await waitFor(() => app.evaluate(`(() => {
      if (document.querySelector('#send').disabled) return null;
      return Object.fromEntries(['status', 'output', 'error'].map(id =>
        [id, document.getElementById(id).textContent]));
    })()`), '网络请求未结束');
    assert.equal(result.status, `HTTP ${status}`, result.error);
    assert.ok(result.output.includes(content), '响应正文缺少预期内容');
    if (mode === 'public') assert.equal(new URL(JSON.parse(result.output).url).hostname, 'httpbin.org');
    return { path, status };
  }

  if (mode === 'public') {
    const endpoints = await app.sdk('c.net.getEndpoints()');
    assert.ok(endpoints.some(endpoint => endpoint.baseUrl === 'https://httpbin.org/' && !endpoint.allowPrivateNetwork));
  }
  const requests = mode === 'public' ? [await request('/get', 200, '"url"')] : [
    await request('/json', 200, '来自真实网络服务'),
    await request('/error', 422, '业务校验未通过'),
  ];
  assert.deepEqual(app.errors, []);
  await writeFile(output, JSON.stringify({ mode, status: 'passed', requests, pageErrors: app.errors }, null, 2));
  console.log(mode === 'public' ? 'CEF 请求台公网 HTTPS 回归通过：HTTP 200' : 'CEF 请求台回归通过：HTTP 200、422');
} catch (error) {
  await writeFile(output, JSON.stringify({ mode, status: 'failed', message: error.message }, null, 2));
  throw error;
} finally {
  app?.socket.close();
  await desktop.sdk('c.runtime.exit()').catch(() => {});
  desktop.socket.close();
}

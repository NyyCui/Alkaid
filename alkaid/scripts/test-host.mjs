/** 连接显式调试模式下的真实 CEF，验证桌面到宿主调用并保存截图。 */
import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { contract } from '../packages/sdk/src/method_catalog.ts';

const output = resolve(process.argv[2] ?? 'build/host-verification');
await mkdir(output, { recursive: true });
let target;
for (let attempt = 0; attempt < 80; attempt++) {
  try {
    const targets = await (await fetch('http://127.0.0.1:9227/json/list')).json();
    target = targets.find(item => item.type === 'page' && item.url.startsWith('alkaid://desktop'));
    if (target) break;
  } catch { /* 等待本轮启动的宿主就绪。 */ }
  await delay(250);
}
assert.ok(target, '真实 CEF 桌面未在调试端口出现');
const socket = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((done, fail) => { socket.onopen = done; socket.onerror = fail; });
let sequence = 0;
const calls = new Map();
const errors = [];
socket.onmessage = event => {
  const message = JSON.parse(event.data);
  if (message.method === 'Runtime.exceptionThrown') errors.push(message.params.exceptionDetails);
  const pending = calls.get(message.id);
  if (!pending) return;
  calls.delete(message.id);
  clearTimeout(pending.timer);
  if (message.error) pending.reject(new Error(JSON.stringify(message.error)));
  else pending.resolve(message.result);
};
function command(method, params = {}) {
  const id = ++sequence;
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => { calls.delete(id); reject(new Error(`调试命令超时：${method}`)); }, 10000);
    calls.set(id, { resolve, reject, timer });
    socket.send(JSON.stringify({ id, method, params }));
  });
}
async function evaluate(expression) {
  const result = await command('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
  if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
  return result.result.value;
}
async function waitConnected(previousTime) {
  for (let attempt = 0; attempt < 60; attempt++) {
    try {
      const state = await evaluate('({state:document.documentElement.dataset.runtime,time:performance.timeOrigin,error:document.getElementById("apps-error")?.textContent})');
      if (state.state === 'connected' && state.time !== previousTime) return state;
      if (state.state === 'error') throw new Error(`桌面连接失败：${JSON.stringify(state)}`);
    } catch (error) {
      if (String(error).includes('桌面连接失败')) throw error;
    }
    await delay(100);
  }
  throw new Error('桌面连接等待超时');
}
const results = [];
async function run(name, test) {
  await test();
  results.push({ name, passed: true });
  console.log(`通过：${name}`);
}
await command('Runtime.enable');
await command('Page.enable');
if (process.argv.includes('--exit-only')) {
  await waitConnected();
  await evaluate('document.getElementById("exit").click();document.getElementById("confirm-exit").click();true');
  socket.close();
  process.exit(0);
}
try {
  await run('真实桌面连接宿主', async () => {
    await waitConnected();
    assert.equal(await evaluate('document.getElementById("runtime-app").textContent'), 'org.alkaid.desktop');
  });
  if (process.argv.includes('--expect-storage')) {
    await run('重启后使用持久化独立 profile', async () => {
      assert.equal(await evaluate('localStorage.getItem("alkaid-profile-probe")'), 'persistent-profile');
      await evaluate('localStorage.removeItem("alkaid-profile-probe")');
    });
  }
  if (process.argv.includes('--seed-storage')) {
    await evaluate('localStorage.setItem("alkaid-profile-probe","persistent-profile")');
  }
  await run('SDK 真实查询与能力列表', async () => {
    const result = await evaluate(`(async()=>{
      const {AlkaidClient}=await import('/sdk/index.js');
      const {createCefTransport}=await import('/src/bridge.js');
      const client=new AlkaidClient(createCefTransport());
      try {return {info:await client.runtime.getInfo(),capabilities:await client.runtime.getCapabilities(),apps:await client.apps.list()};}
      finally {client.dispose();}
    })()`);
    assert.equal(result.info.protocolVersion, 1);
    assert.equal(result.info.runtimeVersion, '2.0.0-dev.6');
    assert.deepEqual(result.capabilities.sort(), contract.methods.filter(method => !method.capability || method.managerOnly).map(method => method.name).sort());
    assert.deepEqual(result.apps.map(app => app.appId).sort(), ['org.alkaid.desktop', 'org.alkaid.files', 'org.alkaid.inspector', 'org.alkaid.notes']);
    assert.equal(result.apps[0].appId, 'org.alkaid.desktop');
  });
  await run('宿主拒绝非法参数、伪造身份、协议与未知方法', async () => {
    const codes = await evaluate(`(async()=>{
      const send=body=>new Promise((resolve,reject)=>__alkaidQuery({request:JSON.stringify(body),onSuccess:r=>resolve(JSON.parse(r)),onFailure:(_,e)=>reject(Error(e))}));
      const base={protocolVersion:1,requestId:'boundary',method:'runtime.getInfo',params:{},timeoutMs:1000};
      const cases=[{...base,appId:'another.app'},{...base,params:{path:'anything'}},{...base,protocolVersion:2},{...base,method:'unknown.method'},{...base,requestId:''}];
      const results=[];for(const item of cases) results.push((await send(item)).error.code);return results;
    })()`);
    assert.deepEqual(codes, ['INVALID_REQUEST', 'INVALID_REQUEST', 'PROTOCOL_MISMATCH', 'METHOD_NOT_FOUND', 'INVALID_REQUEST']);
  });
  await run('资源缺失返回 404，HEAD 不返回正文', async () => {
    const resources = await evaluate(`(async()=>{const missing=await fetch('/missing.js');const head=await fetch('/1.css',{method:'HEAD'});return {missing:missing.status,head:head.status,text:await head.text()};})()`);
    assert.deepEqual(resources, { missing: 404, head: 200, text: '' });
  });
  await run('外部 fetch 被阻止', async () => {
    assert.equal(await evaluate(`fetch('https://example.com/').then(()=>false,()=>true)`), true);
  });
  await run('外部导航被拒绝，桌面身份保持有效', async () => {
    await command('Page.navigate', { url: 'https://example.com/' });
    assert.equal(await evaluate('location.origin'), 'alkaid://desktop');
    await waitConnected();
  });
  await run('刷新后重新建立会话和 SDK 调用', async () => {
    const before = await evaluate('performance.timeOrigin');
    await command('Page.reload', { ignoreCache: true });
    await waitConnected(before);
    assert.equal(await evaluate('document.getElementById("runtime-version").textContent'), '2.0.0-dev.6');
  });
  await run('桌面显示真实登记的随附应用', async () => {
    for (let attempt = 0; attempt < 30; attempt++) {
      if (await evaluate('document.getElementById("app-count").textContent === "3 个应用"')) break;
      await delay(100);
    }
    assert.equal(await evaluate('document.getElementById("app-count").textContent'), '3 个应用');
  });
  await run('关于面板与退出取消交互', async () => {
    await evaluate('document.querySelector(".system-menu [data-open=about]").click()');
    assert.equal(await evaluate('document.getElementById("panel-about").hidden'), false);
    await evaluate('document.querySelector("[data-close=about]").click();document.getElementById("exit").click()');
    assert.equal(await evaluate('document.getElementById("exit-dialog").open'), true);
    await evaluate('document.getElementById("cancel-exit").click()');
    assert.equal(await evaluate('document.getElementById("exit-dialog").open'), false);
  });
  const screenshot = await command('Page.captureScreenshot', { format: 'png' });
  await writeFile(resolve(output, 'desktop.png'), Buffer.from(screenshot.data, 'base64'));
  await run('没有未捕获页面异常', async () => assert.deepEqual(errors, []));
  if (process.argv.includes('--exit')) {
    await run('确认退出后真实浏览器关闭', async () => {
      const closed = new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('退出后浏览器没有关闭')), 10000);
        socket.addEventListener('close', () => { clearTimeout(timer); resolve(); }, { once: true });
      });
      await evaluate('document.getElementById("exit").click();document.getElementById("confirm-exit").click();true');
      await closed;
    });
  }
  await writeFile(resolve(output, 'report.json'), JSON.stringify({ target: target.url, results, errors }, null, 2));
  console.log(`真实 CEF 验证完成：${results.length} 项，结果保存在 ${output}`);
} finally {
  socket.close();
}

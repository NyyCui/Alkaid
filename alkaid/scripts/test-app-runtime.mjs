/** 真实 CEF 多窗口生命周期、权限与来源隔离验证；宿主需显式开启调试模式。 */
import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';

const output = resolve(process.argv[2] ?? 'build/app-verification');
await mkdir(output, { recursive: true });
const APP = 'org.alkaid.inspector';
const connections = [];
const results = [];
const errors = [];
async function targets() { return (await fetch('http://127.0.0.1:9227/json/list')).json(); }
async function waitFor(check, message) {
  for (let attempt = 0; attempt < 100; attempt++) {
    const result = await check();
    if (result) return result;
    await delay(100);
  }
  throw new Error(message);
}
async function connect(origin) {
  const target = await waitFor(async () => {
    try { return (await targets()).find(item => item.type === 'page' && item.url.startsWith(origin)); }
    catch { return false; } // 新宿主可能还没有开始监听调试端口。
  }, `窗口未出现：${origin}`);
  const socket = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((done, fail) => { socket.onopen = done; socket.onerror = fail; });
  let sequence = 0;
  const pending = new Map();
  socket.onmessage = event => {
    const value = JSON.parse(event.data);
    if (value.method === 'Runtime.exceptionThrown') errors.push({ origin, detail: value.params.exceptionDetails });
    const call = pending.get(value.id);
    if (!call) return;
    clearTimeout(call.timer);
    pending.delete(value.id);
    if (value.error) call.reject(new Error(JSON.stringify(value.error)));
    else call.resolve(value.result);
  };
  socket.onclose = () => {
    for (const call of pending.values()) { clearTimeout(call.timer); call.reject(new Error('调试窗口已关闭')); }
    pending.clear();
  };
  function command(method, params = {}) {
    const id = ++sequence;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { pending.delete(id); reject(new Error(`调试命令超时：${method}`)); }, 10000);
      pending.set(id, { resolve, reject, timer });
      socket.send(JSON.stringify({ id, method, params }));
    });
  }
  async function evaluate(expression) {
    const result = await command('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
    if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
    return result.result.value;
  }
  async function sdk(expression) {
    return evaluate(`(async()=>{const {AlkaidClient}=await import('/sdk/index.js');const {createCefTransport}=await import('/src/bridge.js');const c=new AlkaidClient(createCefTransport());try{return await (${expression});}catch(e){return {error:e.code};}finally{c.dispose();}})()`);
  }
  const connection = { target, socket, command, evaluate, sdk };
  connections.push(connection);
  await command('Runtime.enable');
  await command('Page.enable');
  await waitFor(() => evaluate(`document.documentElement.dataset.runtime === 'connected'`), `页面未连接宿主：${origin}`);
  return connection;
}
async function run(name, test) {
  await test();
  results.push({ name, passed: true });
  console.log(`通过：${name}`);
}
const desktop = await connect('alkaid://desktop/');
const action = (method, appId = APP) => desktop.sdk(`c.apps.${method}(${JSON.stringify(appId)})`);
const state = async () => (await desktop.sdk('c.apps.list()')).find(app => app.appId === APP).state;
const stopped = () => waitFor(async () => await state() === 'stopped', '应用没有关闭');
const appTargets = async () => (await targets()).filter(target => target.type === 'page' && target.url.startsWith('alkaid://inspector/'));
let app;
try {
  await run('未运行、未知应用、受保护桌面明确返回错误', async () => {
    await action('close');
    await stopped();
    assert.deepEqual(await action('focus'), { error: 'APP_NOT_RUNNING' });
    assert.deepEqual(await action('launch', 'org.missing.app'), { error: 'APP_NOT_FOUND' });
    for (const method of ['launch', 'focus', 'close']) {
      assert.deepEqual(await action(method, 'org.alkaid.desktop'), { error: 'PROTECTED_APP' });
    }
  });
  await run('首次初始化上下文时立即关闭不会遗留窗口', async () => {
    const values = await desktop.sdk(`(async()=>{const start=c.apps.launch('${APP}');const stop=c.apps.close('${APP}');return Promise.all([start,stop]);})()`);
    assert.ok(Array.isArray(values));
    await stopped();
    assert.equal((await appTargets()).length, 0);
  });
  await run('桌面真实打开能力查看器内嵌窗口', async () => {
    await desktop.evaluate(`document.querySelector('#apps-list [data-action=launch][data-app-id="${APP}"]').click()`);
    app = await connect('alkaid://inspector/');
    assert.equal(await state(), 'running');
    assert.equal((await app.sdk('c.runtime.getInfo()')).appId, APP);
    assert.deepEqual((await app.sdk('c.runtime.getCapabilities()')).sort(), ['config.app.batch', 'config.app.get', 'config.app.remove', 'config.app.set', 'events.subscribe', 'events.unsubscribe', 'files.abortStream', 'files.appendChunk', 'files.close', 'files.commitWrite', 'files.readNext', 'net.close', 'net.readNext', 'plugin.call', 'runtime.getCapabilities', 'runtime.getInfo', 'window.resolveClose', 'window.setCloseHandler']);
    await waitFor(() => desktop.evaluate(`!!document.querySelector('#running-apps .running')`), 'Dock 未显示真实运行状态');
  });
  if (process.argv.includes('--expect-storage')) {
    await run('平台重启后普通应用保留独立 profile 数据', async () => {
      assert.equal(await app.evaluate(`localStorage.getItem('alkaid-app-restart-probe')`), 'inspector-profile');
      assert.equal(await desktop.evaluate(`localStorage.getItem('alkaid-app-restart-probe')`), null);
      await app.evaluate(`localStorage.removeItem('alkaid-app-restart-probe')`);
    });
  }
  await run('并发重复启动保持单实例并聚焦已有窗口', async () => {
    const firstId = app.target.id;
    const repeated = await Promise.all([action('launch'), action('launch'), action('launch')]);
    assert.ok(repeated.every(value => value.state === 'running'));
    assert.equal((await appTargets()).length, 1);
    assert.equal((await appTargets())[0].id, firstId);
    assert.equal((await action('focus')).state, 'running');
    await waitFor(() => app.evaluate('document.hasFocus()'), '能力查看器未取得键盘焦点');
  });
  await run('普通应用不能管理其他应用或退出平台', async () => {
    for (const expression of ['c.apps.list()', `c.apps.launch('${APP}')`, `c.apps.focus('${APP}')`, `c.apps.close('${APP}')`, 'c.runtime.exit()']) {
      assert.deepEqual(await app.sdk(expression), { error: 'PERMISSION_DENIED' });
    }
    assert.equal(await state(), 'running');
  });
  await run('目标参数不能替代调用者身份且拒绝额外参数', async () => {
    const codes = await desktop.evaluate(`(async()=>{const base={protocolVersion:1,requestId:'invalid-target',method:'apps.launch',params:{},timeoutMs:1000};const send=o=>new Promise(r=>__alkaidQuery({request:JSON.stringify(o),onSuccess:s=>r(JSON.parse(s).error.code)}));const values=[{}, {appId:123}, {appId:''}, {appId:'${APP}',path:'../desktop'}, {appId:'x'.repeat(129)}];const results=[];for(const params of values) results.push(await send({...base,params}));return results;})()`);
    assert.deepEqual(codes, Array(5).fill('INVALID_REQUEST'));
    const forged = await app.evaluate(`new Promise(r=>__alkaidQuery({request:JSON.stringify({protocolVersion:1,requestId:'forged',method:'apps.list',params:{},timeoutMs:1000,appId:'org.alkaid.desktop'}),onSuccess:s=>r(JSON.parse(s).error.code)}))`);
    assert.equal(forged, 'INVALID_REQUEST');
  });
  await run('应用与桌面不能互读资源或跨来源导航', async () => {
    assert.equal(await app.evaluate(`fetch('alkaid://desktop/index.html').then(()=>false,()=>true)`), true);
    assert.equal(await desktop.evaluate(`fetch('alkaid://inspector/index.html').then(()=>false,()=>true)`), true);
    await app.command('Page.navigate', { url: 'alkaid://desktop/index.html' });
    assert.equal(await app.evaluate('location.origin'), 'alkaid://inspector');
    assert.equal((await app.sdk('c.runtime.getInfo()')).appId, APP);
  });
  await run('同名浏览器数据按应用隔离', async () => {
    await desktop.evaluate(`localStorage.setItem('alkaid-isolation-probe','desktop')`);
    assert.equal(await app.evaluate(`localStorage.getItem('alkaid-isolation-probe')`), null);
    await app.evaluate(`localStorage.setItem('alkaid-isolation-probe','inspector')`);
    assert.equal(await desktop.evaluate(`localStorage.getItem('alkaid-isolation-probe')`), 'desktop');
    assert.equal(await app.evaluate(`localStorage.getItem('alkaid-isolation-probe')`), 'inspector');
  });
  await run('应用刷新后重新建立普通应用会话', async () => {
    const before = await app.evaluate('performance.timeOrigin');
    await app.command('Page.reload');
    await waitFor(async () => {
      try { return await app.evaluate(`performance.timeOrigin !== ${before} && document.documentElement.dataset.runtime === 'connected'`); }
      catch { return false; }
    }, '应用刷新后未重连');
    assert.equal((await app.sdk('c.runtime.getInfo()')).appId, APP);
    assert.deepEqual(await app.sdk('c.apps.list()'), { error: 'PERMISSION_DENIED' });
  });
  await run('应用自身窗口关闭后桌面继续运行且 Dock 更新', async () => {
    await app.command('Page.close');
    await stopped();
    assert.equal((await appTargets()).length, 0);
    assert.equal((await desktop.sdk('c.runtime.getInfo()')).appId, 'org.alkaid.desktop');
    await waitFor(() => desktop.evaluate(`document.getElementById('running-apps').children.length === 0`), 'Dock 未清除已关闭窗口');
  });
  await run('重新启动新窗口保留应用自己的数据', async () => {
    const oldId = app.target.id;
    await action('launch');
    app = await connect('alkaid://inspector/');
    assert.notEqual(app.target.id, oldId);
    assert.equal(await app.evaluate(`localStorage.getItem('alkaid-isolation-probe')`), 'inspector');
    await app.evaluate(`localStorage.removeItem('alkaid-isolation-probe')`);
    await desktop.evaluate(`localStorage.removeItem('alkaid-isolation-probe')`);
  });
  await run('管理接口关闭真实窗口并允许幂等关闭', async () => {
    await app.evaluate(`window.onbeforeunload = () => '测试退出拦截'; true`);
    assert.equal((await action('close')).state, 'closing');
    await stopped();
    assert.equal((await action('close')).state, 'stopped');
  });
  await run('启动与关闭交错不留下重复窗口或悬挂状态', async () => {
    for (let index = 0; index < 5; index++) {
      const values = await desktop.sdk(`(async()=>{const start=c.apps.launch('${APP}');const stop=c.apps.close('${APP}');return Promise.all([start,stop]);})()`);
      assert.ok(Array.isArray(values));
      await stopped();
      assert.equal((await appTargets()).length, 0);
    }
  });
  await action('launch');
  app = await connect('alkaid://inspector/');
  if (process.argv.includes('--seed-storage')) await app.evaluate(`localStorage.setItem('alkaid-app-restart-probe','inspector-profile')`);
  await waitFor(() => desktop.evaluate(`!!document.querySelector('#running-apps .running') && !!document.querySelector('#apps-list [data-state=running]')`), '截图前运行状态未同步');
  for (const [name, connection] of [['desktop', desktop], ['inspector', app]]) {
    const capture = await connection.command('Page.captureScreenshot', { format: 'png' });
    await writeFile(resolve(output, `${name}.png`), Buffer.from(capture.data, 'base64'));
  }
  await run('桌面和普通应用均无未捕获页面异常', async () => assert.deepEqual(errors, []));
  if (process.argv.includes('--exit') || process.argv.includes('--native-exit')) {
    await run('平台退出会同时关闭桌面与运行中的应用', async () => {
      if (process.argv.includes('--native-exit')) await desktop.command('Page.close');
      else await desktop.sdk('c.runtime.exit()');
      await waitFor(() => connections.every(item => item.socket.readyState === WebSocket.CLOSED), '平台退出后仍有窗口存活');
    });
  }
} catch (error) {
  results.push({ name: '验证中断', passed: false, error: String(error) });
  throw error;
} finally {
  await writeFile(resolve(output, 'report.json'), JSON.stringify({ results, errors }, null, 2));
  for (const item of connections) item.socket.close();
}
console.log(`应用生命周期验证完成：${results.length} 项`);

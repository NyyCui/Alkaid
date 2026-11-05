/** 真实 CEF 集成测试共享连接，限定本机显式开启的调试端口。 */
import { setTimeout as delay } from 'node:timers/promises';
export async function targets() { return (await fetch('http://127.0.0.1:9227/json/list')).json(); }
export async function waitFor(check, message = '等待实际状态超时') {
  for (let i=0; i<150; i++) { const result = await check(); if (result) return result; await delay(100); }
  throw Error(message);
}
export async function connect(origin) {
  const target = await waitFor(async () => {
    try { return (await targets()).find(item => item.type === 'page' && item.url.startsWith(origin)); } catch { return false; }
  }, `未找到窗口：${origin}`);
  const socket = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { socket.onopen = resolve; socket.onerror = reject; });
  const pending = new Map(), errors = [];
  let sequence = 0;
  socket.onmessage = event => {
    const data = JSON.parse(event.data);
    if (data.method === 'Runtime.exceptionThrown') errors.push(data.params.exceptionDetails);
    const call = pending.get(data.id);
    if (!call) return;
    clearTimeout(call.timer); pending.delete(data.id);
    if (data.error) call.reject(Error(JSON.stringify(data.error))); else call.resolve(data.result);
  };
  socket.onclose = () => { for (const call of pending.values()) { clearTimeout(call.timer); call.reject(Error('窗口已关闭')); } pending.clear(); };
  function command(method, params = {}, timeoutMs = 10000) {
    const id = ++sequence;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { pending.delete(id); reject(Error(`调试命令超时：${method}`)); }, timeoutMs);
      pending.set(id, { resolve, reject, timer }); socket.send(JSON.stringify({ id, method, params }));
    });
  }
  async function evaluate(expression, timeoutMs) {
    const result = await command('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true }, timeoutMs);
    if (result.exceptionDetails) throw Error(JSON.stringify(result.exceptionDetails));
    return result.result.value;
  }
  const sdk = (expression, timeoutMs) => evaluate(`(async()=>{const {AlkaidClient}=await import('/sdk/index.js');const {createCefTransport}=await import('/src/bridge.js');const c=new AlkaidClient(createCefTransport());try{return await (${expression});}catch(e){return {error:e.code,message:e.message};}finally{c.dispose();}})()`, timeoutMs);
  await command('Runtime.enable'); await command('Page.enable');
  await waitFor(() => evaluate(`document.documentElement.dataset.runtime === 'connected'`), '页面没有连接宿主');
  return { target, socket, command, evaluate, sdk, errors };
}

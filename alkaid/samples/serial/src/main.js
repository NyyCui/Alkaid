/** 设备界面通过 SDK 调用当前绑定的串口插件。 */
import { AlkaidClient } from '../sdk/index.js';
import { createCefTransport } from './bridge.js';

const client = new AlkaidClient(createCefTransport());
const pluginId = 'com.alkaid.serial';
const find = id => document.getElementById(id);
const encoder = new TextEncoder();
let ready = false;
let busy = false;
let opened;
const portName = () => find('port').value.trim().toUpperCase();
const bytesToSend = () => encoder.encode(find('payload').value);
const call = (method, params) => client.plugin.call(pluginId, method, params);

function render() {
  const locked = !ready || busy;
  const connected = !!opened;
  find('connection').dataset.state = connected ? 'on' : 'off';
  find('connection').textContent = connected ? `${opened.port} 已打开` : ready ? '串口未连接' : '串口插件未就绪';
  for (const id of ['port', 'baud', 'refresh']) find(id).disabled = locked || connected;
  find('open').disabled = locked || connected || !/^COM[1-9]\d{0,3}$/.test(portName());
  for (const id of ['close', 'payload', 'read']) find(id).disabled = locked || !connected;
  const length = bytesToSend().length;
  find('send').disabled = locked || !connected || length === 0 || length > 4096;
}

function error(message) {
  find('error').textContent = message;
  find('error').hidden = false;
}

async function run(action) {
  if (!ready || busy) return;
  busy = true;
  find('error').hidden = true;
  render();
  try {
    await action();
  } catch (failure) {
    if (['HANDLE_EXPIRED', 'PLUGIN_UNAVAILABLE', 'SERIAL_IO_FAILED', 'TIMEOUT', 'ABORTED'].includes(failure.code)) opened = undefined;
    error(failure.message);
  } finally {
    busy = false;
    render();
  }
}

async function refreshPorts() {
  const result = await call('list', {});
  const availablePorts = result.ports;
  find('ports').replaceChildren(...availablePorts.map(port => {
    const option = document.createElement('option');
    option.value = port;
    return option;
  }));
  if (!find('port').value && availablePorts.length) find('port').value = availablePorts[0];
  find('available').textContent = availablePorts.length
    ? `Windows 枚举到 ${availablePorts.length} 个端口：${availablePorts.join('、')}`
    : 'Windows 暂未枚举到端口。接入设备后可刷新列表。';
  find('notice').textContent = '端口列表已刷新';
}

find('port').addEventListener('input', render);
find('payload').addEventListener('input', render);
find('refresh').addEventListener('click', () => run(refreshPorts));
find('open').addEventListener('click', () => run(async () => {
  opened = await call('open', { port: portName(), baudRate: Number(find('baud').value) });
  find('notice').textContent = `${opened.port} · ${opened.baudRate} bps`;
  find('bytes').textContent = '等待读取数据。';
}));
find('close').addEventListener('click', () => run(async () => {
  await call('close', { handle: opened.handle });
  opened = undefined;
  find('notice').textContent = '串口已关闭';
}));
find('send').addEventListener('click', () => run(async () => {
  const bytes = Array.from(bytesToSend());
  const result = await call('write', { handle: opened.handle, bytes });
  find('notice').textContent = `已写入 ${result.written} / ${bytes.length} 字节`;
  if (!result.complete) error('串口仅写入部分数据。请核对设备状态后再决定是否重发。');
}));
find('read').addEventListener('click', () => run(async () => {
  const { bytes } = await call('read', { handle: opened.handle, maxBytes: 4096, waitMs: 200 });
  find('bytes').textContent = bytes.length
    ? bytes.map(byte => byte.toString(16).toUpperCase().padStart(2, '0')).join(' ')
    : '本次等待期间未收到字节。';
  find('notice').textContent = `已读取 ${bytes.length} 字节`;
}));
window.addEventListener('pagehide', () => client.dispose(), { once: true });

try {
  await client.runtime.getInfo();
  document.documentElement.dataset.runtime = 'connected';
  await client.plugin.on(pluginId, 'disconnected', ({ port }) => {
    opened = undefined;
    error(`${port} 已断开，串口句柄已释放。`);
    find('notice').textContent = '可刷新端口后重新连接';
    render();
  });
  ready = true;
  await run(refreshPorts);
} catch (failure) {
  error(failure.message);
} finally {
  render();
}

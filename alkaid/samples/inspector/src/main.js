/** 普通应用只做展示；身份与能力由瑶光台裁决。 */
import { AlkaidClient } from '../sdk/index.js';
import { createCefTransport } from './bridge.js';

const client = new AlkaidClient(createCefTransport());
const find = id => document.getElementById(id);
async function refresh() {
  find('refresh').disabled = true;
  find('error').hidden = true;
  try {
    const [info, capabilities] = await Promise.all([client.runtime.getInfo(), client.runtime.getCapabilities()]);
    find('identity').textContent = info.appId;
    find('version').textContent = info.runtimeVersion;
    find('count').textContent = String(capabilities.length);
    find('methods').replaceChildren(...capabilities.map(method => {
      const item = document.createElement('li');
      item.textContent = method;
      return item;
    }));
    find('status').textContent = '已连接';
    document.documentElement.dataset.runtime = 'connected';
  } catch (error) {
    find('status').textContent = '连接失败';
    find('error').textContent = error.message;
    find('error').hidden = false;
    document.documentElement.dataset.runtime = 'error';
  } finally { find('refresh').disabled = false; }
}
find('refresh').addEventListener('click', refresh);
addEventListener('pagehide', () => client.dispose(), { once: true });
await refresh();

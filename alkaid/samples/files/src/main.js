import { AlkaidClient } from '../sdk/index.js';
import { createCefTransport } from './bridge.js';

const client = new AlkaidClient(createCefTransport());
const find = id => document.getElementById(id);
const edit = find('edit');
let path = '', action, loading = false;
const child = name => path ? `${path}/${name}` : name;
function begin(operation, title, target, value = '') {
  action = { operation, target };
  find('action-title').textContent = title;
  find('edit-error').textContent = '';
  find('name').value = value;
  edit.showModal();
}
function button(text, run) {
  const element = document.createElement('button');
  element.textContent = text;
  element.onclick = run;
  return element;
}
async function list(target = path) {
  if (loading) return;
  loading = true;
  try {
    const items = await client.workspace.list(target);
    path = target;
    find('location').textContent = path ? `工作区 / ${path}` : '工作区';
    find('up').disabled = !path;
    find('status').textContent = items.length ? `${items.length} 个项目` : '这个目录是空的。';
    items.sort((a, b) => Number(b.directory) - Number(a.directory) || a.name.localeCompare(b.name, 'zh-CN'));
    find('items').replaceChildren(...items.map(item => {
      const row = document.createElement('li');
      const target = child(item.name);
      const label = item.directory ? button(`▸ ${item.name}`, () => list(target)) : document.createElement('span');
      if (!item.directory) label.textContent = `${item.name} · ${item.bytes.toLocaleString('zh-CN')} B`;
      row.append(label, button('重命名', () => begin('rename', '重命名', target, item.name)));
      if (!item.directory) row.append(button('复制', () => begin('copy', '复制到当前目录', target)));
      return row;
    }));
  } catch (error) { find('status').textContent = error.message; }
  finally { loading = false; }
}
find('mkdir').onclick = () => begin('mkdir', '新建文件夹', path);
find('up').onclick = () => list(path.split('/').slice(0, -1).join('/'));
find('refresh').onclick = () => list();
find('reveal').onclick = () => client.workspace.reveal().catch(error => { find('status').textContent = error.message; });
find('cancel').onclick = () => edit.close();
find('name-form').onsubmit = async event => {
  event.preventDefault();
  find('submit').disabled = true;
  find('cancel').disabled = true;
  try {
    await client.workspace.change(action.operation, action.target, find('name').value.trim());
    edit.close();
    await list();
  } catch (error) { find('edit-error').textContent = error.message; }
  finally { find('submit').disabled = false; find('cancel').disabled = false; }
};
edit.addEventListener('cancel', event => { if (find('submit').disabled) event.preventDefault(); });
addEventListener('pagehide', () => client.dispose(), { once: true });
await list();
document.documentElement.dataset.runtime = 'connected';

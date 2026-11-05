/** 桌面只负责 UI 状态；应用列表和平台操作通过 SDK 取得真实结果。 */
import { AlkaidClient } from '../sdk/index.js';
import { createCefTransport } from './bridge.js';
import { setupExperience } from './interactions.js';
import { setupPackages } from './packages.js';
import { setupWorkspace } from './workspace.js';

const client = new AlkaidClient(createCefTransport());
const find = id => document.getElementById(id);
let runtimeInfo;
let connecting = false;
let layer = 25;
let toastTimer;
let appsLoading = false;
let disposed = false;
let appsSignature;
let currentApps = [];
let detailAppId;
let permissionDraft;
let savingPermissions = false;
let clearingData = false;
let clearBrowserData = false;
const settingsKey = 'desktop.appearance';
const closeActionKey = 'desktop.closeAction';
const defaultSettings = { palette: 'cream', clockFormat: '24', seconds: false };
let closeAction = 'ask';
let closeActionReady = false;
let handlingClose = false;
let clockFormatter;
const busyApps = new Set();
const stateLabels = { stopped: '未运行', starting: '启动中', running: '运行中', closing: '关闭中', unavailable: '不可用' };
const permissionLabels = { 'storage.kv': '保存私有数据', 'files.private': '读写私有文件',
  'files.userSelected.read': '读取选定文件', 'files.userSelected.write': '写入选定文件', 'net.request': '访问授权网络端点' };

function updateAppDetail() {
  const dialog = find('app-detail-dialog');
  const app = currentApps.find(item => item.appId === detailAppId);
  if (!app) { if (dialog.open) dialog.close(); return false; }
  find('app-detail-title').textContent = app.name;
  find('app-detail-id').textContent = app.appId;
  find('app-detail-state').textContent = `v${app.version} · ${stateLabels[app.state]}`;
  find('app-detail-publisher').textContent = app.publisherName || (app.kind === 'bundled' ? '瑶光台随附' : '发布者信息不可用');
  find('app-detail-fingerprint').textContent = app.publisherKeyId || '—';
  const granted = new Set(app.grantedPermissions ?? []);
  const canEdit = app.kind === 'installed' && !!app.declaredPermissions?.length;
  const stopped = ['stopped', 'unavailable'].includes(app.state);
  const editing = !!permissionDraft;
  find('edit-app-permissions').hidden = !canEdit || editing;
  find('edit-app-permissions').disabled = !stopped || clearingData;
  find('app-permissions-hint').hidden = !canEdit || (!editing && stopped);
  find('app-permissions-hint').textContent = stopped ? '保存后，下次启动应用时使用新授权。' : '请先关闭应用，再调整授权。';
  find('app-permissions-actions').hidden = !editing;
  find('save-app-permissions').disabled = savingPermissions || !stopped;
  find('cancel-app-permissions').disabled = savingPermissions;
  find('close-app-detail').disabled = savingPermissions || clearingData;
  find('app-data-section').hidden = app.kind !== 'installed';
  find('app-data-busy').hidden = stopped;
  find('clear-app-data').disabled = !stopped || editing || clearingData;
  find('clear-browser-data').disabled = !stopped || editing || clearingData || app.browserCleanupPending === true;
  find('browser-cleanup-pending').hidden = !app.browserCleanupPending;
  find('browser-cleanup-pending').textContent = app.browserCleanupFailed
    ? '浏览器数据清理失败。请解除文件占用或检查目录权限，再重启瑶光台重试。'
    : '等待清理。请退出并重开瑶光台，再启动此应用。';
  find('confirm-clear-data').disabled = !stopped || clearingData;
  const endpoints = new Map((app.networkEndpoints ?? []).map(endpoint => [endpoint.scope, endpoint]));
  const permissions = (app.declaredPermissions ?? []).map(permission => {
    const endpoint = endpoints.get(permission);
    const label = endpoint ? `${endpoint.baseUrl} · ${endpoint.methods.join('/')}` :
      permissionLabels[permission] ?? (permission.startsWith('plugin.') ? `调用插件 ${permission.slice(7)}` : permission);
    const row = document.createElement('li');
    if (!editing) row.textContent = `${label} · ${granted.has(permission) ? '已授权' : '未授权'}`;
    else {
      const option = document.createElement('label');
      const input = document.createElement('input');
      input.type = 'checkbox';
      input.value = permission;
      input.checked = permissionDraft.has(permission);
      input.disabled = savingPermissions || !stopped ||
        (permission.startsWith('net.endpoint:') && !permissionDraft.has('net.request'));
      option.append(input, document.createTextNode(label));
      row.append(option);
    }
    return row;
  });
  const permissionsList = find('app-detail-permissions');
  permissionsList.replaceChildren(...permissions);
  if (!permissions.length) {
    const row = document.createElement('li');
    row.textContent = '无额外能力声明';
    permissionsList.append(row);
  }
  const plugins = Object.entries(app.pluginBindings ?? {}).map(([id, version]) => `${id} · v${version}`);
  for (const [id, items, fallback] of [
    ['app-detail-plugins', plugins, '当前没有绑定插件'],
  ]) {
    find(id).replaceChildren(...(items.length ? items : [fallback]).map(text => {
      const row = document.createElement('li');
      row.textContent = text;
      return row;
    }));
  }
  find('app-detail-error').textContent = app.error ?? '';
  find('app-detail-error').hidden = !app.error;
  return true;
}

find('apps-list').addEventListener('click', event => {
  const button = event.target.closest('[data-app-detail]');
  if (!button) return;
  detailAppId = button.dataset.appDetail;
  if (updateAppDetail()) find('app-detail-dialog').showModal();
});
find('close-app-detail').addEventListener('click', () => find('app-detail-dialog').close());
const auditOperations = {
  install: '安装或更新应用', uninstall: '卸载应用', rollback: '恢复版本', permissions: '调整授权',
  clearData: '清理应用数据', clearBrowserData: '清理浏览器数据', pluginInstall: '安装或更新插件',
  pluginUninstall: '卸载插件', pluginExit: '插件连接中断', pluginCall: '插件无法调用', pack: '打包',
  publisherBackup: '发布者备份或恢复',
};
async function openAudit(appId) {
  const dialog = find('audit-dialog');
  const status = find('audit-status');
  const records = find('audit-records');
  find('audit-title').textContent = appId ? `${appId} 的操作记录` : '最近操作';
  records.replaceChildren();
  status.textContent = '正在读取…';
  dialog.showModal();
  try {
    const entries = (await client.apps.listAudit()).filter(entry => !appId || entry.appId === appId);
    records.replaceChildren(...entries.map(entry => {
      const row = document.createElement('li');
      const operation = auditOperations[entry.operation] ?? entry.operation;
      const code = entry.code === 'OK' ? '成功' : entry.code === 'PENDING_RESTART' ? '等待重启' : entry.code;
      row.textContent = [new Date(entry.time * 1000).toLocaleString('zh-CN'), operation, entry.appId, entry.target, code]
        .filter(Boolean).join(' · ');
      return row;
    }));
    status.textContent = entries.length ? `显示 ${entries.length} 条本机记录` : '暂无相关记录';
  } catch (error) { status.textContent = error.message; }
}
find('open-audit').addEventListener('click', () => openAudit());
find('open-app-audit').addEventListener('click', () => openAudit(detailAppId));
find('close-audit').addEventListener('click', () => find('audit-dialog').close());
find('app-detail-dialog').addEventListener('cancel', event => { if (savingPermissions || clearingData) event.preventDefault(); });
find('app-detail-dialog').addEventListener('close', () => {
  detailAppId = undefined;
  permissionDraft = undefined;
  find('app-permissions-error').hidden = true;
  find('app-data-error').hidden = true;
});
function openCleanupConfirmation(browser) {
  clearBrowserData = browser;
  find('app-data-error').hidden = true;
  const scope = browser ? '浏览器数据' : '应用数据';
  find('clear-data-title').textContent = `清理「${find('app-detail-title').textContent}」的${scope}？`;
  find('clear-data-description').textContent = browser
    ? '重启瑶光台后将永久删除此应用的缓存、Cookie 和网页存储。清理完成前，此应用暂停启动。'
    : '此操作将永久删除该应用的私有键值、私有文件和应用配置。';
  find('confirm-clear-data').textContent = browser ? '确认并等待重启' : '永久删除';
  find('clear-data-dialog').showModal();
}
find('clear-app-data').addEventListener('click', () => openCleanupConfirmation(false));
find('clear-browser-data').addEventListener('click', () => openCleanupConfirmation(true));
find('cancel-clear-data').addEventListener('click', () => find('clear-data-dialog').close());
find('confirm-clear-data').addEventListener('click', async () => {
  if (clearingData || !detailAppId) return;
  clearingData = true;
  find('clear-data-dialog').close();
  updateAppDetail();
  try {
    const action = clearBrowserData ? 'clearBrowserData' : 'clearData';
    const { taskId } = await client.apps[action](detailAppId);
    await waitForAppTask(taskId);
    if (clearBrowserData) await loadApps(true);
    toast(clearBrowserData ? '已登记浏览器数据清理，请重启瑶光台' : '应用数据已清理');
  } catch (error) {
    const errorBox = find('app-data-error');
    errorBox.textContent = error.message;
    errorBox.hidden = false;
  } finally {
    clearingData = false;
    if (updateAppDetail()) find(clearBrowserData ? 'close-app-detail' : 'clear-app-data').focus();
  }
});
find('edit-app-permissions').addEventListener('click', () => {
  const app = currentApps.find(item => item.appId === detailAppId);
  if (!app) return;
  permissionDraft = new Set(app.grantedPermissions ?? []);
  updateAppDetail();
  find('app-detail-permissions').querySelector('input:not(:disabled)')?.focus();
});
find('app-detail-permissions').addEventListener('change', event => {
  const input = event.target;
  if (!permissionDraft || input.type !== 'checkbox') return;
  if (input.checked) permissionDraft.add(input.value);
  else {
    permissionDraft.delete(input.value);
    if (input.value === 'net.request') for (const permission of permissionDraft) {
      if (permission.startsWith('net.endpoint:')) permissionDraft.delete(permission);
    }
  }
  for (const option of find('app-detail-permissions').querySelectorAll('input')) {
    if (!option.value.startsWith('net.endpoint:')) continue;
    option.disabled = !permissionDraft.has('net.request');
    option.checked = permissionDraft.has(option.value);
  }
});
find('cancel-app-permissions').addEventListener('click', () => {
  permissionDraft = undefined;
  find('app-permissions-error').hidden = true;
  updateAppDetail();
});
async function waitForAppTask(taskId) {
  for (let attempt = 0; attempt < 75; ++attempt) {
    const task = await client.apps.getTask(taskId);
    if (task.status === 'succeeded') return;
    if (task.status !== 'running') throw Error(task.message);
    await new Promise(resolve => setTimeout(resolve, 200));
  }
  throw Error('操作仍在处理中，可在应用管理查看结果。');
}
find('save-app-permissions').addEventListener('click', async () => {
  if (!permissionDraft || savingPermissions) return;
  savingPermissions = true;
  const errorBox = find('app-permissions-error');
  errorBox.hidden = true;
  updateAppDetail();
  try {
    const { taskId } = await client.apps.setPermissions(detailAppId, [...permissionDraft]);
    await waitForAppTask(taskId);
    permissionDraft = undefined;
    await loadApps(true);
    toast('应用授权已保存');
  } catch (error) {
    errorBox.textContent = error.message;
    errorBox.hidden = false;
  } finally {
    savingPermissions = false;
    updateAppDetail();
  }
});

function toast(message) {
  clearTimeout(toastTimer);
  find('toast').textContent = message;
  find('toast').hidden = false;
  toastTimer = setTimeout(() => { find('toast').hidden = true; }, 4000);
}
function setStatus(state, label) {
  document.documentElement.dataset.runtime = state;
  find('connection').textContent = label;
  find('runtime-status').textContent = label;
}
function showPanel(name) {
  const panel = find(`panel-${name}`);
  panel.hidden = false;
  panel.style.zIndex = String(++layer);
  document.querySelector(`.dock-item[data-open="${name}"]`)?.classList.add('running');
  panel.querySelector('button')?.focus();
}
function actionButton(app, action, label, className = 'text-button') {
  const button = document.createElement('button');
  button.className = className;
  button.textContent = label;
  button.dataset.appId = app.appId;
  button.dataset.action = action;
  button.disabled = busyApps.has(app.appId) || ['closing', 'starting', 'unavailable'].includes(app.state);
  button.setAttribute('aria-label', `${label} ${app.name}`);
  return button;
}
function appGlyph(app, className) {
  const span = document.createElement('span');
  span.className = `${className} application-glyph`;
  span.setAttribute('aria-hidden', 'true');
  const marks = {
    'org.alkaid.files': ['folder', '<path fill="#fff4cc" stroke="none" d="M3 7a2 2 0 0 1 2-2h5l2 2h7a2 2 0 0 1 2 2v1H3z"/><path fill="#fffdf0" d="M3 10h18v7a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>'],
    'org.alkaid.inspector': ['inspect', '<rect x="4" y="4" width="16" height="16" rx="4"/><path d="M7 13h3l2-5 2 8 1.5-3H18"/>'],
    'org.alkaid.notes': ['folder', '<path d="M6 4h12a1 1 0 0 1 1 1v10l-5 5H6a1 1 0 0 1-1-1V5a1 1 0 0 1 1-1Z"/><path d="M14 20v-5h5M8 8h8m-8 4h8"/>'],
  };
  const [theme, mark] = marks[app.appId] ?? ['generic', '<rect x="4" y="4" width="16" height="16" rx="4"/><path d="M8 9h8M8 13h5"/>'];
  span.dataset.glyph = theme;
  span.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.55" stroke-linecap="round" stroke-linejoin="round">${mark}</svg>`;
  if (app.icon) {
    const image = document.createElement('img');
    image.alt = '';
    image.onload = () => span.dataset.glyph = 'custom';
    image.onerror = () => image.remove();
    image.src = `data:image/webp;base64,${app.icon}`;
    span.append(image);
  }
  return span;
}
function renderApps(apps) {
  const selectedId = find('desktop-apps').querySelector('.selected')?.dataset.appId;
  const focused = document.activeElement;
  const focusKey = focused?.dataset.appId ? { id: focused.dataset.appId, action: focused.dataset.action } : null;
  const rows = [], icons = [], dock = [];
  for (const app of apps) {
    const row = document.createElement('div');
    row.className = 'app-row';
    row.dataset.state = app.state;
    const info = document.createElement('div');
    info.className = 'app-identity';
    const caption = document.createElement('div');
    const name = document.createElement('strong');
    const detail = document.createElement('small');
    name.textContent = app.name;
    detail.textContent = app.error || `v${app.version} · ${app.kind === 'bundled' ? '内置应用' : app.appId}`;
    const status = document.createElement('span');
    status.className = 'app-state-pill';
    status.textContent = stateLabels[app.state];
    caption.append(name, detail);
    info.append(appGlyph(app, 'app-list-glyph'), caption);
    const actions = document.createElement('div');
    actions.className = 'app-actions';
    const inspect = document.createElement('button');
    inspect.className = 'text-button';
    inspect.textContent = '详情';
    inspect.dataset.appDetail = app.appId;
    inspect.setAttribute('aria-label', `查看 ${app.name} 的详情`);
    const more = document.createElement('details'); more.className = 'app-more';
    const summary = document.createElement('summary'); summary.textContent = '更多';
    summary.setAttribute('aria-label', `${app.name} 的更多操作`);
    const menu = document.createElement('div'); menu.className = 'menu-sheet';
    more.append(summary, menu);
    menu.append(inspect);
    actions.append(status, actionButton(app, app.state === 'running' ? 'focus' : 'launch', app.state === 'running' ? '切换' : '打开', 'app-open-button'));
    if (['starting', 'running', 'closing'].includes(app.state)) menu.append(actionButton(app, 'close', '关闭应用'));
    if (app.kind === 'installed') {
      for (const [action, label] of [['rollback', '恢复'], ['uninstall', '卸载']]) {
        if (action === 'rollback' && !app.canRollback) continue;
        const button = document.createElement('button');
        button.className = 'text-button'; button.textContent = label;
        button.dataset.packageAction = action; button.dataset.packageId = app.appId; button.dataset.packageName = app.name;
        button.disabled = !['stopped', 'unavailable'].includes(app.state);
        menu.append(button);
      }
    }
    actions.append(more);
    row.dataset.searchName = `${app.name} ${app.appId}`.toLocaleLowerCase();
    row.append(info, actions);
    row.classList.add('managed-app');
    rows.push(row);
    const icon = actionButton(app, 'desktop-launch', '', 'desktop-icon');
    icon.classList.toggle('selected', app.appId === selectedId);
    const symbol = appGlyph(app, 'desktop-icon-emoji app-symbol');
    const label = document.createElement('span');
    label.className = 'desktop-icon-label';
    label.textContent = app.name;
    icon.title = `双击打开 ${app.name}`;
    icon.setAttribute('aria-label', `打开 ${app.name}`);
    icon.append(symbol, label);
    icons.push(icon);
    if (['starting', 'running', 'closing'].includes(app.state)) {
      const item = actionButton(app, 'focus', app.name.slice(0, 1), 'dock-item app-dock');
      item.classList.toggle('running', app.state === 'running');
      item.dataset.tooltip = `${app.name} · ${stateLabels[app.state]}`;
      item.setAttribute('aria-label', `切换到 ${app.name}`);
      item.replaceChildren(appGlyph(app, 'dock-glyph glyph-application'));
      dock.push(item);
    }
  }
  find('apps-list').replaceChildren(...rows);
  find('desktop-apps').replaceChildren(...icons);
  find('running-apps').replaceChildren(...dock);
  find('apps-empty').hidden = apps.length > 0;
  find('app-count').textContent = `${apps.length} 个应用`;
  if (focusKey) [...document.querySelectorAll('[data-app-id]')].find(button =>
    button.dataset.appId === focusKey.id && button.dataset.action === focusKey.action)?.focus();
}
async function loadApps(quiet = false) {
  if (appsLoading || disposed) return;
  appsLoading = true;
  find('apps-loading').hidden = quiet || appsSignature !== undefined;
  if (!quiet) find('refresh').disabled = true;
  try {
    const allApps = await client.apps.list();
    if (disposed) return;
    currentApps = allApps.filter(app => app.kind !== 'desktop');
    updateWorkspace(currentApps);
    const signature = JSON.stringify(currentApps);
    if (signature !== appsSignature) {
      appsSignature = signature;
      renderApps(currentApps);
      if (detailAppId) updateAppDetail();
    }
    find('apps-error').hidden = true;
  } catch (error) {
    find('apps-error').textContent = error.code === 'RUNTIME_UNAVAILABLE'
      ? '请从瑶光台打开桌面，浏览器预览无法读取已安装应用。' : `无法读取应用：${error.message}`;
    find('apps-error').hidden = false;
  } finally { appsLoading = false; find('apps-loading').hidden = true; find('refresh').disabled = false; }
}
async function appAction(appId, action) {
  if (busyApps.has(appId)) return;
  busyApps.add(appId);
  renderApps(currentApps);
  try {
    const app = await client.apps[action](appId);
    if (app.kind === 'installed') {
      for (const panel of document.querySelectorAll('.workspace-panel:not([hidden])'))
        panel.querySelector('[data-close]')?.click();
    }
  }
  catch (error) { toast(`操作失败：${error.message}`); }
  finally { busyApps.delete(appId); renderApps(currentApps); await loadApps(true); }
}
document.addEventListener('click', event => {
  const button = event.target.closest('button[data-app-id]');
  if (!button || button.disabled) return;
  if (button.dataset.action === 'desktop-launch' && event.detail !== 0) return;
  appAction(button.dataset.appId, button.dataset.action === 'desktop-launch' ? 'launch' : button.dataset.action);
});
find('desktop-apps').addEventListener('dblclick', event => {
  const button = event.target.closest('button[data-app-id]');
  if (button && !button.disabled) appAction(button.dataset.appId, 'launch');
});
async function connect() {
  if (connecting) return;
  connecting = true;
  find('reconnect').disabled = true;
  find('exit').disabled = true;
  setStatus('connecting', '正在连接…');
  try {
    runtimeInfo = await client.runtime.getInfo();
    const capabilities = await client.runtime.getCapabilities();
    find('runtime-version').textContent = runtimeInfo.runtimeVersion;
    find('runtime-app').textContent = runtimeInfo.appId;
    find('exit').disabled = !capabilities.includes('runtime.exit');
    if (capabilities.includes('config.platform.get')) await Promise.all([loadSettings(), loadCloseAction()]);
    setStatus('connected', '已连接瑶光台');
  } catch (error) {
    runtimeInfo = undefined;
    find('runtime-version').textContent = '—';
    find('runtime-app').textContent = '—';
    setStatus('error', error.code === 'RUNTIME_UNAVAILABLE' ? '浏览器预览' : '连接失败');
  } finally { connecting = false; find('reconnect').disabled = false; }
  await loadApps();
}
document.querySelectorAll('[data-open]').forEach(button => button.addEventListener('click', () => showPanel(button.dataset.open)));
document.querySelectorAll('[data-close]').forEach(button => button.addEventListener('click', () => {
  find(`panel-${button.dataset.close}`).hidden = true;
  const dock = document.querySelector(`.dock-item[data-open="${button.dataset.close}"]`);
  dock?.classList.remove('running');
  (dock ?? document.querySelector('[data-open=tools]'))?.focus();
}));
document.querySelectorAll('.workspace-panel').forEach(panel => {
  panel.addEventListener('pointerdown', () => { panel.style.zIndex = String(++layer); });
  const title = panel.querySelector('.panel-titlebar');
  title.addEventListener('pointerdown', event => {
    if (event.button !== 0 || event.target.closest('button')) return;
    const start = panel.getBoundingClientRect();
    const origin = { x: event.clientX, y: event.clientY };
    title.setPointerCapture(event.pointerId);
    const move = next => {
      panel.style.left = `${Math.max(0, Math.min(innerWidth - panel.offsetWidth, start.left + next.clientX - origin.x))}px`;
      panel.style.top = `${Math.max(52, Math.min(innerHeight - 110, start.top + next.clientY - origin.y))}px`;
    };
    title.addEventListener('pointermove', move);
    title.addEventListener('lostpointercapture', () => title.removeEventListener('pointermove', move), { once: true });
  });
});
addEventListener('resize', () => {
  for (const panel of document.querySelectorAll('.workspace-panel:not([hidden])')) {
    const rect = panel.getBoundingClientRect();
    panel.style.left = `${Math.max(0, Math.min(rect.left, innerWidth - panel.offsetWidth))}px`;
    panel.style.top = `${Math.max(52, Math.min(rect.top, innerHeight - 110))}px`;
  }
});
find('refresh').addEventListener('click', () => loadApps());
find('reconnect').addEventListener('click', connect);
find('exit').addEventListener('click', () => find('exit-dialog').showModal());
client.runtime.onExitCancelled(message => toast(message));
find('cancel-exit').addEventListener('click', () => find('exit-dialog').close());
find('confirm-exit').addEventListener('click', async () => {
  find('confirm-exit').disabled = true;
  try { await client.runtime.exit(); }
  catch (error) { toast(`退出失败：${error.message}`); }
  finally { find('confirm-exit').disabled = false; find('exit-dialog').close(); }
});
const closeDialog = find('host-close-dialog');
const closeOptions = find('close-action-options').querySelectorAll('input');
const closeButtons = ['cancel-host-close', 'minimize-on-close', 'exit-on-close'].map(find);
function renderCloseAction() {
  for (const input of closeOptions) {
    input.checked = input.value === closeAction;
    input.disabled = !closeActionReady || handlingClose;
  }
  for (const button of closeButtons) button.disabled = handlingClose;
  find('remember-close').disabled = !closeActionReady || handlingClose;
}
async function loadCloseAction() {
  try {
    const { value } = await client.config.platform.get(closeActionKey);
    closeAction = ['minimize', 'exit'].includes(value) ? value : 'ask';
    closeActionReady = true;
  } catch (error) { toast(`读取关闭偏好失败：${error.message}`); }
  renderCloseAction();
}
async function changeCloseAction(action, perform, remember) {
  if (handlingClose) return;
  handlingClose = true;
  renderCloseAction();
  try {
    if (remember) {
      await client.config.platform.set(closeActionKey, action);
      closeAction = action;
    }
    if (perform) {
      closeDialog.close();
      if (action === 'minimize') await client.window.minimize();
      else await client.runtime.exit();
    }
  } catch (error) { toast(error.message); }
  finally { handlingClose = false; renderCloseAction(); }
}
find('host-close').onclick = () => {
  if (handlingClose || closeDialog.open) return;
  if (closeAction !== 'ask') {
    void changeCloseAction(closeAction, true, false);
    return;
  }
  find('remember-close').checked = false;
  closeDialog.showModal();
};
find('cancel-host-close').onclick = () => closeDialog.close();
find('minimize-on-close').onclick = () => changeCloseAction('minimize', true, find('remember-close').checked);
find('exit-on-close').onclick = () => changeCloseAction('exit', true, find('remember-close').checked);
closeDialog.addEventListener('cancel', event => { if (handlingClose) event.preventDefault(); });
find('close-action-options').addEventListener('change', event => {
  event.stopPropagation();
  void changeCloseAction(event.target.value, false, true);
});
renderCloseAction();
function updateClock() {
  find('clock').textContent = clockFormatter.format(new Date());
}
function settingsStatus(message, error = false) {
  const status = find('settings-status');
  status.textContent = message;
  status.classList.toggle('error', error);
}
function setSettingsEnabled(enabled) {
  for (const id of ['settings-fields', 'save-settings', 'reset-settings']) find(id).disabled = !enabled;
}
function populateSettings(settings) {
  for (const option of find('desktop-settings').querySelectorAll('input[name="palette"]'))
    option.checked = option.value === settings.palette;
  for (const option of find('clock-format').querySelectorAll('input')) option.checked = option.value === settings.clockFormat;
  find('clock-seconds').checked = settings.seconds;
}
function applySettings(settings) {
  document.documentElement.dataset.palette = settings.palette;
  clockFormatter = new Intl.DateTimeFormat('zh-CN', {
    month: 'long', day: 'numeric', weekday: 'short', hour: '2-digit', minute: '2-digit',
    hourCycle: settings.clockFormat === '12' ? 'h12' : 'h23',
    ...(settings.seconds ? { second: '2-digit' } : {}),
  });
  populateSettings(settings);
  updateClock();
}
async function loadSettings() {
  setSettingsEnabled(false);
  try {
    const { value } = await client.config.platform.get(settingsKey);
    applySettings({
      palette: ['cream', 'forest', 'mist'].includes(value?.palette) ? value.palette : defaultSettings.palette,
      clockFormat: value?.clockFormat === '12' ? '12' : defaultSettings.clockFormat,
      seconds: value?.seconds === true,
    });
    settingsStatus('当前设置已保存');
    setSettingsEnabled(true);
  } catch (error) {
    settingsStatus(`读取设置失败：${error.message}`, true);
  }
}
find('desktop-settings').addEventListener('submit', async event => {
  event.preventDefault();
  if (find('save-settings').disabled) return;
  const settings = {
    palette: find('desktop-settings').querySelector('input[name="palette"]:checked').value,
    clockFormat: find('clock-format').querySelector('input:checked').value,
    seconds: find('clock-seconds').checked,
  };
  setSettingsEnabled(false);
  settingsStatus('正在保存…');
  try {
    await client.config.platform.set(settingsKey, settings);
    applySettings(settings);
    settingsStatus('设置已保存。');
  } catch (error) {
    settingsStatus(`保存设置失败：${error.message}`, true);
  } finally { setSettingsEnabled(true); }
});
find('settings-fields').addEventListener('change', () => settingsStatus('有未保存的更改'));
find('reset-settings').addEventListener('click', () => {
  populateSettings(defaultSettings);
  settingsStatus('已选中默认选项，保存后生效。');
});
applySettings(defaultSettings);
const clockTimer = setInterval(updateClock, 1000);
// 暂用有界单请求轮询；事件订阅协议接入后替换，未变化时保留 DOM 与键盘焦点。
const appsTimer = setInterval(() => { if (runtimeInfo && !connecting) loadApps(true); }, 1000);
const disposePackages = setupPackages(client, () => loadApps(true), toast);
addEventListener('pagehide', () => { disposed = true; disposePackages(); clearInterval(appsTimer); clearInterval(clockTimer); clearTimeout(toastTimer); client.dispose(); }, { once: true });
setupExperience(client, showPanel, toast);
const updateWorkspace = setupWorkspace(client, () => loadApps(true), toast);

// 壁纸图像只保存在受信桌面的独立浏览器配置内。
function wallpaperStore(mode, action) {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open('alkaid-wallpaper', 1);
    request.onupgradeneeded = () => request.result.createObjectStore('images');
    request.onerror = () => reject(request.error);
    request.onsuccess = () => {
      const db = request.result;
      const transaction = db.transaction('images', mode);
      const item = action(transaction.objectStore('images'));
      transaction.oncomplete = () => { db.close(); resolve(item.result); };
      transaction.onabort = () => { db.close(); reject(transaction.error); };
    };
  });
}
function showWallpaper(value) {
  const image = find('wallpaper-image');
  image.hidden = !value;
  if (value) image.src = value.data;
  else image.removeAttribute('src');
  find('clear-wallpaper').hidden = !value;
  find('wallpaper-status').textContent = value ? value.name : '使用内置配色';
}
let wallpaperBusy = false;
async function changeWallpaper(action) {
  if (wallpaperBusy) return;
  wallpaperBusy = true;
  find('choose-wallpaper').disabled = find('clear-wallpaper').disabled = true;
  try { showWallpaper(await action()); }
  catch (error) { find('wallpaper-status').textContent = error.message || '无法保存壁纸'; }
  finally {
    wallpaperBusy = false;
    find('choose-wallpaper').disabled = find('clear-wallpaper').disabled = false;
  }
}
find('choose-wallpaper').onclick = () => find('wallpaper-file').click();
find('wallpaper-file').onchange = event => {
  const input = event.currentTarget;
  const file = input.files[0];
  input.value = '';
  if (!file) return;
  void changeWallpaper(async () => {
    if (!['image/png', 'image/jpeg', 'image/webp'].includes(file.type) || file.size > 8 * 1024 * 1024)
      throw Error('请选择不超过 8 MiB 的 PNG、JPEG 或 WebP 图片。');
    const data = await new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onload = () => resolve(reader.result);
      reader.onerror = () => reject(reader.error);
      reader.readAsDataURL(file);
    });
    const image = new Image();
    image.src = data;
    try { await image.decode(); } catch { throw Error('无法读取这张图片，请选择其他文件。'); }
    const value = { name: file.name, data };
    await wallpaperStore('readwrite', store => store.put(value, 'current'));
    return value;
  });
};
find('clear-wallpaper').onclick = () => changeWallpaper(async () => {
  await wallpaperStore('readwrite', store => store.delete('current'));
  return null;
});
void changeWallpaper(() => wallpaperStore('readonly', store => store.get('current')));
await connect();

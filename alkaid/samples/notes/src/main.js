/** 便签业务只在前端：宿主提供通用设置、私有文本和受控文件句柄。 */
import { AlkaidClient } from '../sdk/index.js';
import { createCefTransport } from './bridge.js';
const client = new AlkaidClient(createCefTransport());
const find = id => document.getElementById(id);
const editor = find('editor');
const maxBytes = 8 * 1024 * 1024;
let grant = null, name = '未命名便签', savedText = '', revision = 0, persisted = -1, ready = false, busy = false;
let draftWork = null, settingWork = Promise.resolve(), replaceAction = null, previousText = '';
let draftTimer, activeWork = null, operationController = null, closing = false, closeChoice = null, currentEncoding = 'utf-8', savedEncoding = 'utf-8';
const bytes = text => new TextEncoder().encode(text).length;
function failure(error) { find('error').textContent = `${error.message ?? error}${error.code ? `（${error.code}）` : ''}`; find('error').hidden = false; }
function clearError() { find('error').hidden = true; }
function progress(info) {
  find('transfer-label').textContent = info.totalBytes ? `处理中 · ${Math.round(info.processedBytes / info.totalBytes * 100)}%` : `已处理 ${info.processedBytes.toLocaleString()} 字节`;
  find('transfer-progress').value = info.totalBytes ? info.processedBytes / info.totalBytes : 0;
}
function update() {
  document.body.dataset.closing = String(closing);
  find('transfer').hidden = !busy;
  find('encoding-label').textContent = `TEXT / ${currentEncoding.toUpperCase()}`;
  find('document-name').textContent = name; find('heading').textContent = name;
  find('file-state').textContent = grant ? (grant.writable ? '已授权保存到文件' : '已打开文件 · 保存时选择目标') : '私有草稿';
  find('count').textContent = `${editor.value.length.toLocaleString()} 字符 · ${bytes(editor.value).toLocaleString()} B`;
  find('save-state').textContent = busy ? '处理中…' : grant && savedText === editor.value && currentEncoding === savedEncoding ? '内容与本次文件快照一致' : '尚未保存到文件';
  for (const id of ['new','open','save','save-as']) find(id).disabled = !ready || busy || closing;
  editor.disabled = !ready || busy || closing;
  find('font-size').disabled = !ready || closing;
  find('encoding').disabled = !ready || busy || closing;
}
async function persistDraft() {
  clearTimeout(draftTimer);
  if (draftWork) return draftWork;
  draftWork = (async () => {
    while (persisted !== revision) {
      const snapshot = revision;
      const content = JSON.stringify({ text: editor.value, name, encoding: currentEncoding });
      find('draft-state').textContent = '正在保存私有草稿…';
      await client.files.writePrivateText('draft.json', content);
      persisted = snapshot;
    }
    find('draft-state').textContent = '私有草稿已保存';
  })();
  try { await draftWork; }
  catch (error) { find('draft-state').textContent = '草稿尚未保存，请保存到文件或继续编辑重试'; throw error; }
  finally { draftWork = null; }
}
async function release(handle) { if (handle) await client.files.close(handle.handle).catch(() => {}); }
async function run(action) {
  if (busy || !ready || closing) return;
  busy = true; clearError(); operationController = new AbortController(); update();
  activeWork = (async () => {
    try { await action(operationController.signal); }
    catch (error) { failure(error); }
    finally { busy = false; operationController = null; update(); }
  })();
  try { await activeWork; } finally { activeWork = null; }
}
find('cancel-transfer').addEventListener('click', () => operationController?.abort());
async function replace(nextGrant, text, nextName, encoding = 'utf-8') {
  if (bytes(text) > maxBytes) {
    await release(nextGrant); throw Error('此文件超过便签的草稿容量限制，当前内容已保留。');
  }
  await release(grant); grant = nextGrant; name = nextName; currentEncoding = encoding; savedEncoding = encoding; find('encoding').value = encoding;
  editor.value = text; previousText = editor.value; savedText = nextGrant ? editor.value : ''; revision++;
  update(); await persistDraft();
}
function requestReplace(action) {
  if (editor.value) { replaceAction = action; find('replace-dialog').showModal(); }
  else run(action);
}
find('replace-cancel').addEventListener('click', () => { replaceAction = null; find('replace-dialog').close(); });
find('replace-confirm').addEventListener('click', () => { const action = replaceAction; replaceAction = null; find('replace-dialog').close(); if (action) run(action); });
find('new').addEventListener('click', () => requestReplace(() => replace(null, '', '未命名便签')));
find('open').addEventListener('click', () => requestReplace(async signal => {
  const selected = await client.dialog.openFile();
  if (!selected) return;
  let encoding = 'utf-8';
  try {
    const text = await client.files.readText(selected.handle, { signal, maxBytes, encoding: find('encoding').value, onProgress: info => { encoding = info.encoding; progress(info); } });
    await replace(selected, text, selected.name, encoding);
  } catch (error) { if (grant !== selected) await release(selected); throw error; }
}));
async function save(asNew, signal) {
  let selected = grant;
  if (asNew || !selected?.writable) selected = await client.dialog.saveFile();
  if (!selected) return false;
  try { await client.files.writeText(selected.handle, editor.value, { signal, encoding: currentEncoding, onProgress: progress }); }
  catch (error) { if (selected !== grant) await release(selected); throw error; }
  if (selected !== grant) await release(grant);
  grant = selected; name = selected.name; savedText = editor.value; savedEncoding = currentEncoding; revision++;
  await persistDraft(); return true;
}
find('save').addEventListener('click', () => run(signal => save(false, signal)));
find('save-as').addEventListener('click', () => run(signal => save(true, signal)));
find('encoding').addEventListener('change', () => { currentEncoding = find('encoding').value === 'auto' ? 'utf-8' : find('encoding').value; revision++; update(); scheduleDraft(); });
function scheduleDraft() {
  clearTimeout(draftTimer); find('draft-state').textContent = '等待保存私有草稿…';
  draftTimer = setTimeout(() => persistDraft().catch(failure), 350);
}
editor.addEventListener('input', () => {
  if (bytes(editor.value) > maxBytes) {
    editor.value = previousText; failure(Error('内容超过便签容量限制，本次输入未保留。')); return;
  }
  previousText = editor.value; revision++; update(); scheduleDraft();
});
find('font-size').addEventListener('change', () => {
  const fontSize = Number(find('font-size').value); editor.style.fontSize = `${fontSize}px`;
  settingWork = settingWork.catch(() => {}).then(() => client.storage.set('preferences', { fontSize }));
  settingWork.catch(failure);
});
document.addEventListener('keydown', event => {
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 's') { event.preventDefault(); run(signal => save(event.shiftKey, signal)); }
});
addEventListener('beforeunload', event => { if (ready && persisted !== revision) { event.preventDefault(); event.returnValue = ''; } });
addEventListener('pagehide', () => { clearTimeout(draftTimer); client.dispose(); }, { once: true });
function restoreAfterClose() {
  closing = false; closeChoice?.resolve(false); closeChoice = null;
  if (find('close-dialog').open) find('close-dialog').close(); update();
}
async function requestClose({ reason, signal }) {
  closing = true; update();
  signal.addEventListener('abort', restoreAfterClose, { once: true });
  try {
    if (activeWork) await activeWork;
    await settingWork; await persistDraft();
    if (signal.aborted) { restoreAfterClose(); return false; }
    if ((!editor.value && !grant) || (grant && savedText === editor.value && savedEncoding === currentEncoding)) return true;
    find('close-title').textContent = reason === 'platform' ? '退出前，保存这份便签？' : '关闭前，保存这份便签？';
    find('close-error').hidden = true; find('close-dialog').showModal();
    return await new Promise(resolve => { closeChoice = { resolve, signal }; });
  } catch (error) { failure(error); restoreAfterClose(); return false; }
}
find('close-cancel').addEventListener('click', restoreAfterClose);
find('close-dialog').addEventListener('cancel', event => { event.preventDefault(); restoreAfterClose(); });
async function confirmClose(saveFile) {
  const choice = closeChoice; if (!choice || busy) return;
  busy = true; operationController = new AbortController();
  const abort = () => operationController?.abort(); choice.signal.addEventListener('abort', abort, { once: true });
  for (const id of ['close-save','close-draft']) find(id).disabled = true;
  update();
  try {
    if (saveFile && !await save(false, operationController.signal)) return;
    await persistDraft(); await settingWork;
    if (choice.signal.aborted || closeChoice !== choice) return;
    closeChoice = null; find('close-dialog').close(); choice.resolve(true);
  } catch (error) {
    find('close-error').textContent = `保存未完成：${error.message}`; find('close-error').hidden = false;
  } finally {
    choice.signal.removeEventListener('abort', abort); busy = false; operationController = null;
    for (const id of ['close-save','close-draft']) find(id).disabled = false;
    update();
  }
}
find('close-save').addEventListener('click', () => confirmClose(true));
find('close-draft').addEventListener('click', () => confirmClose(false));
async function start() {
  try {
    await client.runtime.getInfo();
    const [preferences, draft] = await Promise.all([client.storage.get('preferences'), client.files.readPrivateText('draft.json', { maxBytes: 64 * 1024 * 1024 })]);
    if (preferences.found && [16,19,23].includes(preferences.value?.fontSize)) {
      find('font-size').value = String(preferences.value.fontSize); editor.style.fontSize = `${preferences.value.fontSize}px`;
    }
    if (draft !== null) {
      const value = JSON.parse(draft);
      if (typeof value.text !== 'string' || typeof value.name !== 'string' || bytes(value.text) > maxBytes) throw Error('私有草稿格式异常，已保留原数据。');
      editor.value = value.text; previousText = value.text; name = value.name;
      if (['utf-8','utf-8-bom','utf-16le','utf-16be','gb18030'].includes(value.encoding)) { currentEncoding = value.encoding; find('encoding').value = value.encoding; }
    }
    await client.window.onCloseRequested(requestClose);
    ready = true; persisted = revision;
    find('draft-state').textContent = draft === null ? '写下内容后自动保存草稿' : '已恢复私有草稿';
    document.documentElement.dataset.runtime = 'connected'; update();
  } catch (error) { failure(error); find('draft-state').textContent = '草稿读取失败，请重新打开应用后重试'; }
}
start();

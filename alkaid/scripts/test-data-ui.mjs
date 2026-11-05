/** 原生文件选择器、受控 SDK、真实便签与重启数据恢复的集成验证。 */
import assert from 'node:assert/strict';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { resolve, join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { setTimeout as delay } from 'node:timers/promises';
import { createHash } from 'node:crypto';
import { connect, waitFor } from './cef-session.mjs';
import { pack } from './ypkg.mjs';
const state = JSON.parse(await readFile('build/data-ui-verification/current.json','utf8'));
const output = join(state.root,'evidence'); await mkdir(output,{recursive:true});
const APP = 'com.example.notes', SHADOW = 'com.example.notes-shadow';
const restart = process.argv.includes('--restart-check');
const browserCleanup = process.argv.includes('--browser-cleanup-check');
const browserBlocked = process.argv.includes('--browser-blocked-check');
const crashSeed = process.argv.includes('--crash-seed');
const crashCheck = process.argv.includes('--crash-check');
const parallelSeed = process.argv.includes('--parallel-seed');
const parallelCheck = process.argv.includes('--parallel-check');
const fixtures = { notes: join(state.root, 'notes.ypkg'), shadow: join(state.root, 'shadow.ypkg') };
if (!crashSeed && !parallelSeed && process.env.ALKAID_TEST_NOTES_PACKAGE) fixtures.notes = resolve(process.env.ALKAID_TEST_NOTES_PACKAGE);
if (!restart && !browserCleanup && !browserBlocked && !crashCheck && !parallelCheck) {
  const manifest = 'samples/notes/manifest.json';
  const assets = 'build/host/web/samples/notes';
  const key = 'build/dev-keys/example.private.pem';
  const shadowManifest = join(state.root, 'shadow.json');
  const base = JSON.parse(await readFile(manifest, 'utf8'));
  await writeFile(shadowManifest, JSON.stringify({ ...base, appId: SHADOW }));
  if (crashSeed || parallelSeed || !process.env.ALKAID_TEST_NOTES_PACKAGE) await pack(manifest, assets, key, fixtures.notes);
  if (!crashSeed) await pack(shadowManifest, assets, key, fixtures.shadow);
}
const origin = id => `alkaid://a-${createHash('sha256').update(id).digest('hex').slice(0,32)}/`;
const desktop = await connect('alkaid://desktop/'), connections = [desktop], results = [];
let app;
async function run(name,test) { await test(); results.push({name,passed:true}); console.log(`通过：${name}`); }
function select(script,args) {
  const result = spawnSync(process.env.ALKAID_TEST_POWERSHELL ?? 'pwsh',['-NoProfile','-File',resolve('scripts',script),'-HostProcessId',String(state.pid),...args],{encoding:'utf8',timeout:20000,windowsHide:true});
  assert.equal(result.status,0,result.stdout+'\n'+result.stderr);
}
async function task(id) {
  return waitFor(async()=> { const value = await desktop.sdk(`c.apps.getTask('${id}')`); if (value.status === 'failed') throw Error(JSON.stringify(value)); return value.status === 'succeeded'; });
}
async function install(path, expectedPermissions) {
  const result = await desktop.sdk('c.apps.prepareInstall()'); assert.ok(result.taskId,JSON.stringify(result));
  select('select-test-package.ps1',['-PackagePath',path]);
  await waitFor(()=>desktop.evaluate(`document.getElementById('install-dialog').open`));
  const preview = (await desktop.sdk(`c.apps.getTask('${result.taskId}')`)).preview;
  assert.deepEqual(preview.permissions.sort(),expectedPermissions.sort());
  const description = await desktop.evaluate(`document.getElementById('package-permissions').textContent`);
  if (expectedPermissions.length) assert.ok(description.includes('新增授权'));
  if (!preview.publisherTrusted) await desktop.evaluate(`document.getElementById('trust-publisher').click()`);
  await desktop.evaluate(`document.getElementById('confirm-install').click()`);
  await task(result.taskId);
  await waitFor(()=>desktop.evaluate(`!document.getElementById('install-dialog').open`));
}
async function open(id=APP) {
  const value = await desktop.sdk(`c.apps.launch('${id}')`); assert.ok(!value.error,JSON.stringify(value));
  const session = await connect(origin(id)); session.appId=id; connections.push(session); return session;
}
async function close(id=APP) {
  await desktop.sdk(`c.apps.close('${id}')`);
  await waitFor(async()=> {
    const target=connections.findLast(x=>x.appId===id);
    if(target?.socket.readyState===WebSocket.OPEN) await target.evaluate(`(()=>{if(document.getElementById('close-dialog')?.open)document.getElementById('close-draft').click();})()`).catch(()=>{});
    return (await desktop.sdk('c.apps.list()')).find(x=>x.appId===id)?.state === 'stopped';
  });
}
async function uninstallFromDesktop(clearData) {
  await waitFor(() => desktop.evaluate(`(() => {
    document.querySelector('[data-open="apps"]').click();
    const button = document.querySelector('[data-package-action="uninstall"][data-package-id="${APP}"]');
    if (button && !button.disabled) button.click();
    return document.getElementById('manage-dialog').open;
  })()`));
  assert.equal(await desktop.evaluate("document.getElementById('uninstall-clear-data').checked"), false);
  if (clearData) {
    await desktop.evaluate("document.getElementById('uninstall-clear-data').click()");
    assert.equal(await desktop.evaluate("document.getElementById('confirm-manage').textContent"), '删除数据并卸载');
  }
  await desktop.evaluate("document.getElementById('confirm-manage').click()");
  await waitFor(async () => !(await desktop.sdk('c.apps.list()')).some(item => item.appId === APP), '应用未卸载');
  await waitFor(() => desktop.evaluate("!document.getElementById('manage-dialog').open"));
}
async function draftSaved() { await waitFor(()=>app.evaluate(`document.getElementById('draft-state').textContent === '私有草稿已保存'`)); }
async function edit(text) {
  await app.evaluate(`(()=>{const e=document.getElementById('editor');e.value=${JSON.stringify(text)};e.dispatchEvent(new Event('input',{bubbles:true}));})()`);
  await draftSaved();
}
async function chooser(mode,file,cancel=false,overwrite=false) {
  await app.evaluate(`(async()=>{const {AlkaidClient}=await import('/sdk/index.js');const {createCefTransport}=await import('/src/bridge.js');globalThis.__dialogClient ??= new AlkaidClient(createCefTransport());globalThis.__dialogDone=false;globalThis.__dialogResult=await __dialogClient.dialog.${mode}();globalThis.__dialogDone=true;})();undefined`);
  select('select-test-text.ps1',['-Title',`笺 · 私有便签 · ${mode==='openFile'?'打开':'保存'}文件`,...(cancel?['-Cancel']:['-FilePath',file]),...(overwrite?['-ConfirmOverwrite']:[])]);
  await waitFor(()=>app.evaluate('globalThis.__dialogDone'));
  return app.evaluate('globalThis.__dialogResult');
}
async function shot(name) { const value=await app.command('Page.captureScreenshot',{format:'png'}); await writeFile(join(output,name+'.png'),Buffer.from(value.data,'base64')); }
const permissions = ['storage.kv','files.private','files.userSelected.read','files.userSelected.write'];
try {
  if (parallelSeed || parallelCheck) {
    if (parallelSeed) {
      await waitFor(async () => Array.isArray(await desktop.sdk('c.apps.listAudit()')));
      await install(fixtures.notes, [...permissions]);
      await install(fixtures.shadow, [...permissions]);
    }
    const primary = await open();
    const shadow = await open(SHADOW);
    const apps = [[primary, '主应用'], [shadow, '并行应用']];
    if (parallelSeed) {
      await run('应用在桌面内切换、最小化与 Dock 恢复', async () => {
        const title = () => desktop.evaluate("!document.getElementById('app-workspace').hidden");
        await waitFor(title, '桌面内应用窗口未显示');
        assert.equal((await desktop.sdk('c.apps.list()')).find(item => item.appId === SHADOW).workspaceActive, true);
        const result = await desktop.sdk(`c.apps.layoutWorkspace({appId:'${SHADOW}',x:40,y:100,width:700,height:480,visible:true,focus:true,persist:false,minimize:false})`);
        assert.equal(result, null, JSON.stringify(result));
        await desktop.evaluate("document.getElementById('workspace-minimize').click()");
        await waitFor(async () => !(await desktop.sdk('c.apps.list()')).some(item => item.workspaceActive), '最小化后宿主仍标记应用为前台');
        assert.equal((await desktop.sdk('c.apps.list()')).find(item => item.appId === SHADOW).state, 'running');
        await desktop.evaluate(`document.querySelector('.app-dock[data-app-id="${APP}"]').click()`);
        await waitFor(async () => (await desktop.sdk('c.apps.list()')).find(item => item.appId === APP)?.workspaceActive);
        await waitFor(title, 'Dock 未恢复桌面内窗口');
        const rect = selector => desktop.evaluate(`document.querySelector('${selector}').getBoundingClientRect().toJSON()`);
        const drag = async (selector, dx, dy) => {
          const box = await rect(selector);
          const x = Math.round(box.x + box.width / 2), y = Math.round(box.y + box.height / 2);
          assert.equal(await desktop.evaluate(`!!document.elementFromPoint(${x},${y})?.closest('${selector}')`), true,
            `拖动点未命中 ${selector}: ${JSON.stringify(box)}`);
          await desktop.command('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y });
          await desktop.command('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', buttons: 1, clickCount: 1 });
          await desktop.command('Input.dispatchMouseEvent', { type: 'mouseMoved', x: x + dx, y: y + dy, button: 'left', buttons: 1 });
          await delay(100);
          await desktop.command('Input.dispatchMouseEvent', { type: 'mouseReleased', x: x + dx, y: y + dy, button: 'left', clickCount: 1 });
        };
        const initial = await rect('#app-workspace');
        await drag('#workspace-resize', -100, -75);
        await waitFor(async () => (await rect('#app-workspace')).width < initial.width, '桌面内窗口缩放未生效');
        const smaller = await rect('#app-workspace');
        await drag('#workspace-handle', 36, 28);
        assert.ok((await rect('#app-workspace')).x > smaller.x);
        await desktop.evaluate("document.getElementById('workspace-maximize').click()");
        assert.ok((await rect('#app-workspace')).width > smaller.width);
        await desktop.evaluate("document.getElementById('workspace-maximize').click()");
        const content = await rect('#workspace-content');
        await waitFor(async () => Math.abs(await primary.evaluate('innerWidth') - content.width) <= 2, 'CEF 应用视图未跟随桌面布局');
        const forged = { appId: APP, x: 0, y: 0, width: 100, height: 100, visible: true, focus: false, persist: false, minimize: false };
        assert.equal((await primary.sdk(`c.apps.layoutWorkspace(${JSON.stringify(forged)})`)).error, 'PERMISSION_DENIED');
      });
      await run('两个应用并行写入同名键、配置、私有文件及网页存储', async () => {
        await Promise.all(apps.map(async ([session, name]) => {
          const value = JSON.stringify(name);
          assert.equal(await session.sdk(`c.storage.set('parallel-test', ${value})`), null);
          assert.equal(await session.sdk(`c.config.app.set('parallel-test', ${value})`), null);
          assert.equal(await session.sdk(`c.files.writePrivateText('parallel-record.txt', ${value})`), null);
          await session.evaluate(`localStorage.setItem('parallel-test', ${value})`);
        }));
      });
      await close(APP);
      assert.equal((await desktop.sdk('c.apps.list()')).find(item => item.appId === SHADOW).state, 'running');
      apps[0][0] = await open();
    }
    await run('两个应用读取各自的同名数据', async () => {
      await Promise.all(apps.map(async ([session, name]) => {
        assert.equal((await session.sdk("c.storage.get('parallel-test')")).value, name);
        assert.equal((await session.sdk("c.config.app.get('parallel-test')")).value, name);
        assert.equal(await session.sdk("c.files.readPrivateText('parallel-record.txt')"), name);
        assert.equal(await session.evaluate("localStorage.getItem('parallel-test')"), name);
      }));
    });
  } else if (crashSeed || crashCheck) {
    const reduced = permissions.filter(permission => permission !== 'files.userSelected.write').sort();
    if (crashSeed) {
      await run('空目录首次启动并安装当前 YPKG v3', async () => {
        await waitFor(async () => Array.isArray(await desktop.sdk('c.apps.listAudit()')));
        assert.deepEqual((await desktop.sdk('c.apps.list()')).filter(item => item.kind === 'installed'), []);
        await install(fixtures.notes, [...permissions]);
        const update = await desktop.sdk(`c.apps.setPermissions('${APP}', ${JSON.stringify(reduced)})`);
        assert.ok(update.taskId, JSON.stringify(update));
        await task(update.taskId);
      });
      await run('应用运行中提交草稿、应用配置和平台配置', async () => {
        app = await open();
        await edit('强制退出后仍应读到这份草稿');
        assert.equal(await app.sdk("c.config.app.set('recovery-test', 42)"), null);
        assert.equal(await desktop.sdk("c.config.platform.set('recovery-test', '已提交')"), null);
      });
    } else {
      await run('强退后恢复安装、授权、草稿、配置和操作记录', async () => {
        const evidence = JSON.parse((await readFile(join(output, 'forced-exit.json'), 'utf8')).replace(/^\uFEFF/, ''));
        assert.equal(evidence.forced, true);
        assert.notEqual(evidence.exitCode, 0);
        const installed = await waitFor(async () => (await desktop.sdk('c.apps.list()')).find(item => item.appId === APP));
        assert.equal(installed.state, 'stopped');
        assert.deepEqual(installed.grantedPermissions.sort(), reduced);
        assert.equal((await desktop.sdk("c.config.platform.get('recovery-test')")).value, '已提交');
        app = await open();
        assert.equal(await app.evaluate("document.getElementById('editor').value"), '强制退出后仍应读到这份草稿');
        assert.equal((await app.sdk("c.config.app.get('recovery-test')")).value, 42);
        const audit = await desktop.sdk('c.apps.listAudit()');
        for (const operation of ['install', 'permissions']) {
          assert.ok(audit.some(entry => entry.appId === APP && entry.operation === operation && entry.code === 'OK'));
        }
      });
    }
  } else if (browserBlocked) {
    await run('文件被占用时保留浏览器清理待办，并提示重启重试', async () => {
      const entry = await waitFor(async () => (await desktop.sdk('c.apps.list()')).find(item => item.appId === APP));
      assert.equal(entry.browserCleanupPending, true);
      assert.equal(entry.browserCleanupFailed, true);
      assert.equal((await desktop.sdk(`c.apps.launch('${APP}')`)).error, 'APP_BROWSER_CLEANUP_FAILED');
      await desktop.evaluate(`document.querySelector('[data-open="apps"]').click()`);
      await waitFor(() => desktop.evaluate(`
        document.querySelector('[data-app-detail="${APP}"]')?.click();
        document.getElementById('app-detail-dialog').open;
      `));
      assert.match(await desktop.evaluate("document.getElementById('browser-cleanup-pending').textContent"), /解除文件占用/);
      const shadow = await open(SHADOW);
      assert.equal(await shadow.evaluate("localStorage.getItem('cleanup-test')"), '保留');
    });
  } else if (browserCleanup) {
    await run('重启后清空目标应用网页存储，保留其他应用与宿主配置', async () => {
      const entry = (await desktop.sdk('c.apps.list()')).find(item => item.appId === APP);
      assert.equal(entry.browserCleanupPending, false);
      assert.equal(entry.browserCleanupFailed, false);
      app = await open();
      assert.equal(await app.evaluate("localStorage.getItem('cleanup-test')"), null);
      assert.equal(await app.evaluate("indexedDB.databases().then(items => items.some(item => item.name === 'cleanup-test'))"), false);
      assert.equal((await app.sdk("c.config.app.get('browser-cleanup-test')")).value, 42);
      const shadow = await open(SHADOW);
      assert.equal(await shadow.evaluate("localStorage.getItem('cleanup-test')"), '保留');
    });
    await run('管理审计重启后保留，应用页面无权读取', async () => {
      assert.equal((await app.sdk('c.apps.listAudit()')).error, 'PERMISSION_DENIED');
      const entries = await desktop.sdk('c.apps.listAudit()');
      for (const [operation, code] of [['install', 'OK'], ['uninstall', 'OK'], ['clearData', 'OK'],
        ['clearBrowserData', 'PENDING_RESTART']]) {
        assert.ok(entries.some(entry => entry.appId === APP && entry.operation === operation && entry.code === code));
      }
      assert.ok(entries.every(entry => Object.keys(entry).sort().join(',') === 'appId,code,operation,target,time'));
    });
  } else if (restart) {
    await run('宿主重启恢复私有草稿和字号设置，系统文件句柄不恢复',async()=> {
      app=await open();
      await waitFor(() => app.evaluate(`document.getElementById('editor').value === ${JSON.stringify('重启后继续书写\n草稿只属于这个应用。')}`));
      assert.equal(await app.evaluate(`document.getElementById('font-size').value`),'23');
      assert.equal(await app.evaluate(`document.getElementById('file-state').textContent`),'私有草稿');
      assert.equal((await app.sdk(`c.files.readText('${'a'.repeat(64)}')`)).error,'HANDLE_EXPIRED');
      await shot('restart');
    });
    await run('应用详情确认清理私有数据与配置，保留安装及用户文件', async () => {
      assert.equal(await app.sdk("c.config.app.set('cleanup-test', 42)"), null);
      assert.equal((await app.sdk(`c.apps.clearData('${APP}')`)).error, 'PERMISSION_DENIED');
      assert.equal((await desktop.sdk(`c.apps.clearData('${APP}')`)).error, 'APP_BUSY');
      const showDetails = () => waitFor(() => desktop.evaluate(`
        document.querySelector('[data-open="apps"]').click();
        document.querySelector('[data-app-detail="${APP}"]')?.click();
        document.getElementById('app-detail-dialog').open;
      `));
      const click = id => desktop.evaluate(`document.getElementById('${id}').click()`);
      await showDetails();
      await waitFor(() => desktop.evaluate("document.getElementById('clear-app-data').disabled"));
      await close();
      await waitFor(() => desktop.evaluate("!document.getElementById('clear-app-data').disabled"));
      await click('clear-app-data');
      assert.equal(await desktop.evaluate("document.getElementById('clear-data-dialog').open"), true);
      await click('cancel-clear-data');
      app = await open();
      assert.equal((await app.sdk("c.config.app.get('cleanup-test')")).value, 42);
      await close();
      await waitFor(() => desktop.evaluate("!document.getElementById('clear-app-data').disabled"));
      await click('clear-app-data');
      await click('confirm-clear-data');
      await waitFor(() => desktop.evaluate("document.getElementById('toast').textContent === '应用数据已清理'"));
      app = await open();
      assert.equal((await app.sdk("c.storage.get('preferences')")).found, false);
      assert.equal(await app.sdk("c.files.readPrivateText('draft.json')"), null);
      assert.equal((await app.sdk("c.config.app.get('cleanup-test')")).found, false);
      assert.equal(await readFile(join(state.root, 'saved.txt'), 'utf8'), '短文本');
      assert.deepEqual((await desktop.sdk('c.apps.list()')).find(item => item.appId === APP).grantedPermissions.sort(), [...permissions].sort());
      await close();
    });
    await run('卸载时明确选择清理，重装后私有数据与配置为空', async () => {
      app = await open();
      await edit('卸载时应删除的草稿');
      assert.equal(await app.sdk("c.storage.set('uninstall-test', 42)"), null);
      assert.equal(await app.sdk("c.config.app.set('uninstall-test', 42)"), null);
      await close();
      await uninstallFromDesktop(true);
      await install(fixtures.notes, permissions);
      app = await open();
      assert.equal((await app.sdk("c.storage.get('uninstall-test')")).found, false);
      assert.equal(await app.sdk("c.files.readPrivateText('draft.json')"), null);
      assert.equal((await app.sdk("c.config.app.get('uninstall-test')")).found, false);
      assert.equal(await readFile(join(state.root, 'saved.txt'), 'utf8'), '短文本');
    });
    await run('从应用详情登记浏览器清理，清理完成前禁止启动', async () => {
      await app.evaluate("localStorage.setItem('cleanup-test', '删除')");
      await app.evaluate(`new Promise((resolve, reject) => {
        const request = indexedDB.open('cleanup-test', 1);
        request.onerror = () => reject(request.error);
        request.onsuccess = () => { request.result.close(); resolve(); };
      })`);
      assert.equal(await app.sdk("c.config.app.set('browser-cleanup-test', 42)"), null);
      assert.equal((await app.sdk(`c.apps.clearBrowserData('${APP}')`)).error, 'PERMISSION_DENIED');
      assert.equal((await desktop.sdk(`c.apps.clearBrowserData('${APP}')`)).error, 'APP_BUSY');
      const shadow = await open(SHADOW);
      await shadow.evaluate("localStorage.setItem('cleanup-test', '保留')");
      await close(SHADOW);
      await close();
      await waitFor(() => desktop.evaluate(`
        document.querySelector('[data-open="apps"]').click();
        document.querySelector('[data-app-detail="${APP}"]')?.click();
        document.getElementById('app-detail-dialog').open && !document.getElementById('clear-browser-data').disabled;
      `));
      await desktop.evaluate("document.getElementById('clear-browser-data').click()");
      assert.equal(await desktop.evaluate("document.getElementById('clear-data-dialog').open"), true);
      await desktop.evaluate("document.getElementById('confirm-clear-data').click()");
      await waitFor(() => desktop.evaluate("!document.getElementById('browser-cleanup-pending').hidden"));
      assert.equal((await desktop.sdk(`c.apps.launch('${APP}')`)).error, 'APP_BROWSER_CLEANUP_PENDING');
    });
  } else {
    await run('安装确认展示四项系统能力，便签从签名包启动',async()=> {
      await install(fixtures.notes,permissions); app=await open();
      assert.equal((await app.sdk('c.runtime.getInfo()')).runtimeVersion,'2.0.0-dev.6');
      assert.ok((await app.sdk('c.runtime.getCapabilities()')).includes('files.writeText'));
      assert.equal((await app.sdk('c.apps.prepareInstall()')).error,'PERMISSION_DENIED');
    });
    await run('正文写入私有草稿，字号保存为宿主私有设置',async()=> {
      await edit('今天的想法\n把页面留给前端，把系统能力交给瑶光台。');
      await app.evaluate(`document.getElementById('font-size').value='23';document.getElementById('font-size').dispatchEvent(new Event('change'));`);
      await waitFor(async()=> (await app.sdk(`c.storage.get('preferences')`)).value?.fontSize===23);
      assert.ok(JSON.parse(await app.sdk(`c.files.readPrivateText('draft.json')`)).text.includes('瑶光台'));
      await shot('notes');
    });
    const source=join(state.root,'source.txt'), saved=join(state.root,'saved.txt');
    await writeFile(source,'本地文本\r\n保留中文与换行','utf8');
    await run('通过便签按钮打开真实 UTF-8 文件',async()=> {
      await app.evaluate(`document.getElementById('open').click();document.getElementById('replace-confirm').click();`);
      select('select-test-text.ps1',['-Title','笺 · 私有便签 · 打开文件','-FilePath',source]);
      await waitFor(()=>app.evaluate(`document.getElementById('editor').value.includes('保留中文与换行')`));
      await draftSaved();
    });
    await run('便签编辑、原生另存为和再次保存实际落盘',async()=> {
      await edit('修改后的中文\n第一次保存');
      await app.evaluate(`document.getElementById('save').click()`);
      select('select-test-text.ps1',['-Title','笺 · 私有便签 · 保存文件','-FilePath',saved]);
      await waitFor(()=>app.evaluate(`!document.getElementById('save').disabled`));
      assert.equal(await readFile(saved,'utf8'),'修改后的中文\n第一次保存');
      await edit('短文本'); await app.evaluate(`document.getElementById('save').click()`);
      await waitFor(()=>app.evaluate(`!document.getElementById('save').disabled`));
      assert.equal(await readFile(saved,'utf8'),'短文本');
      assert.equal(await readFile(source,'utf8'),'本地文本\r\n保留中文与换行');
    });
    let grant;
    await run('只读句柄拒绝写入，取消选择返回 null，页面不能传路径',async()=> {
      grant=await chooser('openFile',source); assert.equal(grant.writable,false);
      assert.equal((await app.sdk(`c.files.writeText('${grant.handle}','禁止覆盖')`)).error,'PERMISSION_DENIED');
      assert.equal(await chooser('openFile',null,true),null);
      const code=await app.evaluate(`new Promise(r=>__alkaidQuery({request:JSON.stringify({protocolVersion:1,requestId:'forged-file-path',method:'dialog.openFile',params:{path:'C:/Windows'},timeoutMs:1000}),onSuccess:s=>r(JSON.parse(s).error.code)}))`);
      assert.equal(code,'INVALID_REQUEST');
    });
    await run('显式覆盖确认后写入原文件，重新选择不会自动扩大其他句柄权限',async()=> {
      const writable=await chooser('saveFile',source,false,true);
      assert.equal(writable.writable,true);
      assert.equal(await app.sdk(`c.files.writeText('${writable.handle}','确认覆盖的测试内容')`),null);
      assert.equal(await readFile(source,'utf8'),'确认覆盖的测试内容');
      assert.equal((await app.sdk(`c.files.writeText('${grant.handle}','不能提升权限')`)).error,'PERMISSION_DENIED');
      await app.sdk(`c.files.close('${writable.handle}')`);
    });
    await run('请求取消和超时后的原生选择结果不会恢复授权',async()=> {
      await app.evaluate(`globalThis.__cancelledReply=false;globalThis.__cancelId=__alkaidQuery({request:JSON.stringify({protocolVersion:1,requestId:'cancel-dialog',method:'dialog.openFile',params:{},timeoutMs:15000}),onSuccess:()=>{globalThis.__cancelledReply=true;}});undefined`);
      await app.evaluate(`__alkaidCancel(globalThis.__cancelId);undefined`);
      select('select-test-text.ps1',['-Title','笺 · 私有便签 · 打开文件','-FilePath',source]);
      assert.equal(await app.evaluate('globalThis.__cancelledReply'),false);
      await app.evaluate(`globalThis.__timeoutReply=null;__alkaidQuery({request:JSON.stringify({protocolVersion:1,requestId:'timeout-dialog',method:'dialog.openFile',params:{},timeoutMs:100}),onSuccess:s=>{globalThis.__timeoutReply=JSON.parse(s);}});undefined`);
      await waitFor(()=>app.evaluate('globalThis.__timeoutReply'));
      select('select-test-text.ps1',['-Title','笺 · 私有便签 · 打开文件','-FilePath',source]);
      assert.equal((await app.evaluate('globalThis.__timeoutReply')).error.code,'TIMEOUT');
      assert.equal(await chooser('openFile',null,true),null);
    });
    await run('另一安装应用不能读取私有数据或使用外来文件句柄',async()=> {
      await install(fixtures.shadow,permissions); const shadow=await open(SHADOW);
      assert.deepEqual(await shadow.sdk(`c.storage.get('preferences')`),{found:false,value:null});
      assert.equal(await shadow.sdk(`c.files.readPrivateText('draft.json')`),null);
      assert.equal((await shadow.sdk(`c.files.readText('${grant.handle}')`)).error,'HANDLE_EXPIRED');
      await close(SHADOW);
      const inspector=await desktop.sdk(`c.apps.launch('org.alkaid.inspector')`); assert.ok(!inspector.error);
      const plain=await connect('alkaid://inspector/');connections.push(plain);
      assert.equal((await plain.sdk(`c.storage.get('preferences')`)).error,'PERMISSION_DENIED');
      assert.equal((await plain.sdk('c.dialog.openFile()')).error,'PERMISSION_DENIED');
      await desktop.sdk(`c.apps.close('org.alkaid.inspector')`);
    });
    await run('刷新页面撤销旧句柄并恢复草稿',async()=> {
      await app.command('Page.reload');
      await waitFor(()=>app.evaluate(`document.documentElement.dataset.runtime==='connected'`));
      assert.equal((await app.sdk(`c.files.readText('${grant.handle}')`)).error,'HANDLE_EXPIRED');
      await waitFor(() => app.evaluate("document.getElementById('editor').value === '短文本'"));
      await edit('重启后继续书写\n草稿只属于这个应用。');
    });
    await run('卸载重装保留私有数据，仍须确认系统权限',async()=> {
      await close();
      await uninstallFromDesktop(false);
      await install(fixtures.notes,permissions); app=await open();
      await waitFor(() => app.evaluate(`document.getElementById('editor').value === ${JSON.stringify('重启后继续书写\n草稿只属于这个应用。')}`));
      assert.equal(await app.evaluate(`document.getElementById('font-size').value`),'23');
    });
  }
  for (const session of connections) assert.deepEqual(session.errors,[]);
  const phase = parallelSeed ? 'parallel-seed' : parallelCheck ? 'parallel-check' :
    crashSeed ? 'crash-seed' : crashCheck ? 'crash-check' : browserBlocked ? 'browser-blocked' :
    browserCleanup ? 'browser-cleanup' : restart ? 'restart' : 'flow';
  await writeFile(join(output,phase+'-results.json'),JSON.stringify(results,null,2));
  console.log(`存储文件界面验证通过：${results.length} 组`);
} finally {
  if (!crashSeed) {
    await desktop.sdk('c.runtime.exit()').catch(()=>{});
    for(let i=0;i<140;i++){
      for(const session of connections) if(session.socket.readyState===WebSocket.OPEN) await session.evaluate(`(()=>{if(document.getElementById('close-dialog')?.open)document.getElementById('close-draft').click();})()`).catch(()=>{});
      if(connections.every(x=>x.socket.readyState===WebSocket.CLOSED))break;
      await delay(100);
    }
  }
  for (const session of connections) session.socket.close();
}

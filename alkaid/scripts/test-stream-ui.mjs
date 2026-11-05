/** 在真实 CEF 中验证分块文件、便签升级及统一关闭协商。 */
import assert from 'node:assert/strict';
import { readFile, writeFile, mkdir, chmod, stat } from 'node:fs/promises';
import { resolve, join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { setTimeout as delay } from 'node:timers/promises';
import { connect, waitFor } from './cef-session.mjs';
const fixtures=JSON.parse(await readFile('build/stream-ui-verification/fixtures.json','utf8'));
const state=JSON.parse(await readFile('build/stream-ui-verification/current.json','utf8'));
const output=join(fixtures.root,'evidence');await mkdir(output,{recursive:true});
const desktop=await connect('alkaid://desktop/'),connections=[desktop],results=[];
const APP='com.example.notes',SHADOW='com.example.notes-shadow';let app,shadow,inspector;
const lastInput='关闭协商验证\n最后一笔输入需要保留。\n原生关闭前的即时输入';
const origin=id=>`alkaid://a-${createHash('sha256').update(id).digest('hex').slice(0,32)}/`;
async function run(name,body){await body();results.push({name,passed:true});console.log(`通过：${name}`);}
function native(script,args){const result=spawnSync(process.env.ALKAID_TEST_POWERSHELL??'pwsh',['-NoProfile','-File',resolve('scripts',script),'-HostProcessId',String(state.pid),...args],{encoding:'utf8',timeout:20000,windowsHide:true});assert.equal(result.status,0,result.stdout+'\n'+result.stderr);}
const select=(path,save=false,overwrite=false)=>native('select-test-text.ps1',['-Title',`笺 · 私有便签 · ${save?'保存':'打开'}文件`,'-FilePath',path,...(overwrite?['-ConfirmOverwrite']:[])]);
async function appState(id=APP){return(await desktop.sdk('c.apps.list()')).find(x=>x.appId===id)?.state;}
async function task(id){return waitFor(async()=>{const result=await desktop.sdk(`c.apps.getTask('${id}')`);if(result.status==='failed')throw Error(JSON.stringify(result));return result.status==='succeeded';});}
async function install(path){const result=await desktop.sdk('c.apps.prepareInstall()');assert.ok(result.taskId,JSON.stringify(result));native('select-test-package.ps1',['-PackagePath',path]);await waitFor(()=>desktop.evaluate(`document.getElementById('install-dialog').open`));const record=await desktop.sdk(`c.apps.getTask('${result.taskId}')`);if(!record.preview.publisherTrusted)await desktop.evaluate(`document.getElementById('trust-publisher').click()`);await desktop.evaluate(`document.getElementById('confirm-install').click()`);await task(result.taskId);await waitFor(()=>desktop.evaluate(`!document.getElementById('install-dialog').open`));}
async function open(id=APP){const result=await desktop.sdk(`c.apps.launch('${id}')`);assert.ok(!result.error,JSON.stringify(result));const session=await connect(origin(id));connections.push(session);return session;}
async function saved(session=app){await waitFor(()=>session.evaluate(`document.getElementById('draft-state').textContent==='私有草稿已保存'`));}
async function edit(text,session=app){await session.evaluate(`(()=>{const e=document.getElementById('editor');e.value=${JSON.stringify(text)};e.dispatchEvent(new Event('input',{bubbles:true}));})()`);await saved(session);}
async function closeDialog(session=app){await waitFor(()=>session.evaluate(`document.getElementById('close-dialog').open`));}
async function idle(session=app){await waitFor(()=>session.evaluate(`!document.getElementById('save').disabled`));}
async function shot(name,session=app){const value=await session.command('Page.captureScreenshot',{format:'png'});await writeFile(join(output,name+'.png'),Buffer.from(value.data,'base64'));}
async function choose(mode,path){await app.evaluate(`(async()=>{const {AlkaidClient}=await import('/sdk/index.js');const {createCefTransport}=await import('/src/bridge.js');globalThis.__transferClient??=new AlkaidClient(createCefTransport());globalThis.__chosen=null;globalThis.__chosen=await __transferClient.dialog.${mode}();})();undefined`);select(path,mode==='saveFile');return waitFor(()=>app.evaluate('globalThis.__chosen'));}
async function finish(){await desktop.sdk('c.runtime.exit()').catch(()=>{});for(let i=0;i<140;i++){for(const session of connections){if(session.socket.readyState!==WebSocket.OPEN)continue;await session.evaluate(`(()=>{const d=document.getElementById('close-dialog');if(d?.open)document.getElementById('close-draft').click();})()`).catch(()=>{});}if(connections.every(x=>x.socket.readyState===WebSocket.CLOSED))return;await delay(100);}}
try{
if(process.argv.includes('--restart-check')){
  await run('完整重启后恢复最后草稿、编码与字号',async()=>{app=await open();assert.equal(await app.evaluate(`document.getElementById('editor').value`),lastInput);assert.equal(await app.evaluate(`document.getElementById('font-size').value`),'23');assert.equal(await app.evaluate(`document.getElementById('encoding').value`),'gb18030');await shot('restart');});
}else{
  await run('当前格式的便签安装后，更新保留草稿和设置',async()=>{
    await install(fixtures.initial);app=await open();
    await edit('升级后仍应保留');
    await app.evaluate(`document.getElementById('font-size').value='23';document.getElementById('font-size').dispatchEvent(new Event('change'));`);
    await saved();
    await desktop.sdk(`c.apps.close('${APP}')`);
    await closeDialog();
    await app.evaluate(`document.getElementById('close-draft').click()`);
    await waitFor(async()=>await appState()==='stopped');
    await install(process.env.ALKAID_TEST_NOTES_PACKAGE??fixtures.candidate);app=await open();
    assert.equal(await app.evaluate(`document.getElementById('font-size').value`),'23');assert.ok((await app.evaluate(`document.getElementById('editor').value`)).includes('升级后仍应保留'));
  });
  await run('便签读取超过 4 MiB 的真实文本并保存完整私有草稿',async()=>{
    await app.evaluate(`document.getElementById('open').click();document.getElementById('replace-confirm').click();`);select(join(fixtures.root,'large.txt'));
    await waitFor(()=>app.evaluate(`document.getElementById('editor').value.length===${fixtures.largeCharacters}`));await saved();
    assert.ok(fixtures.largeBytes>4*1024*1024);await shot('large-text');
  });
  const target=join(fixtures.root,'large-saved.txt');
  await run('大文本编辑、分块另存为及逐字节结果核对',async()=>{
    await app.evaluate(`document.getElementById('editor').value+=${JSON.stringify('\n保存校验结束。')};document.getElementById('editor').dispatchEvent(new Event('input'));`);await saved();
    await app.evaluate(`document.getElementById('save').click()`);select(target,true);await idle();
    assert.equal(await readFile(target,'utf8'),(await readFile(join(fixtures.root,'large.txt'),'utf8'))+'\n保存校验结束。');
  });
  await run('二进制按块读写保持所有字节，取消不替换原文件',async()=>{
    const source=await choose('openFile',join(fixtures.root,'binary.dat'));const destination=await choose('saveFile',join(fixtures.root,'binary-copy.dat'));
    const before = await app.command('Runtime.getHeapUsage');
    const started = performance.now();
    const result=await app.sdk(`c.files.writeChunks('${destination.handle}',c.files.readChunks('${source.handle}'))`, 120000);assert.equal(result,null);
    const elapsedMs = Math.round(performance.now() - started);
    const after = await app.command('Runtime.getHeapUsage');
    assert.deepEqual(await readFile(join(fixtures.root,'binary-copy.dat')),await readFile(join(fixtures.root,'binary.dat')));
    await writeFile(join(output, 'transfer-baseline.json'), JSON.stringify({
      bytes: (await stat(join(fixtures.root, 'binary.dat'))).size, elapsedMs,
      rendererHeapBeforeBytes: before.usedSize, rendererHeapAfterBytes: after.usedSize,
      userAgent: await app.evaluate('navigator.userAgent'),
    }, null, 2));
    const cancelled=await app.sdk(`(async()=>{const a=new AbortController();return c.files.writeText('${destination.handle}','取消'.repeat(100000),{signal:a.signal,onProgress:()=>a.abort()});})()`);assert.equal(cancelled.error,'ABORTED');
    assert.deepEqual(await readFile(join(fixtures.root,'binary-copy.dat')),await readFile(join(fixtures.root,'binary.dat')));
    await app.sdk(`c.files.close('${source.handle}')`);await app.sdk(`c.files.close('${destination.handle}')`);
  });
  await run('自动识别 UTF-16，GB18030 保存与独立解码一致',async()=>{
    const source=await choose('openFile',join(fixtures.root,'utf16.txt'));assert.equal(await app.sdk(`c.files.readText('${source.handle}')`),'UTF-16 中文与 🌿\r\n第二行');await app.sdk(`c.files.close('${source.handle}')`);
    await edit('关闭协商验证\n最后一笔输入需要保留。');await app.evaluate(`document.getElementById('encoding').value='gb18030';document.getElementById('encoding').dispatchEvent(new Event('change'));`);await saved();
    await app.evaluate(`document.getElementById('save-as').click()`);select(join(fixtures.root,'gb18030.txt'),true);await idle();
    assert.equal(new TextDecoder('gb18030',{fatal:true}).decode(await readFile(join(fixtures.root,'gb18030.txt'))),'关闭协商验证\n最后一笔输入需要保留。');
  });
  await run('桌面管理关闭可取消，窗口恢复编辑状态',async()=>{
    await edit('');await desktop.sdk(`c.apps.close('${APP}')`);await closeDialog();await app.evaluate(`document.getElementById('close-cancel').click()`);await waitFor(async()=>await appState()==='running');await idle();
    await edit('关闭协商验证\n最后一笔输入需要保留。\n尚未保存的修改');await desktop.sdk(`c.apps.close('${APP}')`);await closeDialog();await shot('close-confirm');
    await app.evaluate(`document.getElementById('close-cancel').click()`);await waitFor(async()=>await appState()==='running');await idle();
  });
  await run('文件保存失败时不关闭窗口、不覆盖原文件',async()=>{
    const path=join(fixtures.root,'gb18030.txt'),before=await readFile(path);await chmod(path,0o444);
    try{await desktop.sdk(`c.apps.close('${APP}')`);await closeDialog();await app.evaluate(`document.getElementById('close-save').click()`);await waitFor(()=>app.evaluate(`!document.getElementById('close-error').hidden`));assert.equal(await appState(),'closing');assert.deepEqual(await readFile(path),before);await shot('close-save-failed');await app.evaluate(`document.getElementById('close-cancel').click()`);await waitFor(async()=>await appState()==='running');}
    finally{await chmod(path,0o666);}
  });
  await run('关闭确认中的保存成功落盘后才关闭窗口',async()=>{
    const text='从关闭确认保存到文件。';await edit(text);await desktop.sdk(`c.apps.close('${APP}')`);await closeDialog();await app.evaluate(`document.getElementById('close-save').click()`);await waitFor(async()=>await appState()==='stopped');
    assert.equal(new TextDecoder('gb18030',{fatal:true}).decode(await readFile(join(fixtures.root,'gb18030.txt'))),text);app=await open();assert.equal(await app.evaluate(`document.getElementById('editor').value`),text);
  });
  await run('原生窗口关闭等待最后一笔草稿提交，重开内容完整',async()=>{
    await app.evaluate(`document.getElementById('editor').value=${JSON.stringify(lastInput)};document.getElementById('editor').dispatchEvent(new Event('input'));`);
    native('send-test-close.ps1',['-Title','笺 · 私有便签']);await closeDialog();await app.evaluate(`document.getElementById('close-draft').click()`);await waitFor(async()=>await appState()==='stopped');app=await open();
    assert.equal(await app.evaluate(`document.getElementById('editor').value`),lastInput);
  });
  await run('平台退出屏障：一个应用同意、另一个取消时全部保留',async()=>{
    await install(fixtures.shadow);shadow=await open(SHADOW);await edit('另一个应用的独立草稿',shadow);
    native('send-test-close.ps1',['-Title','瑶光台桌面']);await closeDialog(app);await closeDialog(shadow);
    await app.evaluate(`document.getElementById('close-draft').click()`);assert.equal(await appState(),'closing');
    await shadow.evaluate(`document.getElementById('close-cancel').click()`);await waitFor(async()=>await appState()==='running'&&await appState(SHADOW)==='running');await idle(app);await idle(shadow);
    assert.equal(await app.evaluate(`document.body.dataset.closing`),'false');
    await desktop.sdk(`c.apps.close('${SHADOW}')`);await closeDialog(shadow);await shadow.evaluate(`document.getElementById('close-draft').click()`);await waitFor(async()=>await appState(SHADOW)==='stopped');
  });
  await run('关闭处理器不响应时超时取消退出，已同意的应用恢复',async()=>{
    await desktop.sdk(`c.apps.launch('org.alkaid.inspector')`);inspector=await connect('alkaid://inspector/');connections.push(inspector);
    await inspector.evaluate(`(async()=>{const {AlkaidClient}=await import('/sdk/index.js');const {createCefTransport}=await import('/src/bridge.js');globalThis.__guard=new AlkaidClient(createCefTransport());globalThis.__unguard=await __guard.window.onCloseRequested(()=>new Promise(()=>{}));})()`);
    await desktop.sdk('c.runtime.exit()');await closeDialog();await app.evaluate(`document.getElementById('close-draft').click()`);
    for(let i=0;i<350;i++){if(await appState()==='running')break;await delay(100);}
    assert.equal(await appState(),'running');await idle();assert.equal(await appState('org.alkaid.inspector'),'running');await inspector.evaluate('globalThis.__unguard()');
  });
  await shot('ready');
}
for(const session of connections)assert.deepEqual(session.errors,[]);
await writeFile(join(output,process.argv.includes('--restart-check')?'restart-results.json':'flow-results.json'),JSON.stringify(results,null,2));
console.log(`分块与关闭协商验证通过：${results.length} 组`);
}finally{
  if(inspector?.socket.readyState===WebSocket.OPEN)await inspector.evaluate('globalThis.__unguard?.()').catch(()=>{});
  await finish();for(const session of connections)session.socket.close();
}

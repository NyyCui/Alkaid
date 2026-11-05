/** 原生选包 → 桌面确认 → 后台安装 → 独立应用 → 更新/恢复/卸载的真实端到端验证。 */
import assert from 'node:assert/strict';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { connect, waitFor } from './cef-session.mjs';

const fixtures = JSON.parse(await readFile(process.env.ALKAID_TEST_PACKAGE_FIXTURES ?? 'build/package-verification/latest.json','utf8'));
const state = JSON.parse(await readFile('build/package-ui-verification/current.json','utf8'));
const output = resolve('build/package-ui-verification',process.argv.includes('--restart-check') ? 'restart' : 'flow');
await mkdir(output,{recursive:true});
const APP = 'com.example.inspector';
const origin = `alkaid://a-${createHash('sha256').update(APP).digest('hex').slice(0,32)}/`;
const desktop = await connect('alkaid://desktop/');
const connections = [desktop], results = [];
let app;
async function run(name,test) { await test(); results.push({name,passed:true}); console.log(`通过：${name}`); }
async function list() { const value = await desktop.sdk('c.apps.list()'); assert.ok(Array.isArray(value)); return value; }
async function appState() { return (await list()).find(item=>item.appId===APP); }
async function task(id,status) {
  return waitFor(async()=> {
    const value = await desktop.sdk(`c.apps.getTask('${id}')`);
    if (value.status === 'failed') throw Error(JSON.stringify(value));
    return value.status === status ? value : false;
  },`任务未达到 ${status}`);
}
async function pick(file) {
  await waitFor(()=>desktop.evaluate(`!document.getElementById('install-package').disabled`));
  await desktop.evaluate(`document.getElementById('install-package').click()`);
  const choosing = await waitFor(async()=> (await desktop.sdk('c.apps.listTasks()')).find(item=>item.phase==='choosing'));
  const result = spawnSync(process.env.ALKAID_TEST_POWERSHELL ?? 'pwsh', ['-NoProfile','-File',resolve('scripts/select-test-package.ps1'),'-HostProcessId',String(state.pid),'-PackagePath',file], {encoding:'utf8',timeout:20000,windowsHide:true});
  assert.equal(result.status,0,result.stdout+'\n'+result.stderr);
  await task(choosing.taskId,'awaitingConsent');
  await waitFor(()=>desktop.evaluate(`document.getElementById('install-dialog').open`));
  return choosing.taskId;
}
async function confirm(id,trust=false) {
  if (trust) await desktop.evaluate(`document.getElementById('trust-publisher').click()`);
  await desktop.evaluate(`document.getElementById('confirm-install').click()`);
  await task(id,'succeeded');
  await waitFor(()=>desktop.evaluate(`!document.getElementById('install-dialog').open`));
}
async function open() {
  const launch = await desktop.sdk(`c.apps.launch('${APP}')`);
  assert.ok(!launch.error, JSON.stringify(launch));
  app = await connect(origin); connections.push(app);
  return app;
}
async function close() {
  const result = await desktop.sdk(`c.apps.close('${APP}')`); assert.ok(!result.error);
  await waitFor(async()=> (await appState())?.state === 'stopped');
}
async function shot(name,session) {
  const screenshot = await session.command('Page.captureScreenshot',{format:'png'});
  await writeFile(resolve(output,name+'.png'),Buffer.from(screenshot.data,'base64'));
}
try {
  if (process.argv.includes('--restart-check')) {
    await run('宿主重启后从持久化注册表恢复安装应用和任务记录',async()=> {
      await waitFor(async()=> (await appState())?.version === '1.0.0');
      assert.equal((await appState()).state,'stopped');
      assert.ok((await desktop.sdk('c.apps.listTasks()')).some(item=>item.status==='succeeded'));
      await open();
      assert.equal(await app.evaluate(`localStorage.getItem('package-retention-probe')`),'retained');
      await app.evaluate(`localStorage.removeItem('package-retention-probe')`);
    });
  } else {
    const existing = (await desktop.sdk('c.apps.listTasks()')).find(item=>item.status==='awaitingConsent');
    if (existing) { await waitFor(()=>desktop.evaluate(`document.getElementById('install-dialog').open`)); await desktop.evaluate(`document.getElementById('cancel-install').click()`); await task(existing.taskId,'cancelled'); }
    await run('页面不能通过参数传入任意本机路径',async()=> {
      const code = await desktop.evaluate(`new Promise(r=>__alkaidQuery({request:JSON.stringify({protocolVersion:1,requestId:'forged-path',method:'apps.prepareInstall',params:{path:'C:/Windows'},timeoutMs:1000}),onSuccess:s=>r(JSON.parse(s).error.code)}))`);
      assert.equal(code,'INVALID_REQUEST');
    });
    await run('原生选包、未知发布者确认与取消不激活应用',async()=> {
      const id = await pick(fixtures.v1);
      assert.equal(await desktop.evaluate(`document.getElementById('confirm-install').disabled`),true);
      assert.equal((await desktop.sdk(`c.apps.confirmInstall('${id}')`)).error,'PUBLISHER_UNTRUSTED');
      await shot('install-preview',desktop);
      await desktop.evaluate(`document.getElementById('cancel-install').click()`);
      await task(id,'cancelled');
      assert.equal(await appState(),undefined);
    });
    await run('确认发布者后真实安装，重复确认不重复提交',async()=> {
      const id = await pick(fixtures.v1);
      await confirm(id,true);
      assert.equal((await appState()).kind,'installed');
      assert.equal((await appState()).version,'1.0.0');
      assert.equal((await desktop.sdk(`c.apps.confirmInstall('${id}',true)`)).taskId,id);
      assert.ok((await readFile(fixtures.v1)).length);
    });
    await run('安装的前端应用独立运行且不能安装、卸载或信任其他应用',async()=> {
      await open();
      assert.equal((await app.sdk('c.runtime.getInfo()')).appId,APP);
      for (const expression of ['c.apps.prepareInstall()','c.apps.listTasks()',`c.apps.uninstall('${APP}')`,`c.apps.rollback('${APP}')`,`c.apps.confirmInstall('${'a'.repeat(64)}',true)`]) {
        assert.equal((await app.sdk(expression)).error,'PERMISSION_DENIED');
      }
      assert.equal(await app.evaluate(`fetch('/version.json').then(r=>r.status)`),404);
      await app.evaluate(`localStorage.setItem('package-retention-probe','retained')`);
    });
    await run('运行中更新被拒绝，关闭后才激活新版本',async()=> {
      const id = await pick(fixtures.v2);
      assert.equal((await desktop.sdk(`c.apps.getTask('${id}')`)).preview.publisherTrusted,true);
      assert.equal((await desktop.sdk(`c.apps.confirmInstall('${id}')`)).error,'APP_BUSY');
      assert.equal((await appState()).version,'1.0.0');
      await close();
      await confirm(id);
      assert.equal((await appState()).version,'1.1.0');
      await open();
      assert.equal(await app.evaluate(`fetch('/version.json').then(r=>r.json()).then(x=>x.version)`),'1.1.0');
      assert.equal(await app.evaluate(`localStorage.getItem('package-retention-probe')`),'retained');
    });
    await run('恢复上一版本切换资源，应用数据保持不变',async()=> {
      await close();
      const result = await desktop.sdk(`c.apps.rollback('${APP}')`);
      await task(result.taskId,'succeeded');
      assert.equal((await appState()).version,'1.0.0');
      await open();
      assert.equal(await app.evaluate(`fetch('/version.json').then(r=>r.status)`),404);
      assert.equal(await app.evaluate(`localStorage.getItem('package-retention-probe')`),'retained');
    });
    await run('桌面卸载移除程序与图标，同发布者重装保留数据',async()=> {
      assert.equal((await desktop.sdk(`c.apps.uninstall('${APP}')`)).error,'APP_BUSY');
      await close();
      await waitFor(()=>desktop.evaluate(`!!document.querySelector('[data-package-action=uninstall]:not(:disabled)')`));
      await desktop.evaluate(`document.querySelector('[data-package-action=uninstall]').click();document.getElementById('confirm-manage').click()`);
      await waitFor(async()=> !(await appState()));
      const removal = (await desktop.sdk('c.apps.listTasks()')).find(item=>item.kind==='uninstall');
      assert.equal((await desktop.sdk(`c.apps.confirmInstall('${removal.taskId}',true)`)).error,'INVALID_REQUEST');
      const id = await pick(fixtures.v1); await confirm(id);
      await open();
      assert.equal(await app.evaluate(`localStorage.getItem('package-retention-probe')`),'retained');
    });
    await shot('installed-app',app);
    await waitFor(()=>desktop.evaluate(`document.querySelectorAll('#apps-list .app-row').length === 2`));
    await shot('desktop',desktop);
    await run('桌面及安装应用没有未捕获页面异常',async()=> {
      for (const connection of connections) assert.deepEqual(connection.errors,[]);
    });
  }
  if (!process.argv.includes('--keep-running')) {
    await desktop.sdk('c.runtime.exit()');
    await waitFor(()=>connections.every(connection=>connection.socket.readyState===WebSocket.CLOSED));
  }
} catch (error) {
  results.push({name:'验证中断',passed:false,error:String(error)});
  throw error;
} finally {
  await writeFile(resolve(output,'report.json'),JSON.stringify({results},null,2));
  for (const connection of connections) connection.socket.close();
}

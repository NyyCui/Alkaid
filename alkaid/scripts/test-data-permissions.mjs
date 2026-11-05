/** 验证新增权限的安装确认、收缩后回退和旧注册表增量迁移。 */
import assert from 'node:assert/strict';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { resolve, join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { DatabaseSync } from 'node:sqlite';
import { pack, sha256 } from './ypkg.mjs';
const root = resolve('build/data-permission-verification',`run-${Date.now()}`);
await mkdir(root,{recursive:true});
const tool = resolve('build/host/infra/package/Release/alkaid_package_tool.exe');
function invoke(args, expected=0) {
  const result = spawnSync(tool,args,{encoding:'utf8',timeout:30000,windowsHide:true});
  assert.equal(result.status,expected,result.stdout+'\n'+result.stderr);
  return JSON.parse(result.stdout);
}
const base = JSON.parse(await readFile('samples/notes/manifest.json','utf8'));
async function make(name, version, permissions, appId=base.appId) {
  const manifest = join(root,name+'.json'), output = join(root,name+'.ypkg');
  await writeFile(manifest,JSON.stringify({...base,appId,version,permissions}));
  await pack(manifest,'build/host/web/samples/notes','build/dev-keys/example.private.pem',output);
  return output;
}
const first = await make('first','1.0.0',['storage.kv']);
const second = await make('second','1.1.0',['storage.kv','files.private']);
const third = await make('third','1.2.0',['files.private']);
const registry = join(root,'registry');
assert.deepEqual(invoke(['inspect',registry,first]).addedPermissions,['storage.kv']);
assert.deepEqual(invoke(['install',registry,first,'trust']).apps[0].grants,['storage.kv']);
assert.deepEqual(invoke(['inspect',registry,second]).addedPermissions,['files.private']);
assert.deepEqual(invoke(['install',registry,second]).apps[0].grants,['files.private','storage.kv']);
assert.deepEqual(invoke(['inspect',registry,third]).addedPermissions,[]);
assert.deepEqual(invoke(['install',registry,third]).apps[0].grants,['files.private']);
assert.equal(invoke(['rollback',registry,base.appId],1).error.code,'PERMISSION_DENIED');
console.log('通过：新增权限预览、持久授权及回退不能恢复已收缩权限');
await writeFile(join(registry,'versions',sha256(await readFile(third)),'package.ypkg'),'broken');
const recovered = invoke(['list',registry]).apps[0];
assert.equal(recovered.version,'1.1.0'); assert.deepEqual(recovered.grants,['files.private']);
console.log('通过：损坏恢复保留当前授权，不扩大旧版本权限');
const legacy = join(root,'legacy');
const empty = await make('legacy-empty','1.0.0',[],'com.example.legacy');
invoke(['install',legacy,empty,'trust']);
const database = new DatabaseSync(join(legacy,'registry.sqlite'));
database.exec('ALTER TABLE apps DROP COLUMN grants;PRAGMA user_version=1;'); database.close();
const migrated = invoke(['list',legacy]);
assert.equal(migrated.apps[0].appId,'com.example.legacy');
assert.deepEqual(migrated.apps[0].grants,[]); assert.ok(migrated.history.some(x=>x.status==='succeeded'));
const check = new DatabaseSync(join(legacy,'registry.sqlite'));
assert.equal(check.prepare('PRAGMA user_version').get().user_version,2); check.close();
console.log('通过：v1 注册表增量迁移，保留已有应用和任务');
const notes = await make('notes','1.0.0',base.permissions);
const shadow = await make('shadow','1.0.0',base.permissions,'com.example.notes-shadow');
const fixtures = {root,notes,shadow,appId:base.appId};
await writeFile(resolve('build/data-permission-verification/latest.json'),JSON.stringify(fixtures,null,2));
console.log(`权限验证通过：3 组，${root}`);

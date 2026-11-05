/** 使用真实 C++ 验签与 SQLite 注册表验证恶意包、更新约束和进程中断恢复。 */
import assert from 'node:assert/strict';
import { readFile, writeFile, mkdir, readdir, unlink } from 'node:fs/promises';
import { spawnSync } from 'node:child_process';
import { resolve, join } from 'node:path';
import { generateKeyPairSync } from 'node:crypto';
import { pack, signedEntries, zip, publicInfo, sha256 } from './ypkg.mjs';

const root = resolve('build/package-verification', `run-${Date.now()}`);
await mkdir(root, { recursive: true });
const tool = resolve(process.argv[2] ?? 'build/host/infra/package/Release/alkaid_package_tool.exe');
const key = () => generateKeyPairSync('ec', { namedCurve: 'prime256v1' }).privateKey.export({ type: 'pkcs8', format: 'pem' });
const pem = key(), otherPem = key();
const manifest = JSON.parse(await readFile('samples/inspector/manifest.json', 'utf8'));
const assets = [];
async function walk(path, prefix = '') {
  for (const entry of await readdir(path, { withFileTypes: true })) {
    if (entry.isDirectory()) await walk(join(path, entry.name), prefix + entry.name + '/');
    else assets.push({ name: 'assets/' + prefix + entry.name, data: await readFile(join(path, entry.name)) });
  }
}
await walk('build/host/web/samples/inspector');
const results = [];
function invoke(args, expected = 0) {
  const result = spawnSync(tool, args, { encoding: 'utf8', timeout: 30000 });
  assert.equal(result.status, expected, `命令失败 ${args[0]}：${result.stdout}\n${result.stderr}`);
  return expected === 77 ? null : JSON.parse(result.stdout);
}
async function run(name, body) { await body(); results.push({ name, passed: true }); console.log(`通过：${name}`); }
async function make(name, spec = manifest, content = assets, signing = pem, options = {}) {
  const file = join(root, `${name}.ypkg`);
  await zip(file, signedEntries(spec, content, signing, options));
  return file;
}
const v1 = await make('inspector-1.0.0');
const v2 = await make('inspector-1.1.0', { ...manifest, version: '1.1.0', name: '独立应用示例 · 新版' },
  [...assets, { name: 'assets/version.json', data: Buffer.from('{"version":"1.1.0"}') }]);
const registry = join(root, 'registry');
try {
  await run('原生插件可打包验签，应用安装入口拒绝插件类型', async () => {
    const plugin = { manifestVersion: 3, type: 'plugin', pluginId: 'com.example.serial', name: '串口',
      version: '1.0.0', publisher: manifest.publisher, protocolMajor: 1, architecture: 'win-x64',
      entry: 'worker.exe', capabilities: ['open', 'close'], events: ['disconnected'] };
    const worker = { name: 'assets/worker.exe', data: await readFile(tool) };
    const inputs = join(root, 'plugin-input');
    await mkdir(inputs);
    await writeFile(join(inputs, 'worker.exe'), worker.data);
    const spec = join(root, 'plugin.json'), signer = join(root, 'plugin-test-key.pem'), output = join(root, 'serial.ypkg');
    await writeFile(spec, JSON.stringify(plugin));
    await writeFile(signer, pem, { flag: 'wx', mode: 0o600 });
    try { await pack(spec, inputs, signer, output); }
    finally { await unlink(signer); }
    const verified = invoke(['verify', output]);
    assert.equal(verified.type, 'plugin');
    assert.equal(verified.pluginId, plugin.pluginId);
    assert.deepEqual(verified.capabilities, plugin.capabilities);
    const data = join(root, 'plugin-install-gate');
    assert.equal(invoke(['install', data, output, 'trust'], 1).error.code, 'PACKAGE_TYPE_MISMATCH');
    assert.deepEqual(invoke(['list', data]).apps, []);

    const invalid = [
      ['plugin-missing-worker', plugin, assets],
      ['plugin-invalid-exe', plugin, [{ ...worker, data: Buffer.from('MZ') }]],
      ['plugin-duplicate-method', { ...plugin, capabilities: ['open', 'open'] }, [worker]],
      ['app-with-worker', manifest, [...assets, worker]],
    ];
    for (const [name, spec, content] of invalid)
      assert.equal(invoke(['verify', await make(name, spec, content)], 1).error.code, 'PACKAGE_INVALID');

    const pluginData = join(root, 'plugin-registry');
    assert.equal(invoke(['plugin-inspect', pluginData, output]).publisherTrusted, false);
    assert.equal(invoke(['plugin-install', pluginData, output], 1).error.code, 'PUBLISHER_UNTRUSTED');
    assert.equal(invoke(['plugin-install', pluginData, v1, 'trust'], 1).error.code, 'PACKAGE_TYPE_MISMATCH');
    invoke(['plugin-install', pluginData, output, 'trust']);
    assert.equal(invoke(['plugin-list', pluginData]).plugins[0].version, '1.0.0');
    assert.equal(invoke(['list', join(pluginData, 'plugins')], 1).error.code, 'REGISTRY_KIND');
    const next = await make('serial-next', { ...plugin, version: '1.1.0' }, [worker]);
    invoke(['plugin-install', pluginData, next, 'trust', 'before-commit'], 77);
    assert.equal(invoke(['plugin-list', pluginData]).plugins[0].version, '1.0.0');
    invoke(['plugin-install', pluginData, next]);
    const resolveVersion = (min, max, status = 0) =>
      invoke(['plugin-resolve', pluginData, plugin.pluginId, min, max], status);
    assert.equal(resolveVersion('1.0.0', '2.0.0').version, '1.1.0');
    assert.equal(resolveVersion('1.0.0', '1.1.0').version, '1.0.0');
    assert.equal(resolveVersion('2.0.0', '3.0.0', 1).error.code, 'PLUGIN_UNAVAILABLE');
    const foreign = await make('serial-foreign', { ...plugin, version: '2.0.0' }, [worker], otherPem);
    assert.equal(invoke(['plugin-install', pluginData, foreign, 'trust'], 1).error.code, 'PUBLISHER_MISMATCH');
    invoke(['plugin-uninstall', pluginData, plugin.pluginId]);
    assert.deepEqual(invoke(['plugin-list', pluginData]).plugins, []);
    assert.equal(invoke(['plugin-install', pluginData, foreign, 'trust'], 1).error.code, 'PUBLISHER_MISMATCH');
  });
  await run('真实 ECDSA 签名、指纹和完整性校验', () => {
    const verified = invoke(['verify', v1]);
    assert.equal(verified.appId, manifest.appId);
    assert.equal(verified.publisherKeyId, publicInfo(pem).publisherKeyId);
    assert.equal(verified.publisherName, manifest.publisher.name);
  });
  await run('清单可声明包内 HTML 入口', async () => {
    const entry = 'start.html';
    const content = assets.map(asset => asset.name === 'assets/index.html'
      ? { ...asset, name: `assets/${entry}` } : asset);
    assert.equal(invoke(['verify', await make('custom-entry', { ...manifest, entry }, content)]).appId, manifest.appId);
    assert.equal(invoke(['verify', await make('missing-entry', { ...manifest, entry })], 1).error.code, 'PACKAGE_INVALID');
  });
  await run('拒绝不合法的发布者和窗口配置', async () => {
    const specs = [
      { ...manifest, publisher: { name: '' } },
      { ...manifest, window: { ...manifest.window, minWidth: manifest.window.width + 1 } },
    ];
    for (const [index, spec] of specs.entries())
      assert.equal(invoke(['verify', await make(`invalid-metadata-${index}`, spec)], 1).error.code, 'PACKAGE_INVALID');
  });
  await run('插件依赖清单校验及缺失插件的安装行为', async () => {
    const dependency = { pluginId: 'com.example.serial', minVersion: '1.0.0', maxVersionExclusive: '2.0.0', required: true };
    const spec = { ...manifest, appId: 'com.example.device', permissions: ['plugin.com.example.serial'], dependencies: [dependency] };
    const required = await make('required-plugin', spec);
    const data = join(root, 'plugin-dependencies');
    assert.deepEqual(invoke(['verify', required]).dependencies, [dependency]);
    assert.equal(invoke(['install', data, required, 'trust'], 1).error.code, 'PLUGIN_UNAVAILABLE');
    assert.deepEqual(invoke(['list', data]).apps, []);

    const optional = await make('optional-plugin', { ...spec, dependencies: [{ ...dependency, required: false }] });
    invoke(['install', data, optional, 'trust']);
    assert.equal(invoke(['list', data]).apps[0].appId, spec.appId);
    const update = await make('required-plugin-update', { ...spec, version: '1.1.0' });
    assert.equal(invoke(['install', data, update, 'trust'], 1).error.code, 'PLUGIN_UNAVAILABLE');
    assert.equal(invoke(['list', data]).apps[0].version, spec.version);

    const invalid = [
      { ...spec, dependencies: [] },
      { ...spec, dependencies: [dependency, dependency] },
      { ...spec, dependencies: [{ ...dependency, maxVersionExclusive: dependency.minVersion }] },
    ];
    for (const [index, candidate] of invalid.entries())
      assert.equal(invoke(['verify', await make(`invalid-plugin-${index}`, candidate)], 1).error.code, 'PACKAGE_INVALID');
  });
  for (const [name, path] of [['父目录','assets/../escape.js'],['绝对路径','/assets/x.js'],['盘符','C:/x.js'],['备用流','assets/x.js:evil'],['设备名','assets/con.js'],['尾部点','assets/x./a.js'],['反斜杠','assets\\x.js']]) {
    await run(`拒绝${name}`, async () => {
      const file = await make(`bad-path-${results.length}`, manifest, [...assets, { name: path, data: Buffer.from('bad') }]);
      assert.equal(invoke(['verify', file], 1).error.code, 'PACKAGE_INVALID');
    });
  }
  await run('拒绝大小写冲突与重复条目', async () => {
    for (const name of ['assets/index.html', 'assets/INDEX.html']) {
      const file = await make(`duplicate-${results.length}-${name.includes('INDEX')}`, manifest, [...assets, { name, data: Buffer.from('duplicate') }]);
      assert.equal(invoke(['verify', file], 1).error.code, 'PACKAGE_INVALID');
    }
  });
  await run('拒绝签名后的资源篡改与未登记资源', async () => {
    const entries = signedEntries(manifest, assets, pem);
    entries.find(item => item.name === 'assets/index.html').data = Buffer.from('篡改');
    const file = join(root, 'tampered.ypkg'); await zip(file, entries);
    assert.equal(invoke(['verify', file], 1).error.code, 'PACKAGE_INVALID');
    const extra = join(root, 'extra.ypkg');
    await zip(extra, [...signedEntries(manifest, assets, pem), { name: 'assets/extra.js', data: Buffer.from('extra') }]);
    assert.equal(invoke(['verify', extra], 1).error.code, 'PACKAGE_INVALID');
  });
  await run('拒绝伪造签名与重复 JSON 键', async () => {
    const entries = signedEntries(manifest, assets, pem);
    const signature = JSON.parse(entries[2].data); signature.signature = '00'.repeat(64); entries[2].data = Buffer.from(JSON.stringify(signature));
    const file = join(root, 'signature.ypkg'); await zip(file, entries);
    assert.equal(invoke(['verify', file], 1).error.code, 'SIGNATURE_INVALID');
    const raw = JSON.stringify({ ...manifest, publisherKeyId: publicInfo(pem).publisherKeyId }).replace('{', '{"appId":"evil.app",');
    const duplicate = await make('duplicate-json', manifest, assets, pem, { manifestRaw: raw });
    assert.equal(invoke(['verify', duplicate], 1).error.code, 'PACKAGE_INVALID');
  });
  await run('拒绝符号链接、压缩炸弹、保留身份与未支持权限', async () => {
    const cases = [
      [manifest, [...assets, { name: 'assets/link.js', data: Buffer.from('target'), attributes: (0o120777 << 16) >>> 0 }]],
      [manifest, [...assets, { name: 'assets/bomb.txt', data: Buffer.alloc(1024 * 1024) }]],
      [{ ...manifest, appId: 'org.alkaid.desktop' }, assets],
      [{ ...manifest, permissions: ['apps.manage'] }, assets],
      [{ ...manifest, runtime: { apiMajor: 2, minVersion: '2.0.0' } }, assets],
    ];
    for (let i=0; i<cases.length; i++) {
      const file = await make(`rejected-${i}`, ...cases[i]);
      assert.equal(invoke(['verify', file], 1).error.code, 'PACKAGE_INVALID');
    }
  });
  await run('未知发布者必须明确同意信任，检查不会安装', () => {
    assert.equal(invoke(['inspect', registry, v1]).publisherTrusted, false);
    assert.equal(invoke(['list', registry]).apps.length, 0);
    assert.equal(invoke(['install', registry, v1], 1).error.code, 'PUBLISHER_UNTRUSTED');
    assert.equal(invoke(['install', registry, v1, 'trust']).apps[0].version, '1.0.0');
  });
  await run('安装不移动源包，信任与激活记录跨进程保留', async () => {
    assert.ok((await readFile(v1)).length);
    assert.equal(invoke(['inspect', registry, v2]).publisherTrusted, true);
    assert.equal(invoke(['list', registry]).apps[0].error, '');
  });
  await run('更新、显式回退与版本冲突约束', async () => {
    assert.equal(invoke(['install', registry, v2]).apps[0].version, '1.1.0');
    assert.equal(invoke(['install', registry, v1], 1).error.code, 'VERSION_DOWNGRADE');
    assert.equal(invoke(['rollback', registry, manifest.appId]).apps[0].version, '1.0.0');
    const different = await make('conflicting-version', { ...manifest, name: '相同版本不同内容' });
    assert.equal(invoke(['install', registry, different], 1).error.code, 'VERSION_CONFLICT');
  });
  await run('卸载后保留身份绑定，其他发布者不能接管', async () => {
    assert.equal(invoke(['uninstall', registry, manifest.appId]).apps.length, 0);
    const replacement = await make('other-publisher', manifest, assets, otherPem);
    assert.equal(invoke(['install', registry, replacement, 'trust'], 1).error.code, 'PUBLISHER_MISMATCH');
    assert.equal(invoke(['install', registry, v1]).apps[0].version, '1.0.0');
  });
  const oldDigest = sha256(await readFile(v1));
  const newDigest = sha256(await readFile(v2));
  for (const stage of ['prepared', 'staged', 'ready', 'before-commit', 'committed']) {
    const committed = stage === 'committed';
    await run(`首次安装在 ${stage} 中断后可恢复并重试`, async () => {
      const data = join(root, `first-crash-${stage}`);
      invoke(['install', data, v1, 'trust', stage], 77);
      const recovered = invoke(['list', data]);
      assert.deepEqual(recovered.apps.map(app => app.version), committed ? [manifest.version] : []);
      assert.equal(recovered.history[0].status, committed ? 'succeeded' : 'failed');
      assert.equal(invoke(['inspect', data, v1]).publisherTrusted, committed);
      assert.deepEqual(await readdir(join(data, 'staging')), []);
      assert.deepEqual(await readdir(join(data, 'versions')), committed ? [oldDigest] : []);
      const retried = invoke(['install', data, v1, 'trust']);
      assert.equal(retried.apps[0].version, manifest.version);
      assert.equal(retried.apps[0].canRollback, false);
    });
    await run(`更新进程在 ${stage} 中断后恢复一致状态并可重试`, async () => {
      const data = join(root, `crash-${stage}`);
      invoke(['install', data, v1, 'trust']);
      invoke(['install', data, v2, 'trust', stage], 77);
      const recovered = invoke(['list', data]);
      assert.equal(recovered.apps[0].version, committed ? '1.1.0' : '1.0.0');
      assert.equal(recovered.apps[0].error, '');
      assert.equal(recovered.history[0].status, committed ? 'succeeded' : 'failed');
      assert.deepEqual(await readdir(join(data, 'staging')), []);
      assert.deepEqual((await readdir(join(data, 'versions'))).sort(), (committed ? [oldDigest, newDigest] : [oldDigest]).sort());
      const retried = invoke(['install', data, v2]);
      assert.equal(retried.apps[0].version, '1.1.0');
      assert.equal(invoke(['rollback', data, manifest.appId]).apps[0].version, '1.0.0');
    });
  }
  await run('已激活版本损坏时恢复上一份完整版本', async () => {
    const data = join(root, 'corruption');
    invoke(['install', data, v1, 'trust']);
    invoke(['install', data, v2]);
    const digest = sha256(await readFile(v2));
    await writeFile(join(data, 'versions', digest, 'package.ypkg'), 'broken');
    assert.equal(invoke(['list', data]).apps[0].version, '1.0.0');
  });
} finally {
  await writeFile(join(root, 'report.json'), JSON.stringify({ results }, null, 2));
  await writeFile('build/package-verification/latest.json', JSON.stringify({ root, v1, v2, registry, tool }, null, 2));
}
console.log(`包管理验证通过：${results.length} 项，${root}`);

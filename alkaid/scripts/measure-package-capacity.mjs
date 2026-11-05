/** 记录当前 YPKG v3 在接近容量上限时的原生验签和安装耗时。 */
import assert from 'node:assert/strict';
import { generateKeyPairSync, randomBytes } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { mkdir, readFile, stat, writeFile } from 'node:fs/promises';
import { resolve, join } from 'node:path';
import { platform, release, cpus, totalmem } from 'node:os';
import { signedEntries, zip } from './ypkg.mjs';

const mib = 1024 * 1024;
const root = resolve('build/package-capacity', `run-${Date.now()}`);
await mkdir(root, { recursive: true });
const tool = resolve('build/host/infra/package/Release/alkaid_package_tool.exe');
const manifest = JSON.parse(await readFile('samples/inspector/manifest.json', 'utf8'));
const key = generateKeyPairSync('ec', { namedCurve: 'prime256v1' }).privateKey.export({ type: 'pkcs8', format: 'pem' });
const entry = { name: 'assets/index.html', data: Buffer.from('<!doctype html><title>容量验收</title>') };
const timings = [];
async function make(name, sizes) {
  const assets = [entry, ...sizes.map((size, index) => ({
    name: `assets/block-${index}.png`, data: randomBytes(size), compression: 0,
  }))];
  const path = join(root, `${name}.ypkg`);
  const start = performance.now();
  await zip(path, signedEntries(manifest, assets, key));
  timings.push({ operation: `pack-${name}`, elapsedMs: Math.round(performance.now() - start) });
  return { path, resourceBytes: assets.reduce((total, asset) => total + asset.data.length, 0), archiveBytes: (await stat(path)).size };
}
function invoke(operation, args, exit = 0) {
  const start = performance.now();
  const result = spawnSync(tool, args, { encoding: 'utf8', timeout: 60000, windowsHide: true });
  timings.push({ operation, elapsedMs: Math.round(performance.now() - start) });
  assert.equal(result.status, exit, result.error?.message ?? result.stdout + result.stderr);
  return JSON.parse(result.stdout);
}
const candidate = await make('near-limit', [8 * mib, 8 * mib, 8 * mib, 7 * mib]);
const registry = join(root, 'registry');
assert.equal(invoke('verify', ['verify', candidate.path]).appId, manifest.appId);
assert.equal(invoke('install', ['install', registry, candidate.path, 'trust']).apps[0].appId, manifest.appId);
assert.equal(invoke('reopen-registry', ['list', registry]).apps[0].error, '');
const oversizedFile = await make('file-over-limit', [8 * mib + 1]);
assert.equal(invoke('reject-file-over-limit', ['verify', oversizedFile.path], 1).error.code, 'PACKAGE_INVALID');
const oversizedArchive = await make('archive-over-limit', Array(4).fill(8 * mib));
assert.ok(oversizedArchive.archiveBytes > 32 * mib);
assert.equal(invoke('reject-archive-over-limit', ['verify', oversizedArchive.path], 1).error.code, 'PACKAGE_IO');
const report = { environment: { platform: platform(), release: release(), cpu: cpus()[0]?.model, ramBytes: totalmem() },
  candidate, oversizedFile, oversizedArchive, timings };
await writeFile(join(root, 'report.json'), JSON.stringify(report, null, 2));
console.log(JSON.stringify(report, null, 2));

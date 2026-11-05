/** 开发侧 YPKG v3 打包与签名，私钥不进入运行时。ZIP 写入复用 Python 标准库。 */
import { createHash, createPrivateKey, createPublicKey, generateKeyPairSync, sign } from 'node:crypto';
import { readFile, writeFile, mkdir, readdir, lstat, realpath } from 'node:fs/promises';
import { resolve, dirname, relative, isAbsolute, sep } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

export const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
export function signatureInput(manifest, integrity) {
  const parts = [Buffer.from('YPKG-V3\n')];
  for (const bytes of [manifest, integrity]) { const length = Buffer.alloc(4); length.writeUInt32LE(bytes.length); parts.push(length, bytes); }
  return Buffer.concat(parts);
}
export function publicInfo(privatePem) {
  const key = createPrivateKey(privatePem);
  const jwk = createPublicKey(key).export({ format: 'jwk' });
  if (jwk.kty !== 'EC' || jwk.crv !== 'P-256') throw Error('只支持 P-256 发布者密钥');
  const publicBytes = Buffer.concat([Buffer.from([4]), Buffer.from(jwk.x, 'base64url'), Buffer.from(jwk.y, 'base64url')]);
  return { key, publicKey: publicBytes.toString('hex'), publisherKeyId: `sha256:${sha256(publicBytes)}` };
}
export async function keygen(path) {
  const { privateKey } = generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
  const pem = privateKey.export({ type: 'pkcs8', format: 'pem' });
  await mkdir(dirname(resolve(path)), { recursive: true });
  await writeFile(path, pem, { flag: 'wx', mode: 0o600 });
  return publicInfo(pem).publisherKeyId;
}
export async function zip(output, entries, python = process.env.ALKAID_PYTHON ?? 'python') {
  const script = fileURLToPath(new URL('./write-ypkg.py', import.meta.url));
  const result = spawnSync(python, [script, resolve(output)], {
    input: JSON.stringify(entries.map(entry => ({ ...entry, data: Buffer.from(entry.data).toString('base64') }))),
    encoding: 'utf8', maxBuffer: 4 * 1024 * 1024,
  });
  if (result.status !== 0) throw Error(`ZIP 写入失败：${result.stderr || result.error}`);
}
export function signedEntries(manifest, assets, pem, options = {}) {
  const publisher = publicInfo(pem);
  const meta = Buffer.from(options.manifestRaw ?? JSON.stringify({ ...manifest, publisherKeyId: publisher.publisherKeyId }));
  const integrity = Buffer.from(options.integrityRaw ?? JSON.stringify({ files: assets.map(file => ({ path: file.name, size: file.data.length, sha256: sha256(file.data) })) }));
  const signature = { algorithm: 'ECDSA-P256-SHA256', publicKey: publisher.publicKey,
    signature: sign('sha256', signatureInput(meta, integrity), { key: publisher.key, dsaEncoding: 'ieee-p1363' }).toString('hex') };
  return [{ name: 'manifest.json', data: meta }, { name: 'integrity.json', data: integrity },
    { name: 'signature.json', data: Buffer.from(JSON.stringify(signature)) }, ...assets];
}
export async function pack(manifestPath, directory, keyPath, output, localKey) {
  const manifest = JSON.parse(await readFile(manifestPath, 'utf8'));
  if (manifest?.manifestVersion !== 3 || !['app', 'plugin'].includes(manifest.type))
    throw Error('打包需要 YPKG v3 应用或插件清单');
  const plugin = manifest.type === 'plugin';
  if (plugin ? manifest.entry !== 'worker.exe' : !Array.isArray(manifest.dependencies))
    throw Error('插件必须声明 worker.exe，应用必须声明依赖列表');
  const root = await realpath(directory);
  const keyFile = await realpath(keyPath);
  const keyRelative = relative(root, keyFile);
  if (keyRelative !== '..' && !keyRelative.startsWith(`..${sep}`) && !isAbsolute(keyRelative))
    throw Error('签名私钥必须位于资源目录之外');
  const assets = [];
  let total = 0;
  async function walk(path) {
    for (const entry of await readdir(path, { withFileTypes: true })) {
      const child = resolve(path, entry.name);
      const info = await lstat(child);
      if (info.isSymbolicLink()) throw Error('打包目录不能包含链接');
      if (info.isDirectory()) await walk(child);
      else if (info.isFile()) {
        if (info.size > 8 * 1024 * 1024 || assets.length >= 512) throw Error('单文件大小或文件数量超过限制');
        total += info.size;
        if (total > 32 * 1024 * 1024) throw Error('资源总量超过限制');
        assets.push({ name: `assets/${relative(root, child).split(sep).join('/')}`, data: await readFile(child) });
      } else throw Error('只允许打包普通文件');
    }
  }
  await walk(root);
  if (typeof manifest.entry !== 'string' || (!plugin && !manifest.entry.endsWith('.html')) ||
      !assets.some(asset => asset.name === `assets/${manifest.entry}`))
    throw Error('打包目录缺少清单指定的入口');
  if (!plugin && assets.some(asset => asset.name.toLowerCase().endsWith('.exe')))
    throw Error('应用包不能携带可执行文件');
  assets.sort((a, b) => a.name.localeCompare(b.name, 'en'));
  await mkdir(dirname(resolve(output)), { recursive: true });
  await zip(output, signedEntries(manifest, assets, localKey ?? await readFile(keyFile)));
}
if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try {
    const [command, ...args] = process.argv.slice(2);
    if (command === 'keygen' && args.length === 1) console.log(await keygen(args[0]));
    else if (command === 'pack' && args.length === 4) { await pack(...args); console.log(`已生成签名包：${resolve(args[3])}`); }
    else if (command === 'pack-local' && args.length === 4) {
      const chunks = [];
      for await (const chunk of process.stdin) chunks.push(chunk);
      await pack(...args, Buffer.concat(chunks));
      console.log(`已生成签名包：${resolve(args[3])}`);
    }
    else throw Error('用法：ypkg.mjs keygen 私钥路径，或 pack 清单路径 前端目录 私钥路径 输出包路径');
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}

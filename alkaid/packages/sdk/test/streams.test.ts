/** 分块 SDK 的内存界限、背压、失败清理与关闭协商；真实系统调用另由 CEF 集成测试覆盖。 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { AlkaidClient, type BridgeTransport } from '../src/index.ts';
const id = 'a'.repeat(64);
function fixture(reply: (request: any) => unknown) {
  const messages: any[] = [], listeners = new Set<(message: string) => void>();
  const emit = (value: unknown) => { for (const listener of listeners) listener(JSON.stringify(value)); };
  const transport: BridgeTransport = {
    send(raw) { const request = JSON.parse(raw); messages.push(request); queueMicrotask(() => emit({ requestId: request.requestId, ok: true, result: reply(request) })); },
    subscribe(listener) { listeners.add(listener); return () => { listeners.delete(listener); }; },
  };
  return { client: new AlkaidClient(transport), messages, emit };
}
const begin = { streamId: id, totalBytes: 0, encoding: 'utf-8' };
test('多 MiB 文本按顺序发送完整字符的小块，提交后释放事务', async () => {
  let index = 0, text = '';
  const f = fixture(request => {
    if (request.method === 'files.beginWrite') return begin;
    if (request.method === 'files.appendChunk') {
      assert.equal(request.params.index, index++);
      assert.ok(new TextEncoder().encode(request.params.text).length <= 65536);
      assert.ok(JSON.stringify(request).length < 1048576);
      text += request.params.text; return { index, writtenBytes: new TextEncoder().encode(text).length };
    }
    return null;
  });
  const source = '分块测试🌿'.repeat(100000);
  await f.client.files.writeText(id, source);
  assert.equal(text, source); assert.ok(index > 10);
  assert.equal(f.messages.at(-2).method, 'files.commitWrite'); assert.equal(f.messages.at(-1).method, 'files.abortStream'); f.client.dispose();
});
test('取消在途传输清理暂存，绝不继续提交', async () => {
  const controller = new AbortController(); let count = 0;
  const f = fixture(request => request.method === 'files.beginWrite' ? begin : request.method === 'files.appendChunk' ? { index: ++count, writtenBytes: count * 100 } : null);
  await assert.rejects(f.client.files.writeText(id, 'x'.repeat(200000), { signal: controller.signal, onProgress: () => controller.abort() }), { code: 'ABORTED' });
  assert.equal(count, 1); assert.ok(!f.messages.some(request => request.method === 'files.commitWrite'));
  assert.equal(f.messages.at(-1).method, 'files.abortStream'); f.client.dispose();
});
test('二进制迭代源失败后清理事务', async () => {
  const f = fixture(request => request.method === 'files.beginWrite' ? begin : request.method === 'files.appendChunk' ? { index: 1, writtenBytes: 3 } : null);
  async function* data() { yield Uint8Array.of(0, 128, 255); throw Error('输入流中断'); }
  await assert.rejects(f.client.files.writeChunks(id, data()), /输入流中断/);
  assert.ok(!f.messages.some(request => request.method === 'files.commitWrite')); assert.equal(f.messages.at(-1).method, 'files.abortStream'); f.client.dispose();
});
test('提前退出读取迭代器释放宿主流，保持按需背压', async () => {
  let count = 0;
  const f = fixture(request => request.method === 'files.beginRead' ? { ...begin, totalBytes: 200000, encoding: 'binary' } :
    request.method === 'files.readNext' ? { processedBytes: ++count * 65536, totalBytes: 200000, eof: false, data: btoa('x'.repeat(65536)), encoding: 'binary' } : null);
  for await (const chunk of f.client.files.readChunks(id)) { assert.equal(chunk.length, 65536); break; }
  assert.equal(count, 1); assert.equal(f.messages.at(-1).method, 'files.abortStream'); f.client.dispose();
});
test('二进制读取拒绝非规范 Base64 和长度不符的块', async () => {
  for (const [data, totalBytes] of [['/x==', 1], ['/w==', 2]] as const) {
    const f = fixture(request => request.method === 'files.beginRead' ? { ...begin, totalBytes, encoding: 'binary' } :
      request.method === 'files.readNext' ? { processedBytes: totalBytes, totalBytes, eof: true, data, encoding: 'binary' } : null);
    await assert.rejects(f.client.files.readBytes(id), { code: 'INVALID_RESPONSE' });
    assert.equal(f.messages.at(-1).method, 'files.abortStream');
    f.client.dispose();
  }
});
test('整文件预算和截断响应明确失败并释放流', async () => {
  const f = fixture(request => request.method === 'files.beginRead' ? { ...begin, totalBytes: 100 } : { text: 'x', processedBytes: 1, totalBytes: 100, eof: true, encoding: 'utf-8' });
  await assert.rejects(f.client.files.readText(id, { maxBytes: 10 }), { code: 'FILE_TOO_LARGE' });
  await assert.rejects(f.client.files.readText(id), { code: 'INVALID_RESPONSE' }); f.client.dispose();
});
test('非法文本参数不会被转换为空文件', async () => {
  const f = fixture(() => null);
  await assert.rejects(f.client.files.writeText(id, undefined as any), { code: 'INVALID_REQUEST' });
  assert.equal(f.messages.length, 0); f.client.dispose();
});
test('关闭处理器保存失败时拒绝关闭', async () => {
  const f = fixture(() => null);
  await f.client.window.onCloseRequested(async () => { throw Error('保存失败'); });
  f.emit({ event: 'window.closeRequested', payload: { requestId: id, reason: 'manager' } });
  await new Promise(resolve => setTimeout(resolve, 0));
  assert.equal(f.messages.find(request => request.method === 'window.resolveClose')?.params.allow, false); f.client.dispose();
});
test('平台退出被取消后通知已经同意的页面恢复交互', async () => {
  const f = fixture(() => null); let signal: AbortSignal | undefined;
  await f.client.window.onCloseRequested(request => { signal = request.signal; return true; });
  f.emit({ event: 'window.closeRequested', payload: { requestId: id, reason: 'platform' } });
  await new Promise(resolve => setTimeout(resolve, 0)); assert.equal(signal?.aborted, false);
  f.emit({ event: 'window.closeCancelled', payload: { requestId: id } }); assert.equal(signal?.aborted, true); f.client.dispose();
});

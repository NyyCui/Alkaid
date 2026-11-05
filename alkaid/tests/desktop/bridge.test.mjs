/** CEF 传输适配器清理测试，不替代真实宿主验证。 */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { setTimeout as delay } from 'node:timers/promises';
import { createCefTransport } from '../../../desktop/src/bridge.js';

test('CEF 不存在时不创建伪传输', () => {
  assert.equal(createCefTransport(), undefined);
});
test('完成、超时和销毁分别清理 CEF 查询', async () => {
  let id = 0;
  const queries = new Map();
  const cancelled = [];
  globalThis.__alkaidQuery = callbacks => { queries.set(++id, callbacks); return id; };
  globalThis.__alkaidCancel = queryId => cancelled.push(queryId);
  try {
    const transport = createCefTransport();
    const replies = [];
    const stop = transport.subscribe(reply => replies.push(reply));
    transport.send(JSON.stringify({ requestId: 'first', timeoutMs: 20 }));
    queries.get(1).onSuccess('完成');
    transport.send(JSON.stringify({ requestId: 'second', timeoutMs: 10 }));
    await delay(40);
    assert.deepEqual(replies, ['完成']);
    assert.deepEqual(cancelled, [2]);
    transport.send(JSON.stringify({ requestId: 'third', timeoutMs: 1000 }));
    stop();
    assert.deepEqual(cancelled, [2, 3]);
  } finally {
    delete globalThis.__alkaidQuery;
    delete globalThis.__alkaidCancel;
  }
});

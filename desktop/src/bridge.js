/** 将 CEF 异步消息路由适配为 SDK 传输，不向页面暴露本机对象或身份授权。 */
export function createCefTransport() {
  if (typeof globalThis.__alkaidQuery !== 'function' || typeof globalThis.__alkaidCancel !== 'function') return undefined;
  const listeners = new Set();
  const queries = new Map();
  const receiveEvent = event => {
    if (!event.detail || typeof event.detail.event !== 'string') return;
    const message = JSON.stringify(event.detail);
    for (const listener of listeners) listener(message);
  };
  return {
    send(message) {
      const request = JSON.parse(message);
      let queryId;
      const deliver = response => {
        clearTimeout(queries.get(queryId)?.timer);
        queries.delete(queryId);
        for (const listener of listeners) listener(response);
      };
      queryId = globalThis.__alkaidQuery({
        request: message, persistent: false, onSuccess: deliver,
        onFailure: (_code, description) => deliver(JSON.stringify({ requestId: request.requestId, ok: false,
          error: { code: 'TRANSPORT_ERROR', message: description || '系统连接已中断', retryable: false } })),
      });
      const timer = setTimeout(() => {
        queries.delete(queryId);
        globalThis.__alkaidCancel(queryId);
      }, request.timeoutMs);
      queries.set(queryId, {timer,requestId:request.requestId});
    },
    cancel(requestId) {
      for (const [queryId,entry] of queries) if(entry.requestId===requestId) {
        clearTimeout(entry.timer);queries.delete(queryId);globalThis.__alkaidCancel(queryId);return;
      }
    },
    subscribe(listener) {
      if (!listeners.size) globalThis.addEventListener?.('alkaid:runtime-event', receiveEvent);
      listeners.add(listener);
      return () => {
        listeners.delete(listener);
        if (!listeners.size) {
          globalThis.removeEventListener?.('alkaid:runtime-event', receiveEvent);
          for (const [queryId, entry] of queries) {
            clearTimeout(entry.timer);
            globalThis.__alkaidCancel(queryId);
          }
          queries.clear();
        }
      };
    },
  };
}

/** 统一系统调用错误，不把异常操作显示为成功。 */
export class AlkaidError extends Error {
  readonly code: string;
  readonly retryable: boolean;
  readonly traceId?: string;
  constructor(code: string, message: string, retryable = false, traceId?: string) {
    super(message); this.name = 'AlkaidError'; this.code = code; this.retryable = retryable; this.traceId = traceId;
  }
}

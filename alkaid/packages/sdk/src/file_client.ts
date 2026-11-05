/** SDK 封装有界传输、顺序背压和取消；本机读写与编码转换由宿主执行。 */
import { AlkaidError } from './errors.ts';
import type { MethodName } from './method_catalog.ts';
export type TextEncoding = 'utf-8' | 'utf-8-bom' | 'utf-16le' | 'utf-16be' | 'gb18030';
export interface TransferProgress { processedBytes: number; totalBytes: number; encoding?: string; }
export interface TransferOptions { signal?: AbortSignal; onProgress?: (progress: TransferProgress) => void; }
export interface ReadTextOptions extends TransferOptions { encoding?: TextEncoding | 'auto'; maxBytes?: number; }
export interface WriteTextOptions extends TransferOptions { encoding?: TextEncoding; }
type Invoke = <T>(method: MethodName, validate: (value: unknown) => boolean, params: Record<string, unknown>) => Promise<T>;
interface StreamRef { streamId: string; totalBytes: number; encoding: string; }
interface Chunk extends TransferProgress { text?: string; data?: string; eof: boolean; }
interface DecodedChunk extends Chunk { bytes?: Uint8Array; }
type Base64Bytes = Uint8Array & { toBase64?(): string };
const byteArray = Uint8Array as typeof Uint8Array & { fromBase64?(text: string): Uint8Array };
const CHUNK = 65536;
const record = (value: unknown): value is Record<string, unknown> => typeof value === 'object' && value !== null && !Array.isArray(value);
const integer = (value: unknown): value is number => Number.isSafeInteger(value) && (value as number) >= 0;
const validRef = (value: unknown): boolean => value === null || (record(value) && typeof value.streamId === 'string' && /^[a-f0-9]{64}$/.test(value.streamId) && integer(value.totalBytes) && typeof value.encoding === 'string');
const validChunk = (value: unknown): boolean => record(value) && integer(value.processedBytes) && integer(value.totalBytes) && value.processedBytes <= value.totalBytes &&
  typeof value.eof === 'boolean' && typeof value.encoding === 'string' &&
  ((typeof value.text === 'string' && value.data === undefined) || (typeof value.data === 'string' && value.text === undefined && value.data.length <= 87384));
const validNull = (value: unknown): boolean => value === null;
function aborted(signal?: AbortSignal): void { if (signal?.aborted) throw new AlkaidError('ABORTED', '操作已取消，未提交的写入会被清理'); }
function maxBytes(value: number | undefined): number {
  const limit = value ?? 16 * 1024 * 1024;
  if (!Number.isSafeInteger(limit) || limit < 0 || limit > 64 * 1024 * 1024) throw new AlkaidError('INVALID_REQUEST', '整文件读取内存预算须为 0～64 MiB；更大的文件请逐块处理');
  return limit;
}
function encode(bytes: Base64Bytes): string {
  if (bytes.toBase64) return bytes.toBase64();
  let text = '';
  for (let offset = 0; offset < bytes.length; offset += 8192)
    text += String.fromCharCode(...bytes.subarray(offset, offset + 8192));
  return btoa(text);
}
function decode(text: string): Uint8Array {
  try {
    if (byteArray.fromBase64) {
      const bytes = byteArray.fromBase64(text);
      if (bytes.length > CHUNK || encode(bytes) !== text) throw Error();
      return bytes;
    }
    const raw = atob(text);
    if (raw.length > CHUNK || btoa(raw) !== text) throw Error();
    const bytes = new Uint8Array(raw.length);
    for (let index = 0; index < raw.length; ++index) bytes[index] = raw.charCodeAt(index);
    return bytes;
  }
  catch { throw new AlkaidError('INVALID_RESPONSE', '宿主返回的二进制块格式不合法'); }
}
export class FileApi {
  private readonly invoke: Invoke;
  constructor(invoke: Invoke) { this.invoke = invoke; }
  close = (handle: string): Promise<null> => this.invoke('files.close', validNull, { handle });
  removePrivate = (name: string): Promise<null> => this.invoke('files.removePrivate', validNull, { name });
  private abort = async (id: string): Promise<void> => { await this.invoke('files.abortStream', validNull, { streamId: id }).catch(() => {}); };
  private async *read(method: MethodName, params: Record<string, unknown>, options: TransferOptions, limit?: number): AsyncGenerator<DecodedChunk> {
    aborted(options.signal);
    const ref = await this.invoke<StreamRef | null>(method, validRef, params);
    if (!ref) return;
    try {
      if (limit !== undefined && ref.totalBytes > limit) throw new AlkaidError('FILE_TOO_LARGE', '文件超过此次整文件读取的内存预算，请增大 maxBytes 或改用分块接口');
      let previous = 0;
      while (true) {
        aborted(options.signal);
        const chunk = await this.invoke<Chunk>('files.readNext', validChunk, { streamId: ref.streamId });
        const bytes = typeof chunk.data === 'string' ? decode(chunk.data) : undefined;
        if (chunk.totalBytes !== ref.totalBytes || chunk.processedBytes < previous || (!chunk.eof && chunk.processedBytes === previous) ||
            (chunk.eof && chunk.processedBytes !== ref.totalBytes) || chunk.processedBytes - previous > CHUNK ||
            (bytes && bytes.length !== chunk.processedBytes - previous))
          throw new AlkaidError('INVALID_RESPONSE', '文件读取进度不符合契约');
        aborted(options.signal); previous = chunk.processedBytes;
        options.onProgress?.({ processedBytes: chunk.processedBytes, totalBytes: chunk.totalBytes, encoding: chunk.encoding });
        yield { ...chunk, bytes };
        if (chunk.eof) return;
      }
    } finally { await this.abort(ref.streamId); }
  }
  async *readChunks(handle: string, options: TransferOptions = {}): AsyncGenerator<Uint8Array> {
    for await (const chunk of this.read('files.beginRead', { handle, encoding: 'binary' }, options)) {
      if (!chunk.bytes) throw new AlkaidError('INVALID_RESPONSE', '二进制流缺少数据块');
      yield chunk.bytes;
    }
  }
  async *readTextChunks(handle: string, options: ReadTextOptions = {}): AsyncGenerator<string> {
    for await (const chunk of this.read('files.beginRead', { handle, encoding: options.encoding ?? 'auto' }, options)) {
      if (typeof chunk.text !== 'string') throw new AlkaidError('INVALID_RESPONSE', '文本流缺少文本块');
      yield chunk.text;
    }
  }
  async readBytes(handle: string, options: TransferOptions & { maxBytes?: number } = {}): Promise<Uint8Array> {
    const limit = maxBytes(options.maxBytes), chunks: Uint8Array[] = []; let length = 0;
    for await (const chunk of this.read('files.beginRead', { handle, encoding: 'binary' }, options, limit)) {
      if (!chunk.bytes) throw new AlkaidError('INVALID_RESPONSE', '二进制流缺少数据块');
      length += chunk.bytes.length; if (length > limit) throw new AlkaidError('FILE_TOO_LARGE', '超过内存预算'); chunks.push(chunk.bytes);
    }
    const result = new Uint8Array(length); let offset = 0;
    for (const bytes of chunks) { result.set(bytes, offset); offset += bytes.length; }
    return result;
  }
  private async readTextValue(method: MethodName, params: Record<string, unknown>, options: ReadTextOptions): Promise<string | null> {
    const limit = maxBytes(options.maxBytes), chunks: string[] = []; let bytes = 0, found = false;
    for await (const chunk of this.read(method, params, options, limit)) {
      found = true;
      if (typeof chunk.text !== 'string') throw new AlkaidError('INVALID_RESPONSE', '文本流缺少文本块');
      bytes += new TextEncoder().encode(chunk.text).length;
      if (bytes > limit) throw new AlkaidError('FILE_TOO_LARGE', '解码后的文本超过内存预算，请改用分块读取');
      chunks.push(chunk.text);
    }
    return found ? chunks.join('') : null;
  }
  readText = async (handle: string, options: ReadTextOptions = {}): Promise<string> => {
    const text = await this.readTextValue('files.beginRead', { handle, encoding: options.encoding ?? 'auto' }, options);
    if (text === null) throw new AlkaidError('INVALID_RESPONSE', '外部文件读取缺少流');
    return text;
  };
  readPrivateText = (name: string, options: ReadTextOptions = {}): Promise<string | null> => this.readTextValue('files.beginPrivateRead', { name, encoding: options.encoding ?? 'auto' }, options);
  private async write(method: MethodName, params: Record<string, unknown>, chunks: AsyncIterable<string | Uint8Array> | Iterable<string | Uint8Array>, options: TransferOptions, totalBytes: number): Promise<null> {
    aborted(options.signal);
    const ref = await this.invoke<StreamRef | null>(method, validRef, params);
    if (!ref) throw new AlkaidError('INVALID_RESPONSE', '宿主未创建写入事务');
    let index = 0, processed = 0;
    try {
      for await (const chunk of chunks) {
        aborted(options.signal);
        const result = await this.invoke<{ index: number; writtenBytes: number }>('files.appendChunk', value => record(value) && value.index === index + 1 && integer(value.writtenBytes),
          { streamId: ref.streamId, index, ...(typeof chunk === 'string' ? { text: chunk } : { data: encode(chunk) }) });
        ++index; processed += typeof chunk === 'string' ? new TextEncoder().encode(chunk).length : chunk.length;
        options.onProgress?.({ processedBytes: processed, totalBytes, encoding: params.encoding as string });
        if (result.writtenBytes > 8 * 1024 * 1024 * 1024) throw new AlkaidError('INVALID_RESPONSE', '宿主返回的写入大小超过暂存预算');
      }
      aborted(options.signal);
      return await this.invoke('files.commitWrite', validNull, { streamId: ref.streamId });
    } finally { await this.abort(ref.streamId); }
  }
  private *textChunks(text: string): Generator<string> {
    // 每块最多约 48 KiB UTF-8，且不能切断 UTF-16 代理项。
    for (let offset = 0; offset < text.length;) {
      let end = Math.min(offset + 16384, text.length);
      if (end < text.length && text.charCodeAt(end - 1) >= 0xd800 && text.charCodeAt(end - 1) <= 0xdbff) --end;
      const part = text.slice(offset, end);
      if (/[^\uD800-\uDBFF][\uDC00-\uDFFF]|^[\uDC00-\uDFFF]|[\uD800-\uDBFF](?![\uDC00-\uDFFF])/u.test(part)) throw new AlkaidError('FILE_ENCODING', '文本含有不完整的 Unicode 字符');
      yield part; offset = end;
    }
  }
  writeText = (handle: string, text: string, options: WriteTextOptions = {}): Promise<null> => typeof text === 'string' ? this.write('files.beginWrite', { handle, encoding: options.encoding ?? 'utf-8' }, this.textChunks(text), options, new TextEncoder().encode(text).length) : Promise.reject(new AlkaidError('INVALID_REQUEST', '文本必须是字符串'));
  writePrivateText = (name: string, text: string, options: WriteTextOptions = {}): Promise<null> => typeof text === 'string' ? this.write('files.beginPrivateWrite', { name, encoding: options.encoding ?? 'utf-8' }, this.textChunks(text), options, new TextEncoder().encode(text).length) : Promise.reject(new AlkaidError('INVALID_REQUEST', '文本必须是字符串'));
  async writeChunks(handle: string, chunks: AsyncIterable<Uint8Array> | Iterable<Uint8Array>, options: TransferOptions = {}): Promise<null> {
    async function* split(): AsyncGenerator<Uint8Array> {
      for await (const chunk of chunks) {
        if (!(chunk instanceof Uint8Array)) throw new AlkaidError('INVALID_REQUEST', '二进制写入需要 Uint8Array');
        for (let offset = 0; offset < chunk.length; offset += CHUNK) yield chunk.subarray(offset, offset + CHUNK);
      }
    }
    return this.write('files.beginWrite', { handle, encoding: 'binary' }, split(), options, 0);
  }
  writeBytes = (handle: string, bytes: Uint8Array, options: TransferOptions = {}): Promise<null> => this.writeChunks(handle, [bytes], options);
}

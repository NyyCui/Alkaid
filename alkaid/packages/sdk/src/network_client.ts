/** 网络 API 只传递端点标识和相对路径；连接、授权与 HTTP 执行归宿主负责。 */
import { AlkaidError } from './errors.ts';
import type { MethodName } from './method_catalog.ts';
export interface NetworkEndpoint { id: string; baseUrl: string; methods: string[]; allowPrivateNetwork: boolean; scope: string; }
export interface NetworkOptions {
  method?: 'GET'|'HEAD'|'POST'|'PUT'|'PATCH'|'DELETE'; headers?: Record<string,string>; body?: string|Uint8Array;
  signal?: AbortSignal; maxResponseBytes?: number; onProgress?: (value: {processedBytes: number; totalBytes: number|null}) => void;
}
export interface NetworkStream { status: number; ok: boolean; headers: Record<string,string>; totalBytes: number|null; body: AsyncIterable<Uint8Array>; close(): Promise<void>; }
export interface NetworkResponse { status: number; ok: boolean; headers: Record<string,string>; body: Uint8Array; text(): string; json<T=unknown>(): T; }
type Invoke=<T>(method:MethodName,validate:(value:unknown)=>boolean,params:Record<string,unknown>,signal?:AbortSignal)=>Promise<T>;
const record=(v:unknown):v is Record<string,unknown>=>typeof v==='object'&&v!==null&&!Array.isArray(v);
const natural=(v:unknown):v is number=>Number.isSafeInteger(v)&&(v as number)>=0;
export const validEndpoint=(v:unknown):boolean=>record(v)&&typeof v.id==='string'&&typeof v.baseUrl==='string'&&Array.isArray(v.methods)&&v.methods.every(x=>typeof x==='string')&&typeof v.allowPrivateNetwork==='boolean'&&typeof v.scope==='string';
const validNull=(v:unknown)=>v===null;
function aborted(signal?:AbortSignal):void {if(signal?.aborted)throw new AlkaidError('ABORTED','网络请求已取消，服务端已经处理的操作不能撤销');}
function base64(bytes:Uint8Array):string {let raw='';for(const byte of bytes)raw+=String.fromCharCode(byte);return btoa(raw);}
function bytes(value:string):Uint8Array {try{const raw=atob(value);if(raw.length>65536||btoa(raw)!==value)throw Error();return Uint8Array.from(raw,c=>c.charCodeAt(0));}catch{throw new AlkaidError('INVALID_RESPONSE','网络数据块不是规范 Base64');}}
interface Meta {transferId:string;status:number;headers:Record<string,string>;totalBytes:number|null;}
interface Chunk {data:string;processedBytes:number;eof:boolean;}
const validMeta=(v:unknown):boolean=>record(v)&&typeof v.transferId==='string'&&/^[a-f0-9]{64}$/.test(v.transferId)&&natural(v.status)&&v.status>=100&&v.status<=599&&record(v.headers)&&Object.values(v.headers).every(x=>typeof x==='string')&&(v.totalBytes===null||natural(v.totalBytes));
export class NetworkApi {
  private readonly invoke:Invoke;
  constructor(invoke:Invoke){this.invoke=invoke;}
  getEndpoints=():Promise<NetworkEndpoint[]>=>this.invoke('net.getEndpoints',v=>Array.isArray(v)&&v.every(validEndpoint),{});
  async stream(endpoint:string,path:string,options:NetworkOptions={}):Promise<NetworkStream> {
    aborted(options.signal);
    const limit=options.maxResponseBytes??64*1024*1024;
    if(!natural(limit)||limit>256*1024*1024)throw new AlkaidError('INVALID_REQUEST','网络响应预算须为 0～256 MiB');
    if(options.body!==undefined&&typeof options.body!=='string'&&!(options.body instanceof Uint8Array))throw new AlkaidError('INVALID_REQUEST','网络正文需要字符串或 Uint8Array');
    const body=typeof options.body==='string'?new TextEncoder().encode(options.body):options.body??new Uint8Array();
    if(body.length>256*1024)throw new AlkaidError('PAYLOAD_TOO_LARGE','本期网络请求正文上限为 256 KiB');
    const headers:Record<string,string>={};
    for(const [key,value]of Object.entries(options.headers??{})){const normalized=key.toLowerCase();if(normalized in headers)throw new AlkaidError('INVALID_REQUEST','请求头名称重复');headers[normalized]=value;}
    const meta=await this.invoke<Meta>('net.open',validMeta,{endpoint,path,method:options.method??'GET',headers,bodyBase64:base64(body),maxResponseBytes:limit},options.signal);
    let closed=false,consumed=false;const invoke=this.invoke;
    const close=async()=>{if(closed)return;closed=true;options.signal?.removeEventListener('abort',cancel);await invoke('net.close',validNull,{transferId:meta.transferId}).catch(()=>{});};
    const cancel=()=>{void close();};options.signal?.addEventListener('abort',cancel,{once:true});
    if(options.signal?.aborted){await close();aborted(options.signal);}
    if(meta.totalBytes!==null&&meta.totalBytes>limit){await close();throw new AlkaidError('RESPONSE_TOO_LARGE','响应超过本次读取预算');}
    async function* read():AsyncGenerator<Uint8Array> {
      if(consumed)throw new AlkaidError('INVALID_REQUEST','网络响应流只能消费一次');consumed=true;let processed=0;
      try {
        while(!closed){
          aborted(options.signal);
          const chunk=await invoke<Chunk>('net.readNext',v=>record(v)&&typeof v.data==='string'&&v.data.length<=87384&&natural(v.processedBytes)&&typeof v.eof==='boolean',{transferId:meta.transferId},options.signal);
          aborted(options.signal);const data=bytes(chunk.data);processed+=data.length;
          if(processed!==chunk.processedBytes||processed>limit||(!chunk.eof&&!data.length)||(chunk.eof&&meta.totalBytes!==null&&processed!==meta.totalBytes))throw new AlkaidError('INVALID_RESPONSE','网络响应长度或进度不符合契约');
          options.onProgress?.({processedBytes:processed,totalBytes:meta.totalBytes});
          if(data.length)yield data;if(chunk.eof)return;
        }
        aborted(options.signal);
      } finally {await close();}
    }
    return {status:meta.status,ok:meta.status>=200&&meta.status<300,headers:meta.headers,totalBytes:meta.totalBytes,body:read(),close};
  }
  async request(endpoint:string,path:string,options:NetworkOptions={}):Promise<NetworkResponse> {
    const limit=options.maxResponseBytes??8*1024*1024;
    if(!natural(limit)||limit>64*1024*1024)throw new AlkaidError('INVALID_REQUEST','整份网络响应预算最多 64 MiB，更大响应请使用 net.stream');
    const response=await this.stream(endpoint,path,{...options,maxResponseBytes:limit});const chunks:Uint8Array[]=[];let length=0;
    try {for await(const chunk of response.body){chunks.push(chunk);length+=chunk.length;}}finally{await response.close();}
    const body=new Uint8Array(length);let offset=0;for(const chunk of chunks){body.set(chunk,offset);offset+=chunk.length;}
    const text=()=>{try{return new TextDecoder('utf-8',{fatal:true}).decode(body);}catch{throw new AlkaidError('RESPONSE_ENCODING','响应不是有效 UTF-8，请按二进制处理');}};
    return {status:response.status,ok:response.ok,headers:response.headers,body,text,json:<T>()=>JSON.parse(text()) as T};
  }
}

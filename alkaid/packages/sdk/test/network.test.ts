/** 网络 SDK 契约、分块背压、取消与失败清理；真实 WinHTTP 另行集成验证。 */
import {test} from 'node:test';import assert from 'node:assert/strict';
import {AlkaidClient} from '../src/index.ts';
const id='b'.repeat(64);const meta={transferId:id,status:200,headers:{'content-type':'application/json'},totalBytes:null};
function fixture(reply:(r:any)=>any){let listener:(s:string)=>void=()=>{};const calls:any[]=[],cancelled:string[]=[];
 const client=new AlkaidClient({subscribe:f=>{listener=f;return()=>{};},cancel:id=>cancelled.push(id),send:raw=>{const r=JSON.parse(raw);calls.push(r);const result=reply(r);if(result!==undefined)queueMicrotask(()=>listener(JSON.stringify({requestId:r.requestId,ok:true,result})));}});
 return {client,calls,cancelled};}
test('网络正文完整分块组装，状态与业务错误不会伪装传输异常',async()=>{
 let offset=0;const f=fixture(r=>r.method==='net.open'?{...meta,status:422}:r.method==='net.readNext'?offset++?{data:'',processedBytes:7,eof:true}:{data:btoa('{"a":1}'),processedBytes:7,eof:false}:null);
 const response=await f.client.net.request('api','/test');assert.equal(response.status,422);assert.equal(response.ok,false);assert.deepEqual(response.json(),{a:1});assert.equal(f.calls.at(-1).method,'net.close');f.client.dispose();
});
test('流式响应按消费速度读取，提前退出关闭宿主流',async()=>{
 const f=fixture(r=>r.method==='net.open'?meta:r.method==='net.readNext'?{data:btoa('abc'),processedBytes:3,eof:false}:null);
 const stream=await f.client.net.stream('api','/test');assert.equal(f.calls.length,1);for await(const data of stream.body){assert.deepEqual([...data],[97,98,99]);break;}
 assert.equal(f.calls.filter(x=>x.method==='net.readNext').length,1);assert.equal(f.calls.at(-1).method,'net.close');f.client.dispose();
});
test('取消待响应的请求立即拒绝并取消原生查询',async()=>{
 const f=fixture(()=>undefined),controller=new AbortController();const result=f.client.net.request('api','/slow',{signal:controller.signal});controller.abort();await assert.rejects(result,{code:'ABORTED'});assert.equal(f.cancelled.length,1);f.client.dispose();
});
test('等待消费期间取消关闭流，禁止随后继续读取',async()=>{
 const f=fixture(r=>r.method==='net.open'?meta:null),controller=new AbortController();const stream=await f.client.net.stream('api','/test',{signal:controller.signal});controller.abort();await assert.rejects(async()=>{for await(const chunk of stream.body)void chunk;},{code:'ABORTED'});assert.equal(f.calls.at(-1).method,'net.close');f.client.dispose();
});
test('非法长度与超预算在组装前失败并释放流',async()=>{
 const f=fixture(r=>r.method==='net.open'?{...meta,totalBytes:20}:r.method==='net.readNext'?{data:btoa('x'),processedBytes:1,eof:true}:null);
 await assert.rejects(f.client.net.request('api','/',{maxResponseBytes:10}),{code:'RESPONSE_TOO_LARGE'});await assert.rejects(f.client.net.request('api','/'),{code:'INVALID_RESPONSE'});assert.equal(f.calls.at(-1).method,'net.close');f.client.dispose();
});
test('请求体限制在发送前执行，查询不接受网页提供的真实 URL 字段',async()=>{
 const f=fixture(()=>null);await assert.rejects(f.client.net.request('api','/',{body:new Uint8Array(262145)}),{code:'PAYLOAD_TOO_LARGE'});assert.equal(f.calls.length,0);f.client.dispose();
});

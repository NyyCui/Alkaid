/** 仅在显式本机回环端口提供测试数据，记录请求用于证明重定向和浏览器旁路未发出。 */
import http from 'node:http';
import https from 'node:https';
import {writeFile,readFile} from 'node:fs/promises';
import {resolve} from 'node:path';
const output=resolve(process.argv[2]??'build/network-fixture.json');const hits=[];
const handler=async(req,res)=>{
  let body=Buffer.alloc(0);for await(const part of req){body=Buffer.concat([body,part]);if(body.length>1024*1024){res.writeHead(413).end();return;}}
  hits.push({url:req.url,method:req.method,headers:req.headers,bytes:body.length});
  const path=new URL(req.url,'http://fixture.test').pathname;
  res.setHeader('Content-Type','application/json; charset=utf-8');res.setHeader('Set-Cookie','fixture=secret; HttpOnly');
  if(path==='/hits'){res.end(JSON.stringify(hits));return;}
  if(path==='/slow'){setTimeout(()=>{if(!res.destroyed)res.end('{"late":true}');},2000);return;}
  if(path==='/redirect'){res.writeHead(302,{Location:'/forbidden-target'}).end();return;}
  if(path==='/error'){res.writeHead(422).end('{"error":"业务校验未通过"}');return;}
  if(path==='/binary'){res.setHeader('Content-Type','application/octet-stream');const bytes=Buffer.alloc(1048613);for(let i=0;i<bytes.length;i++)bytes[i]=i%256;res.setHeader('Content-Length',bytes.length);res.end(bytes);return;}
  if(path==='/chunked'){res.setHeader('Content-Type','application/octet-stream');for(let i=0;i<32;i++)res.write(Buffer.alloc(32768,i));res.end();return;}
  if(path==='/echo'){res.end(JSON.stringify({method:req.method,body:body.toString(),headers:req.headers}));return;}
  res.end(JSON.stringify({message:'来自真实网络服务',method:req.method,path:req.url}));
};
const server=http.createServer(handler);
const secure=process.argv[3]?https.createServer({pfx:await readFile(process.argv[3])},handler):null;
if(secure)await new Promise(resolve=>secure.listen(0,'127.0.0.1',resolve));
server.listen(0,'127.0.0.1',async()=>{await writeFile(output,JSON.stringify({port:server.address().port,tlsPort:secure?.address().port,pid:process.pid}));console.log('测试网络服务已启动');});
process.on('SIGTERM',()=>{server.close();secure?.close();});

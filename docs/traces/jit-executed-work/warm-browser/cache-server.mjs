// Isolated diagnostic HTTP origin. Never imported by production.
import {createServer} from 'node:http';
import {createReadStream} from 'node:fs';
import {readFile, realpath, stat} from 'node:fs/promises';
import {resolve, extname, sep} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
export async function cacheServer(directory, {cache='public, max-age=3600', log=()=>{}}={}) {
  const root=await realpath(directory), seedPath=await realpath(resolve(root,'jit/seed.wasm'));
  if (!seedPath.startsWith(root+sep)) throw Error('Seed outside root');
  const seed=await readFile(seedPath), seedInfo=await stat(seedPath);
  const sha256=createHash('sha256').update(seed).digest('hex'), etag='"'+sha256+'"';
  const types={'.html':'text/html','.js':'text/javascript','.mjs':'text/javascript','.json':'application/json','.wasm':'application/wasm','.css':'text/css','.svg':'image/svg+xml','.png':'image/png','.gz':'application/gzip'};
  const server=createServer(async(req,res)=>{
    let bytes=0;
    res.on('finish',()=>log({time:new Date().toISOString(),method:req.method,url:req.url,status:res.statusCode,bytes,ifNoneMatch:req.headers['if-none-match']??null}));
    try {
      if (!['GET','HEAD'].includes(req.method)) {res.writeHead(405);res.end();return;}
      const path=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
      if (path==='/jit/seed.wasm') {
        const headers={'Content-Type':'application/wasm','Cache-Control':cache,'ETag':etag,'Last-Modified':seedInfo.mtime.toUTCString(),'X-Content-Type-Options':'nosniff'};
        if(req.headers['if-none-match']?.split(',').map(v=>v.trim().replace(/^W\//,'')).some(v=>v===etag||v==='*')) {res.writeHead(304,headers);res.end();return;}
        res.writeHead(200,{...headers,'Content-Length':seed.length});
        if(req.method==='GET'){bytes=seed.length;res.end(seed);}else res.end();return;
      }
      const file=await realpath(resolve(root,'.'+(path.endsWith('/')?path+'index.html':path)));
      if(!file.startsWith(root+sep)){res.writeHead(403);res.end();return;}
      const info=await stat(file);if(!info.isFile())throw Error('Not a file');
      res.writeHead(200,{'Content-Type':types[extname(file)]||'application/octet-stream','Content-Length':info.size,'Cache-Control':'no-cache','X-Content-Type-Options':'nosniff'});
      if(req.method==='HEAD')res.end();else{bytes=info.size;createReadStream(file).on('error',()=>res.destroy()).pipe(res);}
    }catch{if(!res.headersSent){res.writeHead(404);res.end('Not found');}else res.destroy();}
  });
  return {server,seed:{sha256,bytes:seed.length,etag,lastModified:seedInfo.mtime.toUTCString(),cache}};
}
if(process.argv[1]&&resolve(process.argv[1])===fileURLToPath(import.meta.url)){
  if(!process.env.WEB_ROOT)throw Error('WEB_ROOT must be an explicit frozen diagnostic site');
  const result=await cacheServer(resolve(process.env.WEB_ROOT),{cache:process.env.SEED_CACHE_CONTROL||'public, max-age=3600',log:event=>console.log(JSON.stringify(event))});
  result.server.listen(Number(process.env.PORT||8186),'127.0.0.1',()=>console.log(JSON.stringify({ready:true,...result.seed,address:result.server.address()})));
}

/** Fail closed: deploy a small site only after its exact CDN release is public. */
import {readFile, mkdir, mkdtemp, cp} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import {join} from 'node:path';
import {createHash} from 'node:crypto';
import {buildCdn} from './build-cdn.mjs';
const root=fileURLToPath(new URL('../',import.meta.url));
const pin=JSON.parse(await readFile(join(root,'deploy/cdn-release.json'),'utf8'));
await mkdir(join(root,'.context'),{recursive:true});
const temp=await mkdtemp(join(root,'.context/vercel-cdn-'));
const release=await buildCdn({assetBase:pin.assetBase,out:join(temp,'bundle')});
if(release.version!==pin.version) throw Error(`Unpublished CDN release ${release.version}. Upload and verify its immutable assets, then update deploy/cdn-release.json.`);
const remote=Object.entries(release.sha256).filter(([url])=>url.startsWith(release.remoteBase));
const types={js:'javascript',wasm:'application/wasm',css:'text/css',json:'application/json',png:'image/png',svg:'image/svg+xml'};
for(let offset=0;offset<remote.length;offset+=4){
  await Promise.all(remote.slice(offset,offset+4).map(async([url,hash])=>{
    const response=await fetch(url,{headers:{Origin:'https://2007.mobile'},signal:AbortSignal.timeout(30000)});
    if(!response.ok) throw Error(`CDN ${response.status}: ${url}`);
    if(response.headers.get('access-control-allow-origin')!=='*') throw Error(`CDN CORS missing: ${url}`);
    if(response.headers.get('cross-origin-resource-policy')!=='cross-origin') throw Error(`CDN CORP missing: ${url}`);
    if(!/max-age=31536000/.test(response.headers.get('cache-control')||'')||!/(?:^|[, ])immutable(?:$|[, ])/.test(response.headers.get('cache-control')||'')) throw Error(`CDN immutable caching missing: ${url}`);
    if(!(response.headers.get('content-type')||'').includes(types[url.split('.').pop()])) throw Error(`CDN content type mismatch: ${url}`);
    const actual=createHash('sha256').update(Buffer.from(await response.arrayBuffer())).digest('hex');
    if(actual!==hash) throw Error(`CDN hash mismatch: ${url}`);
  }));
}
await cp(join(temp,'bundle/site'),join(root,'web/.cdn-site'),{recursive:true,errorOnExist:true,force:false});
console.log(`Verified ${remote.length} immutable CDN assets for ${release.version}; Vercel will serve only the small site shell.`);

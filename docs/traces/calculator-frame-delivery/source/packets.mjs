import {mkdir,writeFile}from'node:fs/promises';import {join}from'node:path';import {createHash}from'node:crypto';import {gzipSync}from'node:zlib';
export const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
export function decode(packet){if(typeof packet.base64!=='string'||!Number.isInteger(packet.byteLength)||![6400,112640].includes(packet.byteLength))throw Error('Crop packet shape');const b=Buffer.from(packet.base64,'base64');if(b.length!==packet.byteLength||b.toString('base64')!==packet.base64||sha(b)!==packet.sha256)throw Error('Crop packet integrity');return b;}
export async function persistCrops(root,object,prefix){
 await mkdir(join(root,'crops'),{recursive:true});
 async function visit(value,path){
  if(value&&typeof value==='object'&&'base64'in value){const bytes=decode(value),file='crops/'+prefix+'-'+path.join('-')+'.rgba.gz';if(!/^[a-zA-Z0-9/_.-]+$/.test(file))throw Error('Crop path');await writeFile(join(root,file),gzipSync(bytes));return {file,byteLength:bytes.length,sha256:sha(bytes)};}
  if(Array.isArray(value))return Promise.all(value.map((v,i)=>visit(v,[...path,String(i)])));
  if(value&&typeof value==='object')return Object.fromEntries(await Promise.all(Object.entries(value).map(async([k,v])=>[k,await visit(v,[...path,k])])));
  return value;
 }
 return visit(object,[]);
}

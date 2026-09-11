import {decodeInstruction} from './decode.mjs';

export const UNIT_LIMITS=Object.freeze({blocks:32,instructions:512,granules:8});
const u32=n=>Number.isInteger(n)&&n>=0&&n<=0xffffffff;
const freeze=value=>{if(value&&typeof value==='object'){for(const v of Object.values(value))freeze(v);Object.freeze(value);}return value;};
const sequential=node=>['alu','load','store'].includes(node.op);

function partition(nodes,entryPc){
 const incoming=new Map([...nodes.keys()].map(pc=>[pc,[]]));
 for(const node of nodes.values())for(const target of new Set(node.successors))if(incoming.has(target))incoming.get(target).push(node);
 const heads=new Set([entryPc]);
 for(const [pc,pred] of incoming)if(pred.length!==1||!sequential(pred[0])||pred[0].successors[0]!==pc)heads.add(pc);
 const seen=new Set(),blocks=[];
 function take(head){
  if(seen.has(head)||!nodes.has(head))return;
  const instructions=[];let pc=head;
  while(nodes.has(pc)&&!seen.has(pc)){
   const node=nodes.get(pc);seen.add(pc);instructions.push(node);
   if(!sequential(node)||node.successors.length!==1||heads.has(node.successors[0]))break;
   pc=node.successors[0];
  }
  blocks.push({head,instructions,successors:[...new Set(instructions.at(-1).successors)]});
 }
 for(const pc of heads)take(pc);
 // A closed straight-line address-wrap cycle has no natural head; entry still
 // anchors reachable cycles, but retain a deterministic defensive completion.
 for(const pc of nodes.keys())take(pc);
 return blocks;
}

/** Build an immutable connected IR unit from captured, permission-checked code.
 * The caller must capture mapping/context and physical versions synchronously.
 * Input regions are 1 KiB virtual windows. Aliases share physical dependencies.
 * This does not authorize execution: runtime must revalidate and retain literals.
 */
export function buildUnit({entryPc,thumb=false,regions}){
 if(!u32(entryPc)||entryPc%(thumb?2:4))throw Error('Invalid unit entry');
 if(typeof thumb!=='boolean'||!Array.isArray(regions))throw Error('Invalid capture');
 const captures=new Map(),physical=new Map();
 for(const region of regions){
  const {virtualBase,physicalBase,version,bytes}=region;
  if(!u32(virtualBase)||virtualBase%1024||!u32(physicalBase)||physicalBase%1024||!u32(version)||!version||!(bytes instanceof Uint8Array)||bytes.length!==1024)throw Error('Invalid code capture');
  if(captures.has(virtualBase))throw Error('Duplicate virtual capture');
  const copy=Uint8Array.from(bytes),other=physical.get(physicalBase);
  if(other&&(other.version!==version||other.bytes.some((b,i)=>b!==copy[i])))throw Error('Inconsistent physical aliases');
  const captured={virtualBase,physicalBase,version,bytes:copy};captures.set(virtualBase,captured);physical.set(physicalBase,captured);
 }
 const nodes=new Map(),dependencies=new Map(),pending=[entryPc],visited=new Set(),refusals=[];
 while(pending.length){
  const pc=pending.shift();if(visited.has(pc))continue;visited.add(pc);
  const capture=captures.get((pc&~1023)>>>0);
  let reason;
  if(!capture)reason='uncaptured';
  else if(nodes.size===UNIT_LIMITS.instructions)reason='instruction-limit';
  else if(!dependencies.has(capture.physicalBase)&&dependencies.size===UNIT_LIMITS.granules)reason='granule-limit';
  if(reason){refusals.push({pc,reason});continue;}
  const offset=pc&1023,view=new DataView(capture.bytes.buffer),raw=thumb?view.getUint16(offset,true):view.getUint32(offset,true);
  const node={...decodeInstruction(raw,pc,thumb),physicalAddress:(capture.physicalBase+offset)>>>0};
  nodes.set(pc,node);
  if(partition(nodes,entryPc).length>UNIT_LIMITS.blocks){nodes.delete(pc);refusals.push({pc,reason:'block-limit'});continue;}
  dependencies.set(capture.physicalBase,{physicalBase:capture.physicalBase,version:capture.version});
  pending.push(...node.successors);
 }
 const blocks=partition(nodes,entryPc),admitted=new Set(nodes.keys());
 for(const block of blocks){
  block.edges=block.successors.map(pc=>({pc,internal:admitted.has(pc)}));
  const tail=block.instructions.at(-1);
  block.exits=tail.dynamic?[{kind:'dynamic',condition:tail.condition,register:tail.rm,exchange:true}]:tail.op==='fallback'?[{kind:'before-instruction',pc:tail.pc,reason:tail.reason}]:[];
 }
 return freeze({version:1,entryPc,thumb,limits:{...UNIT_LIMITS},instructionCount:nodes.size,blocks,dependencies:[...dependencies.values()],refusals});
}

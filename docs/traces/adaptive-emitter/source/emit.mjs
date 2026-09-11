// ABI2 executable integer slice. Guard is pure and must validate the actual
// instruction's executable mapping/context/literal before returning exactly 1.
const c=n=>`(i32.const ${n|0})`,get=n=>`(local.get $${n})`;
const op=(name,...args)=>`(${name} ${args.join(' ')})`;
const set=(name,value)=>`(local.set $${name} ${value})`;
const and=(a,b)=>op('i32.and',a,b),xor=(a,b)=>op('i32.xor',a,b);
const or=(...xs)=>xs.reduce((a,b)=>op('i32.or',a,b));
const shr=(a,n)=>op('i32.shr_u',a,c(n)),shl=(a,n)=>op('i32.shl',a,c(n));
const reg=n=>get(`r${n}`),carry=()=>and(shr(get('f'),29),c(1));
const leave=reason=>set('reason',c(reason))+'(br $done)';
function condition(cond){
 const bit=n=>and(shr(get('f'),n),c(1));
 const z=bit(30),eq=op('i32.eq',bit(31),bit(28));
 const base=[z,bit(29),bit(31),bit(28),and(bit(29),op('i32.eqz',z)),eq,and(eq,op('i32.eqz',z)),c(1)][cond>>1];
 return cond&1?op('i32.eqz',base):base;
}
function flags(logical,cy){
 let value=or(and(get('f'),c(logical?0x1fffffff:0x0fffffff)),and(get('value'),c(0x80000000)),shl(op('i32.eqz',get('value')),30),shl(cy,29));
 if(!logical)value=or(value,and(shr(and(xor(xor(get('a'),get('b')),c(-1)),xor(get('a'),get('value'))),3),c(0x10000000)));
 return set('f',value);
}
function alu(node){
 const k=node.opcode,write=!(k>=8&&k<=11),save=node.setFlags||!write;
 const a=node.rn===15?c(node.pc+(node.thumb?4:8)):reg(node.rn),b=c(node.operand.value);
 let code='';
 if([2,3,4,5,6,7,10,11].includes(k)){
  const reverse=k===3||k===7,sub=[2,3,6,7,10].includes(k);
  code+=set('a',reverse?b:a)+set('b',sub?xor(reverse?a:b,c(-1)):b);
  const cin=k===5||k===6||k===7?carry():c(sub?1:0);
  code+=set('wide',op('i64.add',op('i64.add',op('i64.extend_i32_u',get('a')),op('i64.extend_i32_u',get('b'))),op('i64.extend_i32_u',cin)));
  code+=set('value',op('i32.wrap_i64',get('wide')));
  if(save)code+=flags(false,op('i32.wrap_i64',op('i64.shr_u',get('wide'),'(i64.const 32)')));
 }else{
  const value=k===0||k===8?and(a,b):k===1||k===9?xor(a,b):k===12?or(a,b):k===13?b:k===14?and(a,xor(b,c(-1))):xor(b,c(-1));
  code+=set('value',value);
  if(save)code+=flags(true,node.operand.carry==='preserve'?carry():c(node.operand.carry));
 }
 if(write)code+=set(`r${node.rd}`,get('value'));
 return code;
}
function selection(nodes,lo=0,hi=nodes.length){
 if(lo===hi)return c(-1);
 const mid=(lo+hi)>>1,pc=c(nodes[mid].pc);
 return `(if (result i32) (i32.eq ${reg(15)} ${pc}) (then ${c(mid)}) (else (if (result i32) (i32.lt_u ${reg(15)} ${pc}) (then ${selection(nodes,lo,mid)}) (else ${selection(nodes,mid+1,hi)}))))`;
}

/** Pure emitter: owns no CPU, unit or memory; output uses env.memory and env.guard.
 * Result offsets: retired/reason/status/resume_pc/instruction_pc/dependency/detail/reserved.
 */
export function emitUnit(unit,{registers,cpsr}){
 for(const offset of [registers,cpsr])if(!Number.isInteger(offset)||offset<0||offset>0xffffffff-64||offset%4)throw Error('Invalid ABI2 CPU offset');
 if(unit?.version!==1||typeof unit.thumb!=='boolean'||!Array.isArray(unit.blocks))throw Error('Invalid ABI2 unit');
 const nodes=unit.blocks.flatMap(b=>b.instructions).sort((a,b)=>a.pc-b.pc);
 if(nodes.length!==unit.instructionCount||nodes.length>512||unit.blocks.length>32||unit.dependencies.length>8||new Set(nodes.map(n=>n.pc)).size!==nodes.length)throw Error('Invalid ABI2 unit bounds');
 const deps=new Map(unit.dependencies.map((d,i)=>[d.physicalBase,i]));
 for(const n of nodes){
  if(n.thumb!==unit.thumb||!['alu','branch','branch-exchange','fallback'].includes(n.op)||n.width!==(unit.thumb?2:4))throw Error('Invalid ABI2 instruction');
  if(n.op==='alu'&&(!Number.isInteger(n.opcode)||n.opcode<0||n.opcode>15||n.rd<0||n.rd>=15||n.rn<0||n.rn>15||n.operand?.kind!=='immediate'))throw Error('Unsupported ABI2 ALU');
 }
 let body=Array.from({length:16},(_,i)=>set(`r${i}`,`(i32.load offset=${registers+4*i} ${get('cpu')})`)).join('');
 body+=set('f',`(i32.load offset=${cpsr} ${get('cpu')})`)+set('instruction',reg(15))+set('dependency',c(-1));
 body+='(block $done (loop $dispatch ';
 body+=`(if (i32.ge_u ${get('n')} ${get('budget')}) (then ${leave(1)}))`;
 body+=`(if (i32.ne (i32.and ${get('f')} ${c(32)}) ${c(unit.thumb?32:0)}) (then ${leave(5)}))`;
 body+='(block $outside ';
 for(let i=nodes.length-1;i>=0;i--)body+=`(block $i${i} `;
 body+=`(br_table ${nodes.map((_,i)=>`$i${i}`).join(' ')} $outside ${selection(nodes)})`;
 for(let i=0;i<nodes.length;i++){
  const node=nodes[i],next=c(node.pc+node.width),dependency=deps.get((node.physicalAddress&~1023)>>>0);
  if(dependency===undefined)throw Error('Instruction has no physical dependency');
  body+=')';
  body+=set('instruction',c(node.pc))+set('dependency',c(dependency));
  body+=`(if (i32.ge_u ${get('n')} ${get('budget')}) (then ${leave(1)}))`;
  body+=`(if (i32.ne (call $guard ${c(node.pc)} ${c(node.raw)} ${c(node.width)}) ${c(1)}) (then ${leave(4)}))`;
  if(node.op==='fallback'){body+=leave(3);continue;}
  let effect='';
  if(node.op==='alu')effect=alu(node)+set('r15',next);
  else if(node.op==='branch')effect=(node.link?set('r14',c(node.returnAddress)):'')+set('r15',c(node.target));
  else {
   effect=set('value',node.rm===15?c(node.pc+(node.thumb?4:8)):reg(node.rm));
   effect+=`(if (i32.eq (i32.and ${get('value')} ${c(3)}) ${c(2)}) (then ${leave(3)}))`;
   effect+=set('f',or(and(get('f'),c(~32)),shl(and(get('value'),c(1)),5)))+set('r15',and(get('value'),c(~1)));
  }
  body+=node.condition===14?effect:`(if ${condition(node.condition)} (then ${effect}) (else ${set('r15',next)}))`;
  body+=set('n',op('i32.add',get('n'),c(1)));
  if(node.op==='branch-exchange')body+=`(if (i32.ne (i32.and ${get('f')} ${c(32)}) ${c(unit.thumb?32:0)}) (then ${leave(6)}))`;
  // Physical layout is not execution order. Only an actual sequential ALU edge
  // can enter the next emitted case without redispatching its current PC.
  if(node.op!=='alu'||nodes[i+1]?.pc!==((node.pc+node.width)>>>0))body+='(br $dispatch)';
 }
 body+=')'+set('instruction',reg(15))+set('dependency',c(-1))+set('reason',`(if (result i32) (i32.eqz ${get('n')}) (then ${c(5)}) (else ${c(2)}))`)+'(br $done)))';
 body+=Array.from({length:16},(_,i)=>`(i32.store offset=${registers+4*i} ${get('cpu')} ${reg(i)})`).join('');
 body+=`(i32.store offset=${cpsr} ${get('cpu')} ${get('f')})`;
 const result=[get('n'),get('reason'),c(0),reg(15),get('instruction'),get('dependency'),c(0),c(0)];
 body+=result.map((value,i)=>`(i32.store offset=${i*4} ${get('result')} ${value})`).join('');
 const locals=[...Array.from({length:16},(_,i)=>`r${i}`),'f','n','reason','instruction','dependency','a','b','value'].map(n=>`(local $${n} i32)`).join('');
 return `(module (import "env" "memory" (memory 1)) (import "env" "guard" (func $guard (param i32 i32 i32) (result i32))) (func (export "run") (param $cpu i32) (param $budget i32) (param $result i32) ${locals}(local $wide i64) ${body}))`;
}

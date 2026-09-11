// ABI2 executable integer slice. Guard validates mapping/context/literal and
// may reproduce reference fetch-cache accounting. No instruction or MMIO
// effects and no architectural-register changes may precede returning 1.
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
function shiftedOperand(node){
 const {rm,shift}=node.operand;
 const value=rm===15?c(node.pc+(node.thumb?4:8)):reg(rm),v=get('shift_value');
 let code=set('shift_value',value)+set('shift_carry',carry());
 const cy=x=>set('shift_carry',x),sv=x=>set('shift_value',x);
 if(shift.kind==='immediate'){
  const n=shift.amount;
  if(shift.type===0){if(n)code+=cy(and(shr(v,32-n),c(1)))+sv(shl(v,n));}
  else if(shift.type===1)code+=cy(and(shr(v,n?n-1:31),c(1)))+sv(n?shr(v,n):c(0));
  else if(shift.type===2)code+=cy(and(shr(v,n?n-1:31),c(1)))+sv(op('i32.shr_s',v,c(n||31)));
  else if(n)code+=sv(op('i32.rotr',v,c(n)))+cy(shr(v,31));
  else code+=cy(and(v,c(1)))+sv(or(shr(v,1),shl(carry(),31)));
  return code;
 }
 code+=set('shift_amount',and(reg(shift.rs),c(255)));
 const n=get('shift_amount');
 let nonzero;
 if(shift.type===0){
  const small=cy(and(op('i32.shr_u',v,op('i32.sub',c(32),n)),c(1)))+sv(op('i32.shl',v,n));
  const large=cy(`(if (result i32) (i32.eq ${n} ${c(32)}) (then ${and(v,c(1))}) (else ${c(0)}))`)+sv(c(0));
  nonzero=`(if (i32.lt_u ${n} ${c(32)}) (then ${small}) (else ${large}))`;
 }else if(shift.type===1){
  const small=cy(and(op('i32.shr_u',v,op('i32.sub',n,c(1))),c(1)))+sv(op('i32.shr_u',v,n));
  const large=cy(`(if (result i32) (i32.eq ${n} ${c(32)}) (then ${shr(v,31)}) (else ${c(0)}))`)+sv(c(0));
  nonzero=`(if (i32.lt_u ${n} ${c(32)}) (then ${small}) (else ${large}))`;
 }else if(shift.type===2){
  const small=cy(and(op('i32.shr_u',v,op('i32.sub',n,c(1))),c(1)))+sv(op('i32.shr_s',v,n));
  const large=cy(shr(v,31))+sv(op('i32.shr_s',v,c(31)));
  nonzero=`(if (i32.lt_u ${n} ${c(32)}) (then ${small}) (else ${large}))`;
 }else nonzero=sv(op('i32.rotr',v,n))+cy(shr(v,31));
 // WASM masks shift counts modulo32. The explicit zero/32/>32 branches
 // above retain ARM's distinct results; ROR alone intentionally uses modulo32.
 return code+`(if (i32.ne ${n} ${c(0)}) (then ${nonzero}))`;
}
function alu(node){
 const k=node.opcode,write=!(k>=8&&k<=11),save=node.setFlags||!write;
 const shifted=node.operand.kind==='register';
 const a=node.rn===15?c(node.pc+(node.thumb?4:8)):reg(node.rn),b=shifted?get('shift_value'):c(node.operand.value);
 let code=shifted?shiftedOperand(node):'';
 if(k==='mul'){
  code+=set('value',op('i32.mul',a,b));
  // The current Thumb MUL changes N/Z only. Re-emitting the original carry
  // through the logical flag helper preserves both C and V bit-for-bit.
  if(save)code+=flags(true,carry());
 }else if([2,3,4,5,6,7,10,11].includes(k)){
  const reverse=k===3||k===7,sub=[2,3,6,7,10].includes(k);
  code+=set('a',reverse?b:a)+set('b',sub?xor(reverse?a:b,c(-1)):b);
  const cin=k===5||k===6||k===7?carry():c(sub?1:0);
  code+=set('wide',op('i64.add',op('i64.add',op('i64.extend_i32_u',get('a')),op('i64.extend_i32_u',get('b'))),op('i64.extend_i32_u',cin)));
  code+=set('value',op('i32.wrap_i64',get('wide')));
  if(save)code+=flags(false,op('i32.wrap_i64',op('i64.shr_u',get('wide'),'(i64.const 32)')));
 }else{
  const value=k===0||k===8?and(a,b):k===1||k===9?xor(a,b):k===12?or(a,b):k===13?b:k===14?and(a,xor(b,c(-1))):xor(b,c(-1));
  code+=set('value',value);
  if(save)code+=flags(true,shifted?get('shift_carry'):node.operand.carry==='preserve'?carry():c(node.operand.carry));
 }
 if(write)code+=set(`r${node.rd}`,get('value'));
 return code;
}
function selection(nodes,lo=0,hi=nodes.length){
 if(lo===hi)return c(-1);
 const mid=(lo+hi)>>1,pc=c(nodes[mid].pc);
 return `(if (result i32) (i32.eq ${reg(15)} ${pc}) (then ${c(mid)}) (else (if (result i32) (i32.lt_u ${reg(15)} ${pc}) (then ${selection(nodes,lo,mid)}) (else ${selection(nodes,mid+1,hi)}))))`;
}

function memoryAccess(node){
 const base=node.rn===15?c(node.literal?((node.pc+4)&~3):node.pc+8):reg(node.rn);
 const priv=node.unprivileged?c(0):op('i32.ne',and(get('f'),c(31)),c(16));
 // Address/source are captured before destination/writeback. A refusing helper
 // leaves all architectural locals untouched; write itself owns RAM commitment.
 let code=set('a',base)+set('b',op(node.add?'i32.add':'i32.sub',get('a'),c(node.offset)));
 const address=node.preIndex?get('b'):get('a');
 if(node.op==='load'){
  code+=set('pointer',`(call $read ${address} ${c(node.bytes)} ${priv})`);
  code+=`(if (i32.eqz ${get('pointer')}) (then ${leave(3)}))`;
  code+=set(`r${node.rd}`,op(node.bytes===1?'i32.load8_u':'i32.load',get('pointer')));
 }else{
  const source=node.rd===15?c(node.pc+12):reg(node.rd);
  code+=set('write_status',`(call $write ${address} ${c(node.bytes)} ${priv} ${source})`);
  code+=`(if (i32.eqz ${get('write_status')}) (then ${leave(3)}))`;
  // Unknown results may already have committed memory: fatal, never fallback.
  code+=`(if (i32.gt_u ${get('write_status')} ${c(2)}) (then ${leave(0)}))`;
 }
 if(node.writeback)code+=set(`r${node.rn}`,get('b'));
 return code;
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
  if(n.thumb!==unit.thumb||!['alu','load','store','branch','branch-exchange','fallback'].includes(n.op)||n.width!==(unit.thumb?2:4))throw Error('Invalid ABI2 instruction');
  if(['load','store'].includes(n.op)&&(!Number.isInteger(n.rd)||n.rd<0||n.rd>15||(n.rd===15&&(n.op==='load'||n.bytes!==4))||!Number.isInteger(n.rn)||n.rn<0||n.rn>15||![1,4].includes(n.bytes)||!Number.isInteger(n.offset)||n.offset<0||n.offset>4095||['add','preIndex','writeback','unprivileged','literal'].some(k=>typeof n[k]!=='boolean')||(n.writeback&&(n.rn===15||n.rn===n.rd))))throw Error('Invalid ABI2 memory operation');
  if(n.op==='alu'){
   const comparison=n.opcode>=8&&n.opcode<=11;
   if(!(n.thumb&&n.opcode==='mul')&&(!Number.isInteger(n.opcode)||n.opcode<0||n.opcode>15))throw Error('Unsupported ABI2 ALU opcode');
   if(n.rd<0||n.rd>15||(n.rd===15&&!comparison)||n.rn<0||n.rn>15||!['immediate','register'].includes(n.operand?.kind))throw Error('Unsupported ABI2 ALU');
   if(n.operand.kind==='register'){
    const {rm,shift}=n.operand;
    if(!Number.isInteger(rm)||rm<0||rm>15||!shift||!Number.isInteger(shift.type)||shift.type<0||shift.type>3)throw Error('Invalid ABI2 register operand');
    if(shift.kind==='immediate'){
     if(!Number.isInteger(shift.amount)||shift.amount<0||shift.amount>31)throw Error('Invalid ABI2 immediate shift');
    }else if(shift.kind!=='register'||!Number.isInteger(shift.rs)||shift.rs<0||shift.rs>=15||rm===15||n.rn===15)throw Error('Invalid ABI2 register shift');
   }
  }
 }
 let body=Array.from({length:16},(_,i)=>set(`r${i}`,`(i32.load offset=${registers+4*i} ${get('cpu')})`)).join('');
 body+=set('f',`(i32.load offset=${cpsr} ${get('cpu')})`)+set('instruction',reg(15))+set('dependency',c(-1));
 body+='(block $done (loop $dispatch ';
 body+=`(if (i32.ge_u ${get('n')} ${get('budget')}) (then ${leave(1)}))`;
 body+=`(if (i32.ne (i32.and ${get('f')} ${c(32)}) ${c(unit.thumb?32:0)}) (then ${leave(5)}))`;
 body+='(block $outside ';
 // Bound structured nesting independently from the 512-instruction unit cap.
 // A label for every instruction at once can exhaust the WAT parser stack.
 const groups=Math.ceil(nodes.length/64);
 body+=set('selected',selection(nodes));
 for(let g=groups-1;g>=0;g--)body+=`(block $g${g} `;
 body+=`(br_table ${Array.from({length:groups},(_,g)=>`$g${g}`).join(' ')} $outside ${shr(get('selected'),6)})`;
 for(let g=0;g<groups;g++){
  body+=')';
  const lo=g*64,hi=Math.min(nodes.length,lo+64);
  for(let i=hi-1;i>=lo;i--)body+=`(block $i${i} `;
  body+=`(br_table ${nodes.slice(lo,hi).map((_,j)=>`$i${lo+j}`).join(' ')} $outside (i32.sub ${get('selected')} ${c(lo)}))`;
 for(let i=lo;i<hi;i++){
  const node=nodes[i],next=c(node.pc+node.width),dependency=deps.get((node.physicalAddress&~1023)>>>0);
  if(dependency===undefined)throw Error('Instruction has no physical dependency');
  body+=')';
  body+=set('instruction',c(node.pc))+set('dependency',c(dependency));
  body+=`(if (i32.ge_u ${get('n')} ${get('budget')}) (then ${leave(1)}))`;
  body+=`(if (i32.ne (call $guard ${c(node.pc)} ${c(node.raw)} ${c(node.width)}) ${c(1)}) (then ${leave(4)}))`;
  if(node.op==='fallback'){body+=leave(3);continue;}
  let effect='';
  if(node.op==='alu')effect=alu(node)+set('r15',next);
  else if(['load','store'].includes(node.op))effect=memoryAccess(node)+set('r15',next);
  else if(node.op==='branch')effect=(node.link?set('r14',c(node.returnAddress)):'')+set('r15',c(node.target));
  else {
   effect=set('value',node.rm===15?c(node.pc+(node.thumb?4:8)):reg(node.rm));
   effect+=`(if (i32.eq (i32.and ${get('value')} ${c(3)}) ${c(2)}) (then ${leave(3)}))`;
   effect+=set('f',or(and(get('f'),c(~32)),shl(and(get('value'),c(1)),5)))+set('r15',and(get('value'),c(~1)));
  }
  if(node.op==='store')body+=set('write_status',c(0));
  body+=node.condition===14?effect:`(if ${condition(node.condition)} (then ${effect}) (else ${set('r15',next)}))`;
  body+=set('n',op('i32.add',get('n'),c(1)));
  if(node.op==='store')body+=`(if (i32.eq ${get('write_status')} ${c(2)}) (then ${leave(6)}))`;
  if(node.op==='branch-exchange')body+=`(if (i32.ne (i32.and ${get('f')} ${c(32)}) ${c(unit.thumb?32:0)}) (then ${leave(6)}))`;
  // Physical layout is not execution order. Only an actual sequential edge
  // can enter the next emitted case without redispatching its current PC.
  if(!['alu','load','store'].includes(node.op)||i+1===hi||nodes[i+1]?.pc!==((node.pc+node.width)>>>0))body+='(br $dispatch)';
 }
 }
 body+=')'+set('instruction',reg(15))+set('dependency',c(-1))+set('reason',`(if (result i32) (i32.eqz ${get('n')}) (then ${c(5)}) (else ${c(2)}))`)+'(br $done)))';
 body+=Array.from({length:16},(_,i)=>`(i32.store offset=${registers+4*i} ${get('cpu')} ${reg(i)})`).join('');
 body+=`(i32.store offset=${cpsr} ${get('cpu')} ${get('f')})`;
 const result=[get('n'),get('reason'),c(0),reg(15),get('instruction'),get('dependency'),c(0),c(0)];
 body+=result.map((value,i)=>`(i32.store offset=${i*4} ${get('result')} ${value})`).join('');
 const reads=nodes.some(n=>n.op==='load'),writes=nodes.some(n=>n.op==='store');
 const locals=[...Array.from({length:16},(_,i)=>`r${i}`),'f','n','reason','instruction','dependency','a','b','value','selected','shift_value','shift_carry','shift_amount',...(reads?['pointer']:[]),...(writes?['write_status']:[])].map(n=>`(local $${n} i32)`).join('');
 const readImport=reads?'(import "env" "read" (func $read (param i32 i32 i32) (result i32)))':'';
 const writeImport=writes?'(import "env" "write" (func $write (param i32 i32 i32 i32) (result i32)))':'';
 return `(module (import "env" "memory" (memory 1)) (import "env" "guard" (func $guard (param i32 i32 i32) (result i32))) ${readImport}${writeImport}(func (export "run") (param $cpu i32) (param $budget i32) (param $result i32) ${locals}(local $wide i64) ${body}))`;
}

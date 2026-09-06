/* ARMv6/Thumb guarded1KiB region emitter. See README.md for regeneration. */
const C=n=>`(i32.const ${n|0})`;
const L=n=>`(local.get $${n})`;
const K=n=>typeof n==='string'?L(n):C(n);
const op=(name,...args)=>`(${name} ${args.join(' ')})`;
const add=(a,b)=>op('i32.add',a,b),and=(a,b)=>op('i32.and',a,b),xor=(a,b)=>op('i32.xor',a,b);
const shl=(a,n)=>op('i32.shl',a,C(n)),shr=(a,n)=>op('i32.shr_u',a,C(n));
let R,F,FETCH,OFF,DIAG;
const reg=n=>L("r"+n);
const put=(n,x)=>`(local.set $r${n} ${x})`;
const set=(n,x)=>`(local.set $${n} ${x})`;
const carry=()=>and(shr(L('f'),29),C(1));
function flags(logical,cy=carry()) {
  const bits=[and(L('r'),C(0x80000000)),shl(op('i32.eqz',L('r')),30),shl(cy,29)];
  if(!logical)bits.push(and(shr(and(xor(xor(L('a'),L('b')),C(-1)),xor(L('a'),L('r'))),3),C(0x10000000)));
  let v=and(L('f'),C(logical?0x1fffffff:0x0fffffff));for(const b of bits)v=op('i32.or',v,b);
  return set('f',v)+`(i32.store offset=${F} ${L('c')} ${L('f')})`;
}
function arith(a,b,cin,saveFlags){
 let s=set('a',a)+set('b',b);
 s+=set('wide',op('i64.add',op('i64.add',op('i64.extend_i32_u',L('a')),op('i64.extend_i32_u',L('b'))),op('i64.extend_i32_u',cin)));
 s+=set('r',op('i32.wrap_i64',L('wide')));
 if(saveFlags)s+=flags(false,op('i32.wrap_i64',op('i64.shr_u',L('wide'),'(i64.const 32)')));
 return s;
}
function alu(code,a,b,S,rd,cy=carry()){
 let s,write=!(code>=8&&code<=11);
 if(code===2||code===6||code===10)s=arith(a,xor(b,C(-1)),code===6?carry():C(1),S||code===10);
 else if(code===3||code===7)s=arith(b,xor(a,C(-1)),code===7?carry():C(1),S);
 else if(code===4||code===5||code===11)s=arith(a,b,code===5?carry():C(0),S||code===11);
 else {
   let x=code===0||code===8?and(a,b):code===1||code===9?xor(a,b):code===12?op('i32.or',a,b):code===13?b:code===14?and(a,xor(b,C(-1))):xor(b,C(-1));
   s=set('r',x);if(S||!write)s+=flags(true,cy);
 }
 if(write)s+=put(rd,L('r'));return s;
}
function condition(code){
 const bit=n=>and(shr(L('f'),n),C(1));
 const N=bit(31),Z=bit(30),CY=bit(29),V=bit(28),eq=op('i32.eq',N,V);
 const base=[Z,CY,N,V,and(CY,op('i32.eqz',Z)),eq,and(eq,op('i32.eqz',Z)),C(1)][code>>1];
 return code&1?op('i32.eqz',base):base;
}
function span(addr,bytes,load,transactions,n){
 let fail=reason=>`(local.set $why ${C(reason)})(br $done)`;
 let s=set('addr',addr);
 s+=`(if(i32.or(i32.ne(i32.and ${L('addr')}${C(bytes>=4?3:bytes===2?1:0)})${C(0)})(i32.gt_u(i32.add(i32.and ${L('addr')}${C(1023)})${C(bytes)})${C(1024)}))(then ${fail(6)}))`;
 const priv=op('i32.ne',and(L('f'),C(31)),C(16)),slot=and(add(shr(L('addr'),10),op('i32.mul',priv,C(32))),C(63));
 s+=set('entry',add(add(L('c'),C(OFF[load?4:5])),op('i32.mul',slot,C(OFF[11]))));
 s+=set('p',`(i32.load ${L('entry')})`);
 s+=`(if(i32.or(i32.eqz ${L('p')})(i32.or(i32.ne(i32.load offset=4 ${L('entry')})(i32.or(i32.and ${L('addr')}${C(~1023)})${priv}))(i32.ne(i32.load offset=8 ${L('entry')})(i32.load offset=${OFF[6]} ${L('c')}))))(then ${fail(6)}))`;
 if(!load)s+=`(if(i32.eqz(i32.load offset=${OFF[10]}(i32.load offset=${OFF[9]} ${L('c')})))(then ${fail(6)}))`;
 s+=set('p',add(L('p'),and(L('addr'),C(1023))));
 if(!load)s+=`(if(i32.and(i32.lt_u ${L('p')}(i32.add ${L('fetch')}${C(1024)}))(i32.gt_u(i32.add ${L('p')}${C(bytes)})${L('fetch')}))(then ${fail(7)}))`;
 s+=`(i64.store offset=${OFF[load?7:8]} ${L('c')}(i64.add(i64.load offset=${OFF[load?7:8]} ${L('c')})(i64.const ${transactions})))`;
 return s;
}

function transfer(addr,bytes,load,rd,n){
 let s=span(addr,bytes,load,1,n);
 const suffix=bytes===4?'':bytes===2?'16_u':'8_u';
 return s+(load?put(rd,`(i32.load${suffix} ${L('p')})`):`(i32.store${bytes===4?'':bytes===2?'16':'8'} ${L('p')} ${reg(rd)})`);
}
function emitBase(insn,pc,thumb,n){
 let body='',stop=false,next=C(pc+(thumb?2:4));
 const rr=r=>r===15?C(pc+(thumb?4:8)):reg(r);
 if(!thumb){
   const cond=insn>>>28;if(cond===15)return null;
   const rn=insn>>>16&15,rd=insn>>>12&15,code=insn>>>21&15,S=!!(insn&0x100000);
   if((insn&0x0e000000)===0x0a000000){const off=(insn<<8)>>6;next=C(pc+8+off);if(insn&0x1000000)body+=put(14,C(pc+4));stop=true;}
   else if((insn&0x0ffffff0)===0x012fff10){body=set('r',rr(insn&15))+`(if(i32.eq(i32.and ${L('r')} ${C(3)})${C(2)})(then(return ${K(n)})))`;
     body+=set('f',op('i32.or',and(L('f'),C(~32)),shl(and(L('r'),C(1)),5)))+`(i32.store offset=${F} ${L('c')} ${L('f')})`;next=and(L('r'),C(~1));stop=true;}
   else if((insn&0x0f200000)===0x05000000&&rd!==15){const addr=(insn&0x800000)?add(rr(rn),C(insn&4095)):op('i32.sub',rr(rn),C(insn&4095));body=transfer(addr,(insn&0x400000)?1:4,!!(insn&0x100000),rd,n);}
   else if((insn&0x0e400000)===0x08000000 && rn!==15 && (insn&0xffff)!==0 && !(insn&0x8000) && !((insn&0x200000)&&(insn&(1<<rn)))){
     const regs=Array.from({length:15},(_,i)=>i).filter(i=>insn&(1<<i)),count=regs.length,load=!!(insn&0x100000),U=!!(insn&0x800000),P=!!(insn&0x1000000);
     body=set('a',reg(rn));const addr=add(L('a'),C(U?(P?4:0):(P?-4*count:-4*count+4)));
     body+=span(addr,count*4,load,count,n);
     regs.forEach((r,i)=>body+=load?put(r,`(i32.load offset=${i*4} ${L('p')})`):`(i32.store offset=${i*4} ${L('p')} ${reg(r)})`);
     if(insn&0x200000)body+=put(rn,add(L('a'),C(U?4*count:-4*count)));
   }
   else if((insn&0x0c000000)===0 && (insn&0x01900000)!==0x01000000 && !((insn&0x0e000000)===0&&(insn&0x10)) && rd!==15){
     let b,cy=carry();
     if(insn&0x2000000){let rot=(insn>>>8&15)*2,imm=insn&255;imm=rot?((imm>>>rot)|(imm<<(32-rot))):imm;b=C(imm);if(rot)cy=C(imm>>>31);}
     else {let amount=insn>>>7&31,type=insn>>>5&3,v=rr(insn&15);[b,cy]=shift(v,type,amount);}
     body=alu(code,rr(rn),b,S,rd,cy);
   }else return null;
   if(cond!==14)body=`(if ${condition(cond)}(then ${body}${put(15,next)})(else ${put(15,C(pc+4))}))`;
   else body+=put(15,next);
 }else{
   const group=insn>>>12,rd=insn&7,rs=insn>>>3&7;
   if(insn<0x1800){const type=insn>>>11&3,amount=insn>>>6&31,a=reg(rs);let b,cy=carry();
     if(type===0){b=amount?shl(a,amount):a;if(amount)cy=and(shr(a,32-amount),C(1));}
     else if(type===1){b=amount?shr(a,amount):C(0);cy=and(shr(a,amount?amount-1:31),C(1));}
     else{b=op('i32.shr_s',a,C(amount||31));cy=and(shr(a,amount?amount-1:31),C(1));}
     body=alu(13,C(0),b,true,rd,cy);
   }else if(insn<0x2000){let b=(insn&0x400)?C(insn>>>6&7):reg(insn>>>6&7);body=alu((insn&0x200)?2:4,reg(rs),b,true,rd);}
   else if(insn<0x4000){const target=insn>>>8&7,code=[13,10,4,2][insn>>>11&3];body=alu(code,reg(target),C(insn&255),true,target);}
   else if((insn&0xfc00)===0x4000){const code=insn>>>6&15,mapping={0:0,1:1,5:5,6:6,8:8,10:10,11:11,12:12,14:14,15:15};
     if(code===9)body=alu(2,C(0),reg(rs),true,rd);
     else if(code===13)body=set('r',op('i32.mul',reg(rd),reg(rs)))+set('f',op('i32.or',and(L('f'),C(0x3fffffff)),op('i32.or',and(L('r'),C(0x80000000)),shl(op('i32.eqz',L('r')),30))))+`(i32.store offset=${F} ${L('c')} ${L('f')})`+put(rd,L('r'));
     else if(code in mapping)body=alu(mapping[code],reg(rd),reg(rs),true,rd);else return null;
   }else if((insn&0xfc00)===0x4400){let target=rd|(insn>>>4&8),source=rs|(insn>>>3&8),code=insn>>>8&3;
     if(code===3){return null;}if(target===15)return null;
     body=alu(code===0?4:code===1?10:13,rr(target),rr(source),code===1,target);
   }else if((insn&0xf800)===0x4800){body=transfer(C(((pc+4)&~3)+((insn&255)<<2)),4,true,insn>>>8&7,n);}
   else if(group===6||group===7||group===8||group===9){let bytes=group===8?2:group===7?1:4;let target=group===9?insn>>>8&7:rd,addr=group===9?add(reg(13),C((insn&255)*4)):add(reg(rs),C((insn>>>6&31)*bytes));body=transfer(addr,bytes,!!(insn&0x800),target,n);}
   else if(group===10){body=put(insn>>>8&7,add((insn&0x800)?reg(13):C((pc+4)&~3),C((insn&255)*4)));}
   else if((insn&0xff00)===0xb000){body=put(13,add(reg(13),C((insn&127)*((insn&128)?-4:4))));}
   else if((insn&0xf600)===0xb400){const load=!!(insn&0x800),extra=!!(insn&256);if(load&&extra)return null;
     const regs=Array.from({length:8},(_,i)=>i).filter(i=>insn&(1<<i));if(extra)regs.push(14);if(!regs.length)return null;
     body=set('a',reg(13));body+=span(load?L('a'):add(L('a'),C(-4*regs.length)),4*regs.length,load,regs.length,n);
     regs.forEach((r,i)=>body+=load?put(r,`(i32.load offset=${i*4} ${L('p')})`):`(i32.store offset=${i*4} ${L('p')} ${reg(r)})`);body+=put(13,add(L('a'),C((load?4:-4)*regs.length)));
   }else if(group===13&&(insn>>>8&15)<14){body=`(if ${condition(insn>>>8&15)}(then ${put(15,C(pc+4+((insn<<24)>>23)))})(else ${put(15,C(pc+2))}))`;return {body,stop:true};}
   else if((insn&0xf800)===0xe000){next=C(pc+4+((insn<<21)>>20));stop=true;}
   else if(group===15){if(!(insn&0x800))body=put(14,C(pc+4+((insn<<21)>>9)));else{body=set('a',add(reg(14),C((insn&2047)*2)))+put(14,C((pc+2)|1));next=and(L('a'),C(~1));stop=true;}}
   else return null;
   body+=put(15,next);
 }
 return {body,stop};
}

function shift(v,type,amount){let x,cy=carry();if(type===0){x=amount?shl(v,amount):v;if(amount)cy=and(shr(v,32-amount),C(1));}
else if(type===1){x=amount?shr(v,amount):C(0);cy=and(shr(v,amount?amount-1:31),C(1));}
else if(type===2){x=op('i32.shr_s',v,C(amount||31));cy=and(shr(v,amount?amount-1:31),C(1));}
else if(amount){x=op('i32.rotr',v,C(amount));cy=and(shr(v,amount-1),C(1));}
else{x=op('i32.or',shl(carry(),31),shr(v,1));cy=and(v,C(1));}return[x,cy];}
function emit(insn,pc,thumb,n){let body='',next=C(pc+(thumb?2:4)),stop=false;
 if(!thumb){const cond=insn>>>28,rn=insn>>>16&15,rd=insn>>>12&15,rm=insn&15,rr=r=>r===15?C(pc+8):reg(r);if(cond===15)return emitBase(insn,pc,thumb,n);
  if((insn&0x0f200e00)===0x0d000a00){const D=!!(insn&0x400000),dbl=!!(insn&0x100),load=!!(insn&0x100000),U=!!(insn&0x800000),first=(rd*2)+(dbl?0:+D);if(dbl&&D)return null;
   const get=off=>`(i32.load offset=${off} ${L('c')})`,cp=get(OFF[14]),cp10=and(shr(cp,20),C(3)),cp11=and(shr(cp,22),C(3)),acc=op('select',cp10,cp11,op('i32.lt_u',cp10,cp11));
   const allowed=op('i32.or',op('i32.eq',acc,C(3)),op('i32.and',op('i32.eq',acc,C(1)),op('i32.ne',and(L('f'),C(31)),C(16))));
   body=`(if(i32.or(i32.eqz ${allowed})(i32.or(i32.eqz(i32.and ${get(OFF[13])}${C(1<<30)}))(i32.ne(i32.and ${get(OFF[15])}${C(0xc00000)})${C(0)})))(then(br $done)))`;
   const addr=U?add(rr(rn),C((insn&255)*4)):op('i32.sub',rr(rn),C((insn&255)*4)),offset=OFF[12]+first*4,kind=dbl?'i64':'i32';body+=span(addr,dbl?8:4,load,dbl?2:1,n);body+=load?`(${kind}.store offset=${offset} ${L('c')}(${kind}.load ${L('p')}))`:`(${kind}.store ${L('p')}(${kind}.load offset=${offset} ${L('c')}))`;
  }else if((insn&0x0fc000f0)===0x00000090){const dest=insn>>>16&15,acc=insn>>>12&15,rs=insn>>>8&15,A=!!(insn&0x200000),S=!!(insn&0x100000);if(dest===15||rs===15||rm===15||(A&&acc===15))return null;let v=op('i32.mul',reg(rm),reg(rs));if(A)v=add(v,reg(acc));body=set('r',v)+put(dest,L('r'));if(S)body+=set('f',op('i32.or',and(L('f'),C(0x3fffffff)),op('i32.or',and(L('r'),C(0x80000000)),shl(op('i32.eqz',L('r')),30))));}
  else if((insn&0x0e000090)===0x00000090&&(insn&0x60)!==0){const P=!!(insn&0x1000000),U=!!(insn&0x800000),I=!!(insn&0x400000),W=!!(insn&0x200000),load=!!(insn&0x100000),sh=insn>>>5&3,wb=!P||W;if(rd===15||(!P&&W)||(wb&&(rn===15||rn===rd))||(!I&&((insn&0xf00)||rm===15))||(!load&&sh!==1))return null;const off=I?C(((insn>>>8&15)<<4)|(insn&15)):rr(rm),bytes=sh===2?1:2;body=set('a',U?add(rr(rn),off):op('i32.sub',rr(rn),off))+span(P?L('a'):rr(rn),bytes,load,1,n);body+=load?put(rd,`(i32.load${sh===1?'16_u':sh===2?'8_s':'16_s'} ${L('p')})`):`(i32.store16 ${L('p')}${reg(rd)})`;if(wb)body+=put(rn,L('a'));}
  else if((insn&0x0c000000)===0x04000000&&!(insn&0x02000000)&&rd!==15){
   const I=!!(insn&0x2000000),P=!!(insn&0x1000000),U=!!(insn&0x800000),B=!!(insn&0x400000),W=!!(insn&0x200000),load=!!(insn&0x100000),wb=!P||W;
   if((!P&&W)||(wb&&(rn===15||rn===rd))||(I&&rm===15))return null;
   const off=I?shift(rr(rm),insn>>>5&3,insn>>>7&31)[0]:C(insn&4095);
   body=set('a',U?add(rr(rn),off):op('i32.sub',rr(rn),off));body+=transfer(P?L('a'):rr(rn),B?1:4,load,rd,n);if(wb)body+=put(rn,L('a'));
  }else if((insn&0x0c000000)===0x04000000&&(insn&0x2000000)&&!(insn&16)&&rd!==15){
   const P=!!(insn&0x1000000),U=!!(insn&0x800000),B=!!(insn&0x400000),W=!!(insn&0x200000),load=!!(insn&0x100000),wb=!P||W;
   if((!P&&W)||(wb&&(rn===15||rn===rd))||rm===15)return null;
   const off=shift(rr(rm),insn>>>5&3,insn>>>7&31)[0];body=set('a',U?add(rr(rn),off):op('i32.sub',rr(rn),off))+transfer(P?L('a'):rr(rn),B?1:4,load,rd,n);if(wb)body+=put(rn,L('a'));
  }else if((insn&0x0fff03f0)===0x06af0070&&rd!==15&&rm!==15){body=put(rd,op('i32.extend8_s',op('i32.rotr',reg(rm),C((insn>>>10&3)*8))));}
  else if((insn&0x0fff0fff)===0x010f0000&&rd!==15){body=put(rd,L('f'));}
  else if((insn&0x0fff0fff)===0x0e070f5a){body='';}
  else return emitBase(insn,pc,thumb,n);
  body=cond===14?body+put(15,next):`(if ${condition(cond)}(then ${body}${put(15,next)})(else ${put(15,C(pc+4))}))`;
 }else if((insn&0xf800)===0xe800){body=set('a',add(reg(14),C((insn&2047)*2)))+put(14,C((pc+2)|1))+set('f',and(L('f'),C(~32)))+put(15,and(L('a'),C(~3)));stop=true;}
 else if((insn&0xf000)===0x5000){const code=insn>>>9&7,rd=insn&7,addr=add(reg(insn>>>3&7),reg(insn>>>6&7));let bytes=[4,2,1,1,4,2,1,2][code],load=code>=3;body=span(addr,bytes,load,1,n);if(load)body+=put(rd,`(i32.load${bytes===4?'':bytes===2?(code===7?'16_s':'16_u'):(code===3?'8_s':'8_u')} ${L('p')})`);else body+=`(i32.store${bytes===4?'':bytes===2?'16':'8'} ${L('p')}${reg(rd)})`;body+=put(15,next);}
 else return emitBase(insn,pc,thumb,n);
 return {body,stop};
}
function compileRegion(e){
 const code=new Map();for(const [pc,w] of e.ops){let c=emit(w,pc,e.thumb,'n');if(c){c.body=c.body.replaceAll(`(i32.store offset=${F} ${L('c')} ${L('f')})`,'').replaceAll('(return (local.get $n))','(br $done)');code.set(pc,{...c,pc,w});}}
 const width=e.thumb?2:4,heads=new Set();
 for(const [pc,c]of code){let prev=code.get(pc-width);if(!prev||prev.stop)heads.add(pc);for(const m of c.body.matchAll(/\(local.set \$r15 \(i32.const (-?\d+)\)\)/g)){let p=(+m[1])>>>0;if(p!==pc+width&&code.has(p))heads.add(p);}}
 // Bound straight-line length so timebase budgets can normally fit an entry.
 for(const pc of [...code.keys()].sort((a,b)=>a-b)){let p=pc,k=0;if(!heads.has(pc))continue;while(code.has(p)){if(k++===16){heads.add(p);break;}if(code.get(p).stop)break;p+=width;if(heads.has(p))break;}}
 let blocks=[];for(const head of [...heads].sort((a,b)=>a-b)){let ops=[],pc=head;while(code.has(pc)){ops.push(code.get(pc));if(code.get(pc).stop||ops.length===16)break;pc+=width;if(heads.has(pc))break;}if(ops.length)blocks.push({head,ops});}
 if(!blocks.length)return null;
 const labels=new Map();blocks.forEach((b,i)=>b.ops.forEach(c=>labels.set((c.pc-e.pc)/width,'$case'+i))); let body='';
 for(let n=0;n<16;n++)body+=put(n,`(i32.load offset=${R+n*4} ${L('c')})`);
 body+=set('f',`(i32.load offset=${F} ${L('c')})`)+set('fetch',`(i32.load offset=${FETCH} ${L('c')})`);
 body+=`(block $done(loop $run
 (if(i32.ge_u ${L('n')}${L('budget')})(then(local.set $why(i32.const 1))(br $done)))
 (if(i32.ne(i32.and ${reg(15)}${C(~1023)})${C(e.pc)})(then(local.set $why(i32.const 2))(br $done)))
 (if(i32.ne(i32.and ${L('f')}${C(32)})${C(e.thumb?32:0)})(then(local.set $why(i32.const 3))(br $done)))
 (block $unsupported `;
 const groups=Math.ceil(blocks.length/64);
 if(groups>1)for(let g=groups-1;g>=0;g--)body+=`(block $group${g} `;
 const groupLabels=new Map();blocks.forEach((b,i)=>b.ops.forEach(c=>groupLabels.set((c.pc-e.pc)/width,'$group'+Math.floor(i/64))));
 if(groups>1)body+=`(br_table ${Array.from({length:1024/width},(_,i)=>groupLabels.get(i)||'$unsupported').join(' ')} $unsupported(i32.shr_u(i32.and ${reg(15)}${C(1023)})${C(e.thumb?1:2)}))`;
 for(let g=0;g<groups;g++){
  if(groups>1)body+=`) `;const lo=g*64,hi=Math.min(blocks.length,lo+64);
  for(let i=hi-1;i>=lo;i--)body+=`(block $case${i} `;
  body+=`(br_table ${Array.from({length:1024/width},(_,i)=>{const label=labels.get(i);if(!label)return '$unsupported';const index=+label.slice(5);return index>=lo&&index<hi?label:'$unsupported';}).join(' ')} $unsupported(i32.shr_u(i32.and ${reg(15)}${C(1023)})${C(e.thumb?1:2)}))`;
  for(let bi=lo;bi<hi;bi++){const b=blocks[bi];body+=`) `;
   for(const c of b.ops)body+=`(if(i32.ne(i32.load${e.thumb?'16_u':''} offset=${c.pc&1023} ${L('fetch')})${C(c.w)})(then(local.set $why(i32.const 5))(br $done)))`;
   body+=`(block $tail_bad${bi} `;for(let i=b.ops.length-1;i>=0;i--)body+=`(block $tail${bi}_${i} `;
   body+=`(br_table ${b.ops.map((_,i)=>'$tail'+bi+'_'+i).join(' ')} $tail_bad${bi}(i32.shr_u(i32.sub ${reg(15)}${C(b.head)})${C(e.thumb?1:2)}))`;
   for(const c of b.ops)body+=`) (if(i32.ge_u ${L('n')}${L('budget')})(then(local.set $why(i32.const 1))(br $done)))`+put(15,C(c.pc))+c.body+set('n',add(L('n'),C(1)));
   body+=`(br $run))(local.set $why(i32.const 4))(br $done)`;
  }
  body+='(br $unsupported)';
 }
 body+=')(local.set $why(i32.const 4))(br $done)))';
 for(let n=0;n<16;n++)body+=`(i32.store offset=${R+n*4} ${L('c')}${reg(n)})`;
 body+=`(i32.store offset=${F} ${L('c')}${L('f')})`;if(DIAG)body+=`(i64.store(i32.add ${C(DIAG)}(i32.mul ${L('why')}${C(8)}))(i64.add(i64.load(i32.add ${C(DIAG)}(i32.mul ${L('why')}${C(8)})))(i64.const 1)))`;body+=`(if(i32.eq ${L('why')}${C(5)})(then(call $stale ${C(e.pc)}${C(e.thumb?1:0)})))`+L('n');
 let masks=Array(16).fill(0);for(const head of code.keys()){let bit=(head-e.pc)/width;masks[bit>>>5]|=1<<(bit&31);}
 return {masks,count:code.size,heads:blocks.length,wat:`(func(export "b${e.index}")(param $c i32)(param $budget i32)(result i32) ${Array.from({length:16},(_,i)=>`(local $r${i} i32)`).join(' ')}(local $a i32)(local $b i32)(local $r i32)(local $f i32)(local $p i32)(local $fetch i32)(local $entry i32)(local $addr i32)(local $why i32)(local $n i32)(local $wide i64)${body})`};
}

module.exports = { configure(offsets) { OFF=offsets;R=OFF[0];F=OFF[1];FETCH=OFF[2];DIAG=0; }, compileRegion };

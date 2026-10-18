/* ARMv6/Thumb guarded1KiB region emitter. See README.md for regeneration. */
const C=n=>`(i32.const ${n|0})`;
const L=n=>`(local.get $${n})`;
const K=n=>typeof n==='string'?L(n):C(n);
const op=(name,...args)=>`(${name} ${args.join(' ')})`;
const add=(a,b)=>op('i32.add',a,b),and=(a,b)=>op('i32.and',a,b),xor=(a,b)=>op('i32.xor',a,b);
const shl=(a,n)=>op('i32.shl',a,C(n)),shr=(a,n)=>op('i32.shr_u',a,C(n));
let R,F,FETCH,OFF;
// r0-r14 live in the CPU struct; V8 cannot keep all guest state in host
// registers, and memory operands spill less than locals across region exits.
const reg=n=>n===15?L('r15'):`(i32.load offset=${R+n*4} ${L('c')})`;
const put=(n,x)=>n===15?`(local.set $r15 ${x})`:`(i32.store offset=${R+n*4} ${L('c')} ${x})`;
// Loop-invariant CPU fields are reloaded at use rather than held live.
const FE=()=>`(i32.load offset=${FETCH} ${L('c')})`,TG=()=>`(i32.load offset=${OFF[6]} ${L('c')})`,HW=()=>`(i32.load offset=${OFF[10]}(i32.load offset=${OFF[9]} ${L('c')}))`;
const set=(n,x)=>`(local.set $${n} ${x})`;
// NZCV live in locals inside a call: N=sign(fN), Z=(fZ==0), C=fC, V=sign(fV).
const carry=()=>L('fC');
const packed=()=>op('i32.or',op('i32.or',and(L('f'),C(0x0fffffff)),and(L('fN'),C(0x80000000))),op('i32.or',op('i32.or',shl(op('i32.eqz',L('fZ')),30),shl(L('fC'),29)),shl(shr(L('fV'),31),28)));
const unpack=()=>set('fN',L('f'))+set('fZ',op('i32.eqz',and(L('f'),C(0x40000000))))+set('fC',and(shr(L('f'),29),C(1)))+set('fV',shl(L('f'),3));
function flags(logical,cy=carry()) {
  let s=set('fC',cy)+set('fN',L('r'))+set('fZ',L('r'));
  if(!logical)s+=set('fV',and(xor(xor(L('a'),L('b')),C(-1)),xor(L('a'),L('r'))));
  return s;
}
function arith(a,b,cin,saveFlags){
 let s=set('a',a)+set('b',b);
 if(!saveFlags)return s+set('r',add(add(L('a'),L('b')),cin));
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
 const nz=op('i32.ne',L('fZ'),C(0)),eq=op('i32.ge_s',xor(L('fN'),L('fV')),C(0));
 const base=[op('i32.eqz',L('fZ')),L('fC'),op('i32.lt_s',L('fN'),C(0)),op('i32.lt_s',L('fV'),C(0)),and(L('fC'),nz),eq,and(eq,nz),C(1)][code>>1];
 return code&1?op('i32.eqz',base):base;
}
const DREAD_ENTRIES=1024; // ARM_DREAD_ENTRIES; jit-runtime.c asserts it.
function span(addr,bytes,load,transactions,n){
 let fail=reason=>`(local.set $why ${C(reason)})(br $done)`;
 let s=set('addr',addr);
 // Aligned accesses of at most a word cannot leave their 1 KiB block.
 if(bytes>1)s+=`(if(i32.and ${L('addr')}${C(bytes>=4?3:1)})(then ${fail(6)}))`;
 if(bytes>4)s+=`(if(i32.gt_u(i32.add(i32.and ${L('addr')}${C(1023)})${C(bytes)})${C(1024)})(then ${fail(6)}))`;
 // The mode bits are invariant within a call, so privilege is hoisted to entry.
 const priv=L('priv'),slot=and(add(shr(L('addr'),10),L('pofs')),C(DREAD_ENTRIES-1));
 s+=set('entry',add(add(L('c'),C(OFF[load?4:5])),op('i32.mul',slot,C(OFF[11]))));
 s+=set('p',`(i32.load ${L('entry')})`);
 s+=`(if(i32.ne(i32.load offset=4 ${L('entry')})(i32.or(i32.and ${L('addr')}${C(~1023)})${priv}))(then ${fail(6)}))`;
 s+=`(if(i32.ne(i32.load offset=8 ${L('entry')})${TG()})(then ${fail(6)}))`;
 s+=`(if(i32.eqz ${L('p')})(then ${fail(6)}))`;
 if(!load)s+=`(if(i32.eqz ${HW()})(then ${fail(6)}))`;
 s+=set('p',add(L('p'),and(L('addr'),C(1023))));
 if(!load)s+=`(if(i32.and(i32.lt_u ${L('p')}(i32.add ${FE()}${C(1024)}))(i32.gt_u(i32.add ${L('p')}${C(bytes)})${FE()}))(then ${fail(7)}))`;
 s+=set(load?'rh':'wh',`(i64.add ${L(load?'rh':'wh')}(i64.const ${transactions}))`);
 return s;
}

function transfer(addr,bytes,load,rd,n){
 let s=span(addr,bytes,load,1,n);
 const suffix=bytes===4?'':bytes===2?'16_u':'8_u';
 return s+(load?put(rd,`(i32.load${suffix} ${L('p')})`):`(i32.store${bytes===4?'':bytes===2?'16':'8'} ${L('p')} ${reg(rd)})`);
}
const badpc=t=>`(if(i32.eq(i32.and ${L('r')}${C(3)})${C(2)})(then ${t?`(local.set $rh(i64.sub ${L('rh')}(i64.const ${t})))`:''}(br $done)))`;
const interwork=()=>set('f',op('i32.or',and(L('f'),C(~32)),shl(and(L('r'),C(1)),5)));
function emitBase(insn,pc,thumb,n){
 let body='',stop=false,next=C(pc+(thumb?2:4));
 const rr=r=>r===15?C(pc+(thumb?4:8)):reg(r);
 if(!thumb){
   const cond=insn>>>28;if(cond===15)return null;
   const rn=insn>>>16&15,rd=insn>>>12&15,code=insn>>>21&15,S=!!(insn&0x100000);
   if((insn&0x0e000000)===0x0a000000){const off=(insn<<8)>>6;next=C(pc+8+off);if(insn&0x1000000)body+=put(14,C(pc+4));stop=true;}
   else if((insn&0x0ffffff0)===0x012fff10){body=set('r',rr(insn&15))+`(if(i32.eq(i32.and ${L('r')} ${C(3)})${C(2)})(then(return ${K(n)})))`;
     body+=set('f',op('i32.or',and(L('f'),C(~32)),shl(and(L('r'),C(1)),5)))+`(i32.store offset=${F} ${L('c')} ${L('f')})`;next=and(L('r'),C(~1));stop=true;}
   else if((insn&0x0f700000)===0x05100000&&rd===15){const addr=(insn&0x800000)?add(rr(rn),C(insn&4095)):op('i32.sub',rr(rn),C(insn&4095));
     body=span(addr,4,true,1,n)+set('r','(i32.load '+L('p')+')')+badpc(1)+interwork();next=and(L('r'),C(~1));stop=true;}
   else if((insn&0x0f200000)===0x05000000&&rd!==15){const addr=(insn&0x800000)?add(rr(rn),C(insn&4095)):op('i32.sub',rr(rn),C(insn&4095));body=transfer(addr,(insn&0x400000)?1:4,!!(insn&0x100000),rd,n);}
   else if((insn&0x0e400000)===0x08000000 && rn!==15 && (insn&0xffff)!==0 && (!(insn&0x8000)||(insn&0x100000)) && !((insn&0x200000)&&(insn&(1<<rn)))){
     const withPc=!!(insn&0x8000),regs=Array.from({length:15},(_,i)=>i).filter(i=>insn&(1<<i)),count=regs.length+(withPc?1:0),load=!!(insn&0x100000),U=!!(insn&0x800000),P=!!(insn&0x1000000);
     body=set('a',reg(rn));const addr=add(L('a'),C(U?(P?4:0):(P?-4*count:-4*count+4)));
     body+=span(addr,count*4,load,count,n);
     if(withPc)body+=set('r',`(i32.load offset=${regs.length*4} ${L('p')})`)+badpc(count);
     regs.forEach((r,i)=>body+=load?put(r,`(i32.load offset=${i*4} ${L('p')})`):`(i32.store offset=${i*4} ${L('p')} ${reg(r)})`);
     if(insn&0x200000)body+=put(rn,add(L('a'),C(U?4*count:-4*count)));
     if(withPc){body+=interwork();next=and(L('r'),C(~1));stop=true;}
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
     else if(code===13)body=set('r',op('i32.mul',reg(rd),reg(rs)))+set('fN',L('r'))+set('fZ',L('r'))+put(rd,L('r'));
     else if(code in mapping)body=alu(mapping[code],reg(rd),reg(rs),true,rd);else return null;
   }else if((insn&0xfc00)===0x4400){let target=rd|(insn>>>4&8),source=rs|(insn>>>3&8),code=insn>>>8&3;
     if(code===3){if(source===15||(insn&7))return null;body=set('r',reg(source))+badpc(0);if(insn&0x80)body+=put(14,C((pc+2)|1));body+=interwork();next=and(L('r'),C(~1));stop=true;}
     else{if(target===15)return null;
     body=alu(code===0?4:code===1?10:13,rr(target),rr(source),code===1,target);}
   }else if((insn&0xf800)===0x4800){body=transfer(C(((pc+4)&~3)+((insn&255)<<2)),4,true,insn>>>8&7,n);}
   else if(group===6||group===7||group===8||group===9){let bytes=group===8?2:group===7?1:4;let target=group===9?insn>>>8&7:rd,addr=group===9?add(reg(13),C((insn&255)*4)):add(reg(rs),C((insn>>>6&31)*bytes));body=transfer(addr,bytes,!!(insn&0x800),target,n);}
   else if(group===10){body=put(insn>>>8&7,add((insn&0x800)?reg(13):C((pc+4)&~3),C((insn&255)*4)));}
   else if((insn&0xff00)===0xb000){body=put(13,add(reg(13),C((insn&127)*((insn&128)?-4:4))));}
   else if((insn&0xf600)===0xb400){const load=!!(insn&0x800),extra=!!(insn&256),popPc=load&&extra;
     const regs=Array.from({length:8},(_,i)=>i).filter(i=>insn&(1<<i));if(extra&&!load)regs.push(14);const count=regs.length+(popPc?1:0);if(!count)return null;
     body=set('a',reg(13));body+=span(load?L('a'):add(L('a'),C(-4*count)),4*count,load,count,n);
     if(popPc)body+=set('r',`(i32.load offset=${regs.length*4} ${L('p')})`)+badpc(count);
     regs.forEach((r,i)=>body+=load?put(r,`(i32.load offset=${i*4} ${L('p')})`):`(i32.store offset=${i*4} ${L('p')} ${reg(r)})`);body+=put(13,add(L('a'),C((load?4:-4)*count)));
     if(popPc){body+=interwork();next=and(L('r'),C(~1));stop=true;}
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

/* VFP11 subset, bit-exact with core/src/arm/vfp.c as built for the browser
 * (no host exception flags). Anything that could differ exits before writing:
 * VFP unavailable, RMode, trap enables or short vectors in FPSCR, and NaN
 * results, whose payload depends on host operand order. */
const isVfp=insn=>{const cp=insn>>>8&15;return (cp===10||cp===11)&&((insn&0x0f000e00)===0x0e000a00||(insn&0x0fe00e00)===0x0c400a00);};
function vfpGate(){
 const get=off=>`(i32.load offset=${off} ${L('c')})`,cp=get(OFF[14]),cp10=and(shr(cp,20),C(3)),cp11=and(shr(cp,22),C(3)),acc=op('select',cp10,cp11,op('i32.lt_u',cp10,cp11));
 const allowed=op('i32.or',op('i32.eq',acc,C(3)),op('i32.and',op('i32.eq',acc,C(1)),op('i32.ne',and(L('f'),C(31)),C(16))));
 return set('fs',get(OFF[15]))+set('e',C(0))+`(if(i32.or(i32.eqz ${allowed})(i32.or(i32.eqz(i32.and ${get(OFF[13])}${C(1<<30)}))(i32.and ${L('fs')}${C(0x00f79f00)})))(then(br $done)))`;
}
const SV=k=>OFF[12]+4*k,FZ=()=>and(L('fs'),C(0x1000000));
const ldS=k=>`(f32.load offset=${SV(k)} ${L('c')})`,ldD=k=>`(f64.load offset=${SV(2*k)} ${L('c')})`;
const stF=(dbl,k,x)=>`(${dbl?'f64':'f32'}.store offset=${SV(dbl?2*k:k)} ${L('c')} ${x})`;
// FZ: a nonzero magnitude below the smallest normal becomes a signed zero (IDC on input, UFC on output).
function flush(dbl,v,bit){const t=dbl?'f64':'f32',i=dbl?'i64':'i32',k=x=>`(${i}.const ${x})`;
 const tiny=`(${i}.lt_u(${i}.sub(${i}.and(${i}.reinterpret_${t} ${L(v)})${k(dbl?'0x7fffffffffffffff':'0x7fffffff')})${k(1)})${k(dbl?'0x000fffffffffffff':'0x7fffff')})`;
 return `(if ${FZ()}(then(if ${tiny}(then ${set('e',op('i32.or',L('e'),C(bit)))}${set(v,`(${t}.copysign(${t}.const 0)${L(v)})`)}))))`;}
const IDC=0x80,UFC=0x8,IOC=0x1;
const nanExit=(dbl,v)=>`(if(${dbl?'f64':'f32'}.ne ${L(v)}${L(v)})(then(br $done)))`;
const commit=()=>`(if ${L('e')}(then(i32.store offset=${OFF[15]} ${L('c')}(i32.or ${L('fs')}${L('e')}))))`;
function fop(dbl,o,a,b){const t=dbl?'f64':'f32',A=dbl?'da':'sa',B=dbl?'db':'sb',Z=dbl?'dz':'sz';
 return set(A,a)+set(B,b)+flush(dbl,A,IDC)+flush(dbl,B,IDC)+set(Z,`(${t}.${o} ${L(A)}${L(B)})`)+nanExit(dbl,Z)+flush(dbl,Z,UFC);}
function emitVfp(insn){
 const b=i=>insn>>>i&1,vn=insn>>>16&15,vd=insn>>>12&15,vm=insn&15,dbl=!!b(8);
 if((insn&0x0fe00e00)===0x0c400a00){const rt=vd,rt2=vn,Ld=b(20);
  if((insn&0xc0)||rt===15||rt2===15)return null;
  let lo,hi;if(dbl){if(b(5))return null;lo=2*vm;hi=lo+1;}else{lo=vm*2+b(5);if(lo===31)return null;hi=lo+1;}
  const g=k=>`(i32.load offset=${SV(k)} ${L('c')})`;
  return vfpGate()+(Ld?put(rt,g(lo))+put(rt2,g(hi)):`(i32.store offset=${SV(lo)} ${L('c')}${reg(rt)})(i32.store offset=${SV(hi)} ${L('c')}${reg(rt2)})`);}
 if(insn&0x10){const opc1=insn>>>21&7,Ld=b(20),rt=vd;
  if((!dbl&&opc1===0)||(dbl&&opc1<=1)){if((insn&0x6f)||rt===15||(dbl&&b(7)))return null;const k=dbl?vn*2+opc1:vn*2+b(7);
   return vfpGate()+(Ld?put(rt,`(i32.load offset=${SV(k)} ${L('c')})`):`(i32.store offset=${SV(k)} ${L('c')}${reg(rt)})`);}
  if(dbl||opc1!==7||(insn&0xef)||vn!==1)return null;
  if(Ld){const v=L('fs');if(rt!==15)return vfpGate()+put(rt,v);
   return vfpGate()+set('fN',v)+set('fZ',op('i32.eqz',and(v,C(0x40000000))))+set('fC',and(shr(v,29),C(1)))+set('fV',shl(v,3));}
  if(rt===15)return null;
  return vfpGate()+`(i32.store offset=${OFF[15]} ${L('c')}${and(reg(rt),C(0xf3f79f9f))})`;}
 const opc=b(23)<<2|b(21)<<1|b(20),alt=b(6),t=dbl?'f64':'f32';
 const sreg=(f,lo)=>f*2+lo;
 if(opc!==7){if(opc>4||(opc===4&&alt))return null;
  if(dbl&&(b(22)||b(7)||b(5)))return null;
  const rd=dbl?vd:sreg(vd,b(22)),rn=dbl?vn:sreg(vn,b(7)),rm=dbl?vm:sreg(vm,b(5)),ld=dbl?ldD:ldS,Z=dbl?'dz':'sz',neg=x=>`(${t}.neg ${x})`;
  let s=vfpGate();
  if(opc===4)s+=fop(dbl,'div',ld(rn),ld(rm));
  else if(opc===3)s+=fop(dbl,alt?'sub':'add',ld(rn),ld(rm));
  else if(opc===2){s+=fop(dbl,'mul',ld(rn),ld(rm));if(alt)s+=set(Z,neg(L(Z)));}
  else{const kind=opc===0?(alt?'VMLS':'VMLA'):(alt?'VNMLA':'VNMLS');
   s+=fop(dbl,'mul',ld(rn),ld(rm));const p=kind==='VMLS'||kind==='VNMLA'?neg(L(Z)):L(Z),d=kind==='VNMLA'||kind==='VNMLS'?neg(ld(rd)):ld(rd);
   s+=fop(dbl,'add',d,p);}
  return s+stF(dbl,rd,L(Z))+commit();}
 if(!alt)return null;
 const opc2=vn,top=b(7),D=b(22),M=b(5);
 if(opc2===0||(opc2===1&&!top)){if(dbl&&(D||M))return null;
  if(dbl){const g=`(i64.load offset=${SV(2*vm)} ${L('c')})`,x=opc2===1?`(i64.xor ${g}(i64.const 0x8000000000000000))`:top?`(i64.and ${g}(i64.const 0x7fffffffffffffff))`:g;
   return vfpGate()+`(i64.store offset=${SV(2*vd)} ${L('c')}${x})`;}
  const g=`(i32.load offset=${SV(sreg(vm,M))} ${L('c')})`,x=opc2===1?xor(g,C(0x80000000)):top?and(g,C(0x7fffffff)):g;
  return vfpGate()+`(i32.store offset=${SV(sreg(vd,D))} ${L('c')}${x})`;}
 if(opc2===4||opc2===5){const zero=opc2===5;if(zero&&(vm||M))return null;if(dbl&&(D||M))return null;
  const i=dbl?'i64':'i32',A=dbl?'da':'sa',B=dbl?'db':'sb',UA=dbl?'ua':'a',UB=dbl?'ub':'b';
  const raw=k=>dbl?`(i64.load offset=${SV(2*k)} ${L('c')})`:`(i32.load offset=${SV(k)} ${L('c')})`;
  const ra=dbl?vd:sreg(vd,D),rb=dbl?vm:sreg(vm,M);
  const snan=u=>dbl?`(i32.and(i64.eq(i64.and ${L(u)}(i64.const 0x7ff8000000000000))(i64.const 0x7ff0000000000000))(i64.ne(i64.and ${L(u)}(i64.const 0x0007ffffffffffff))(i64.const 0)))`
   :`(i32.and(i32.eq(i32.and ${L(u)}${C(0x7fc00000)})${C(0x7f800000)})(i32.ne(i32.and ${L(u)}${C(0x3fffff)})${C(0)}))`;
  let s=vfpGate()+set(UA,raw(ra))+set(UB,zero?`(${i}.const 0)`:raw(rb))+set(A,`(${t}.reinterpret_${i} ${L(UA)})`)+set(B,`(${t}.reinterpret_${i} ${L(UB)})`)+flush(dbl,A,IDC)+flush(dbl,B,IDC);
  const nan=op('i32.or',`(${t}.ne ${L(A)}${L(A)})`,`(${t}.ne ${L(B)}${L(B)})`);
  s+=set('r',nan);
  let sig=zero?snan(UA):op('i32.or',snan(UA),snan(UB));if(top)sig=op('i32.or',sig,L('r'));
  s+=`(if ${sig}(then ${set('e',op('i32.or',L('e'),C(IOC)))}))`;
  const flags=op('select',C(0x30000000),op('select',C(0x60000000),op('select',C(0x80000000),C(0x20000000),`(${t}.lt ${L(A)}${L(B)})`),`(${t}.eq ${L(A)}${L(B)})`),L('r'));
  return s+`(i32.store offset=${OFF[15]} ${L('c')}(i32.or(i32.or(i32.and ${L('fs')}${C(0x0fffffff)})${flags})${L('e')}))`;}
 if(opc2===7&&top){
  if(dbl){if(M)return null;return vfpGate()+set('da',ldD(vm))+flush(true,'da',IDC)+set('sz',`(f32.demote_f64 ${L('da')})`)+nanExit(false,'sz')+flush(false,'sz',UFC)+stF(false,sreg(vd,D),L('sz'))+commit();}
  if(D)return null;return vfpGate()+set('sa',ldS(sreg(vm,M)))+flush(false,'sa',IDC)+set('dz',`(f64.promote_f32 ${L('sa')})`)+nanExit(true,'dz')+stF(true,vd,L('dz'))+commit();}
 return null;
}
function emit(insn,pc,thumb,n){let body='',next=C(pc+(thumb?2:4)),stop=false;
 if(!thumb){const cond=insn>>>28,rn=insn>>>16&15,rd=insn>>>12&15,rm=insn&15,rr=r=>r===15?C(pc+8):reg(r);if(cond===15)return emitBase(insn,pc,thumb,n);
  if(isVfp(insn)){body=emitVfp(insn);if(body===null)return null;}
  else if((insn&0x0f200e00)===0x0d000a00){const D=!!(insn&0x400000),dbl=!!(insn&0x100),load=!!(insn&0x100000),U=!!(insn&0x800000),first=(rd*2)+(dbl?0:+D);if(dbl&&D)return null;
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
  }else if((insn&0x0f8003f0)===0x06800070&&rd!==15&&rm!==15&&[0xa,0xb,0xc,0xe,0xf].includes(insn>>>20&15)&&!((insn>>>20&15)===0xc&&rn!==15)){
   // SXTB/SXTH/UXTB16/UXTB/UXTH and the Rn-accumulating SXTA*/UXTA* forms.
   const kind=insn>>>20&15,rot=(insn>>>10&3)*8,v=rot?op('i32.rotr',reg(rm),C(rot)):reg(rm);
   let x=kind===0xa?op('i32.extend8_s',v):kind===0xb?op('i32.extend16_s',v):and(v,C(kind===0xc?0x00ff00ff:kind===0xe?0xff:0xffff));
   if(rn!==15)x=add(reg(rn),x);body=put(rd,x);}
  else if((insn&0x0fff0fff)===0x010f0000&&rd!==15){body=put(rd,packed());}
  else if((insn&0x0fff0fff)===0x0e070f5a){body='';}
  else return emitBase(insn,pc,thumb,n);
  body=cond===14?body+put(15,next):`(if ${condition(cond)}(then ${body}${put(15,next)})(else ${put(15,C(pc+4))}))`;
 }else if((insn&0xf800)===0xe800){body=set('a',add(reg(14),C((insn&2047)*2)))+put(14,C((pc+2)|1))+set('f',and(L('f'),C(~32)))+put(15,and(L('a'),C(~3)));stop=true;}
 else if((insn&0xff00)===0xb200){const kind=insn>>>6&3,v=reg(insn>>>3&7);body=put(insn&7,kind===0?op('i32.extend16_s',v):kind===1?op('i32.extend8_s',v):and(v,C(kind===2?0xffff:0xff)))+put(15,next);}
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
 body+=put(15,`(i32.load offset=${R+60} ${L('c')})`);
 body+=set('f',`(i32.load offset=${F} ${L('c')})`)+unpack()+set('priv',op('i32.ne',and(L('f'),C(31)),C(16)))+set('pofs',op('i32.mul',L('priv'),C(DREAD_ENTRIES/2)));
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
   // Stores never reach the current fetch page while this call stays in it,
   // so a block's words need verifying only on its first entry per call.
   const vg='$v'+(bi>>6),vb=`(i64.const ${BigInt.asIntN(64,1n<<BigInt(bi&63))})`;
   body+=`(if(i64.eqz(i64.and(local.get ${vg})${vb}))(then `;
   // Block ops are contiguous, so their words are compared eight bytes at a time.
   const width=e.thumb?2:4,bytes=[];for(const c of b.ops)for(let k=0;k<width;k++)bytes.push(c.w>>>(8*k)&255);
   for(let i=0;i<bytes.length;){const size=bytes.length-i>=8?8:bytes.length-i>=4?4:2,at=(b.ops[0].pc&1023)+i;let v=0n;for(let k=size-1;k>=0;k--)v=v<<8n|BigInt(bytes[i+k]);
    const want=size===8?`(i64.const ${BigInt.asIntN(64,v)})`:C(Number(v)),load=size===8?'i64.load':size===4?'i32.load':'i32.load16_u';
    body+=`(if(${size===8?'i64':'i32'}.ne(${load} offset=${at} ${FE()})${want})(then(local.set $why(i32.const 5))(br $done)))`;i+=size;}
   body+=`(local.set ${vg}(i64.or(local.get ${vg})${vb}))))`;
   body+=`(loop $L${bi}(block $tail_bad${bi} `;for(let i=b.ops.length-1;i>=0;i--)body+=`(block $tail${bi}_${i} `;
   body+=`(br_if $tail${bi}_0(i32.eq ${reg(15)}${C(b.head)}))(br_table ${b.ops.map((_,i)=>'$tail'+bi+'_'+i).join(' ')} $tail_bad${bi}(i32.shr_u(i32.sub ${reg(15)}${C(b.head)})${C(e.thumb?1:2)}))`;
   for(const c of b.ops)body+=`) (if(i32.ge_u ${L('n')}${L('budget')})(then(local.set $why(i32.const 1))(br $done)))`+put(15,C(c.pc))+c.body+set('n',add(L('n'),C(1)));
   // Statically known successors in this group skip the region dispatch.
   const last=b.ops[b.ops.length-1],seen=new Set();
   for(const m of last.body.matchAll(/\(local.set \$r15 \(i32.const (-?\d+)\)\)/g)){const t=(+m[1])>>>0;if(seen.has(t))continue;seen.add(t);
    const j=t===b.head?bi:blocks.findIndex((x,k)=>k>bi&&k<hi&&x.head===t);if(j<0)continue;
    // Only an op that writes f can leave the region's instruction set.
    const hit=`(i32.eq ${reg(15)}${C(t)})`,tset=last.body.includes('(local.set $f ');
    body+=`(br_if ${j===bi?'$L'+bi:'$case'+j}${tset?`(i32.and${hit}(i32.eq(i32.and ${L('f')}${C(32)})${C(e.thumb?32:0)}))`:hit})`;}
   body+=`(br $run))(local.set $why(i32.const 4))(br $done))`;
  }
  body+='(br $unsupported)';
 }
 body+=')(local.set $why(i32.const 4))(br $done)))';
 for(const [o,v] of [[OFF[7],'rh'],[OFF[8],'wh']])body+=`(i64.store offset=${o} ${L('c')}(i64.add(i64.load offset=${o} ${L('c')})${L(v)}))`;
 body+=`(i32.store offset=${R+60} ${L('c')}${reg(15)})`;
 body+=`(i32.store offset=${F} ${L('c')}${packed()})`;body+=`(if(i32.eq ${L('why')}${C(5)})(then(call $stale ${C(e.pc)}${C(e.thumb?1:0)})))`+L('n');
 let masks=Array(16).fill(0);for(const head of code.keys()){let bit=(head-e.pc)/width;masks[bit>>>5]|=1<<(bit&31);}
 return {masks,count:code.size,heads:blocks.length,wat:`(func(export "b${e.index}")(param $c i32)(param $budget i32)(result i32) (local $r15 i32)(local $a i32)(local $b i32)(local $r i32)(local $f i32)(local $p i32)(local $entry i32)(local $addr i32)(local $why i32)(local $n i32)(local $wide i64)(local $rh i64)(local $wh i64)(local $priv i32)(local $pofs i32)(local $fN i32)(local $fZ i32)(local $fC i32)(local $fV i32)(local $fs i32)(local $e i32)(local $sa f32)(local $sb f32)(local $sz f32)(local $da f64)(local $db f64)(local $dz f64)(local $ua i64)(local $ub i64)${Array.from({length:Math.ceil(blocks.length/64)},(_,g)=>`(local $v${g} i64)`).join('')}${body})`};
}

/* Linked regions: every region is one function in a shared seed table. When
 * control leaves a region for another installed region, the region tail-calls
 * it through the runtime link table instead of returning to C. Registers are
 * flushed at every region exit, so the CPU struct is exact at each edge. */
const LINK=`(if(i32.and(i32.lt_u(local.get $n)(local.get $budget))(i32.or(i32.eq(local.get $why)(i32.const 2))(i32.eq(local.get $why)(i32.const 3))))(then
 (local.set $a(local.get $r15))
 (local.set $p(i32.add(global.get $link)(i32.shl(i32.and(i32.shr_u(local.get $a)(i32.const 10))(i32.const 4095))(i32.const 3))))
 (if(i32.and(i32.eq(i32.load(local.get $p))(i32.or(i32.or(i32.and(local.get $a)(i32.const -1024))(i32.shr_u(i32.and(local.get $f)(i32.const 32))(i32.const 4)))(i32.const 1)))(i32.eqz(i32.and(local.get $a)(select(i32.const 1)(i32.const 3)(i32.and(local.get $f)(i32.const 32))))))
  (then(if(call $refill(local.get $c)(local.get $a))(then(return_call_indirect(type $pg)(local.get $c)(local.get $budget)(local.get $n)(i32.load offset=4(local.get $p)))))))))`;
function compilePages(regions){
 const funcs=[],defs=[];
 for(const e of regions){
  const built=compileRegion(e);if(!built)continue;
  const i=defs.length,sig=`(func(export "b${e.index}")(param $c i32)(param $budget i32)(result i32)`,tail='(local.get $n))';
  let wat=built.wat;
  if(!wat.startsWith(sig)||wat.split('(local $n i32)').length!==2||!wat.endsWith(tail))throw Error('Unexpected region shape');
  wat=`(func $p${i}(type $pg)(param $c i32)(param $budget i32)(param $n i32)(result i32)`+wat.slice(sig.length,-tail.length).replace('(local $n i32)','')+LINK+tail;
  funcs.push(wat);defs.push({pc:e.pc,thumb:e.thumb,index:i,masks:built.masks.map(v=>v>>>0)});
 }
 const wat=`(module(type $pg(func(param i32 i32 i32)(result i32)))(import "env" "memory"(memory 8192))(import "env" "stale"(func $stale(param i32 i32)))(import "env" "refill"(func $refill(param i32 i32)(result i32)))(import "env" "link"(global $link i32))(table ${funcs.length} funcref)(elem(i32.const 0) func ${funcs.map((_,i)=>'$p'+i).join(' ')})${funcs.join('')}(func(export "run")(param $c i32)(param $budget i32)(param $i i32)(result i32)(call_indirect(type $pg)(local.get $c)(local.get $budget)(i32.const 0)(local.get $i))))`;
 return {wat,regions:defs};
}

module.exports = { configure(offsets) { OFF=offsets;R=OFF[0];F=OFF[1];FETCH=OFF[2]; }, compileRegion, compilePages };

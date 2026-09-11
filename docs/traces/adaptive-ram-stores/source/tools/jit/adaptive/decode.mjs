// Initial explicit IR slice. Unsupported encodings end a unit BEFORE execution.
// No host pointers, executable code, or guest effects are produced by decoding.
export function decodeInstruction(raw,pc,thumb=false){
 if(!Number.isInteger(pc)||pc<0||pc>0xffffffff||pc%(thumb?2:4))throw Error('Unaligned instruction PC');
 if(!Number.isInteger(raw)||raw<0||raw>(thumb?0xffff:0xffffffff))throw Error('Invalid instruction bits');
 const width=thumb?2:4,next=(pc+width)>>>0;
 const base={pc,raw,thumb,width,condition:thumb?14:raw>>>28};
 const fallback=()=>({...base,op:'fallback',reason:'unsupported',successors:[]});
 const direct=(target,condition=base.condition,link=false)=>({...base,condition,op:'branch',target:target>>>0,link,returnAddress:link?next:null,successors:condition===14?[target>>>0]:[target>>>0,next]});
 if(thumb){
  const load=(rd,rn,bytes,offset,literal=false)=>({...base,op:'load',rd,rn,bytes,offset,add:true,preIndex:true,writeback:false,unprivileged:false,literal,successors:[next]});
  if((raw&0xf800)===0x4800)return load(raw>>>8&7,15,4,(raw&255)*4,true);
  if((raw&0xe000)===0x6000){const bytes=raw&0x1000?1:4;return {...load(raw&7,raw>>>3&7,bytes,(raw>>>6&31)*bytes),op:raw&0x800?'load':'store'};}
  const alu=(opcode,rd,rn,operand,setFlags=true)=>({...base,op:'alu',opcode,rd,rn,setFlags,operand,successors:[next]});
  const unshifted=rm=>({kind:'register',rm,shift:{type:0,kind:'immediate',amount:0}});
  if(raw<0x2000){
   const rd=raw&7,rs=raw>>>3&7;
   if((raw&0xf800)===0x1800){
    const operand=raw&0x400?{kind:'immediate',value:raw>>>6&7,carry:'preserve'}:unshifted(raw>>>6&7);
    return alu(raw&0x200?2:4,rd,rs,operand);
   }
   return alu(13,rd,0,{kind:'register',rm:rs,shift:{type:raw>>>11&3,kind:'immediate',amount:raw>>>6&31}});
  }
  if((raw&0xfc00)===0x4000){
   const code=raw>>>6&15,rd=raw&7,rs=raw>>>3&7;
   if([2,3,4,7].includes(code))return alu(13,rd,rd,{kind:'register',rm:rd,shift:{type:{2:0,3:1,4:2,7:3}[code],kind:'register',rs}});
   if(code===9)return alu(3,rd,rs,{kind:'immediate',value:0,carry:'preserve'});
   return alu(code===13?'mul':code,rd,rd,unshifted(rs));
  }
  if((raw&0xfc00)===0x4400){
   const code=raw>>>8&3,rd=(raw&7)|(raw>>>4&8),rs=raw>>>3&15;
   // High-register CMP can read PC; ADD/MOV destinations at PC and BX/BLX
   // retain the reference boundary rather than gaining implicit branch logic.
   if(code===3||(rd===15&&code!==1))return fallback();
   return alu([4,10,13][code],rd,rd,unshifted(rs),code===1);
  }
  if((raw&0xf800)===0xe000)return direct(pc+4+((raw<<21)>>20));
  if((raw&0xf000)===0xd000){const cond=raw>>>8&15;if(cond>=14)return fallback();return direct(pc+4+((raw<<24)>>23),cond);}
  // MOV/CMP/ADD/SUB immediate, low register, always sets flags.
  if((raw&0xe000)===0x2000)return {...base,op:'alu',opcode:[13,10,4,2][raw>>>11&3],rd:raw>>>8&7,rn:raw>>>8&7,setFlags:true,operand:{kind:'immediate',value:raw&255,carry:'preserve'},successors:[next]};
  return fallback();
 }
 if(base.condition===15)return fallback();
 if((raw&0x0e000000)===0x04000000){
  const rn=raw>>>16&15,rd=raw>>>12&15,preIndex=!!(raw&0x1000000),add=!!(raw&0x800000),w=!!(raw&0x200000),offset=raw&4095;
  const isLoad=!!(raw&0x100000),bytes=raw&0x400000?1:4;
  const sameZeroPost=isLoad&&!preIndex&&!w&&rn===rd&&offset===0;
  if((rd===15&&(isLoad||bytes===1))||((!preIndex||w)&&(rn===15||(rn===rd&&!sameZeroPost))))return fallback();
  return {...base,op:isLoad?'load':'store',rd,rn,bytes,offset,add,preIndex,writeback:(!preIndex||w)&&!sameZeroPost,unprivileged:!preIndex&&w,literal:false,successors:[next]};
 }
 if((raw&0x0e000000)===0x0a000000)return direct(pc+8+((raw<<8)>>6),base.condition,!!(raw&0x1000000));
 if((raw&0x0ffffff0)===0x012fff10)return {...base,op:'branch-exchange',rm:raw&15,successors:base.condition===14?[]:[next],dynamic:true};
 // ARM data processing; privileged/miscellaneous encodings and writes to PC
 // remain with the reference engine until their own contracts are tested.
 const opcode=raw>>>21&15,rd=raw>>>12&15,setFlags=!!(raw&0x100000);
 if((raw&0x0e000000)===0x02000000&&rd!==15&&(!(opcode>=8&&opcode<=11)||setFlags)){
  const rotation=(raw>>>8&15)*2,imm=raw&255,value=rotation?((imm>>>rotation)|(imm<<(32-rotation)))>>>0:imm;
  return {...base,op:'alu',opcode,rd,rn:raw>>>16&15,setFlags,operand:{kind:'immediate',value,carry:rotation?value>>>31:'preserve'},successors:[next]};
 }
 if((raw&0x0e000000)===0&&(raw&0x90)!==0x90&&rd!==15&&(!(opcode>=8&&opcode<=11)||setFlags)){
  const rn=raw>>>16&15,rm=raw&15,type=raw>>>5&3,controlled=!!(raw&0x10),rs=raw>>>8&15;
  // The current ARM1176 interpreter refuses every R15 operand in a
  // register-controlled shift. Do not invent the older ARM7 PC+12 behavior.
  if(controlled&&(rn===15||rm===15||rs===15))return fallback();
  const shift=controlled?{type,kind:'register',rs}:{type,kind:'immediate',amount:raw>>>7&31};
  return {...base,op:'alu',opcode,rd,rn,setFlags,operand:{kind:'register',rm,shift},successors:[next]};
 }
 return fallback();
}

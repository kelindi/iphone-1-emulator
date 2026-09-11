#include "dns_packet.h"
static unsigned get16(const unsigned char *p){return (unsigned)p[0]*256+p[1];}
static void put16(unsigned char *p,unsigned n){p[0]=(unsigned char)(n>>8);p[1]=(unsigned char)n;}
int own_dns_response(const unsigned char *in,unsigned n,unsigned char *out,unsigned cap,unsigned *written){
 static const unsigned char host[]={9,'i','p','h','o','n','e','-','w','u',5,'a','p','p','l','e',3,'c','o','m',0};
 static const unsigned char auth[]={3,'d','m','5',6,'g','o','o','g','l','e',3,'c','o','m',0};
 if(!written)return 0;
 *written=0;
 if(!in||!out||n<17||n>OWN_DNS_PACKET_CAP)return 0;
 unsigned flags=get16(in+2),additional=get16(in+10);
 if((flags&~0x0130u)||get16(in+4)!=1||get16(in+6)||get16(in+8)||additional>1)return 0;
 unsigned pos=12;
 for(;;){
  if(pos>=n)return 0;
  unsigned len=in[pos++];
  if(!len)break;
  if(len>63||len>n-pos||pos-12+len>254)return 0;
  pos+=len;
 }
 /* Compare encoded labels as well as letters: dots inside one label must
  * never alias separators. Only these two owned service names are routed. */
 unsigned namesize=pos-12,match=0;
 const unsigned char *names[]={host,auth};
 const unsigned sizes[]={sizeof host,sizeof auth};
 for(unsigned k=0;k<2;k++){
  if(namesize!=sizes[k])continue;
  unsigned same=1;
  for(unsigned j=0;j<namesize;j++){
   unsigned c=in[12+j];if(c>='A'&&c<='Z')c+=32;
   if(c!=names[k][j])same=0;
  }
  if(same)match=1;
 }
 if(n-pos<4)return 0;
 unsigned type=get16(in+pos),klass=get16(in+pos+2);pos+=4;
 if(klass!=1)return 0;
 unsigned question_end=pos,udp=512,do_bit=0;
 if(additional){
  if(n-pos<11||in[pos]!=0||get16(in+pos+1)!=41)return 0;
  udp=get16(in+pos+3);if(udp<512)udp=512;if(udp>OWN_DNS_PACKET_CAP)udp=OWN_DNS_PACKET_CAP;
  /* EDNS version/extended RCODE zero; only DNSSEC-OK is a known flag. */
  if(in[pos+5]||in[pos+6]||(get16(in+pos+7)&~0x8000u))return 0;
  do_bit=get16(in+pos+7);unsigned bytes=get16(in+pos+9);pos+=11;
  if(bytes!=n-pos)return 0;
  while(bytes){if(bytes<4)return 0;unsigned size=get16(in+pos+2);if(size>bytes-4)return 0;pos+=4+size;bytes-=4+size;}
 }
 if(pos!=n)return 0;
 unsigned answer=match&&type==1,total=question_end+(answer?16:0)+(additional?11:0);
 if(total>cap||total>udp)return 0;
 for(unsigned i=0;i<question_end;i++)out[i]=in[i];
 put16(out+2,0x8400u|(flags&0x0110u)|(match?0:3));
 put16(out+6,answer);put16(out+8,0);put16(out+10,additional);
 pos=question_end;
 if(answer){
  out[pos++]=0xc0;out[pos++]=12;put16(out+pos,1);pos+=2;put16(out+pos,1);pos+=2;
  out[pos++]=0;out[pos++]=0;out[pos++]=0;out[pos++]=60;put16(out+pos,4);pos+=2;
  out[pos++]=127;out[pos++]=0;out[pos++]=0;out[pos++]=1;
 }
 if(additional){out[pos++]=0;put16(out+pos,41);pos+=2;put16(out+pos,udp);pos+=2;out[pos++]=0;out[pos++]=0;put16(out+pos,do_bit);pos+=2;put16(out+pos,0);pos+=2;}
 *written=pos;return 1;
}

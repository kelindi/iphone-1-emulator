#include "youtube_auth.h"
static unsigned length(const char *s){unsigned n=0;while(s[n])n++;return n;}
static int equal(const unsigned char *p,unsigned n,const char *s){if(n!=length(s))return 0;for(unsigned i=0;i<n;i++)if(p[i]!=(unsigned char)s[i])return 0;return 1;}
static int hex(unsigned c){if(c>='0'&&c<='9')return (int)(c-'0');if(c>='A'&&c<='F')return (int)(c-'A'+10);if(c>='a'&&c<='f')return (int)(c-'a'+10);return -1;}
static int sextet(unsigned c){if(c>='A'&&c<='Z')return (int)(c-'A');if(c>='a'&&c<='z')return (int)(c-'a'+26);if(c>='0'&&c<='9')return (int)(c-'0'+52);if(c=='-')return 62;if(c=='_')return 63;return -1;}
static int hash20(struct own_youtube_field f,unsigned char out[20]){
 unsigned char chars[28];unsigned n=0;
 for(unsigned i=0;i<f.length;i++){
  unsigned c=f.data[i];if(c=='%'){if(f.length-i<3)return 0;int a=hex(f.data[i+1]),b=hex(f.data[i+2]);if(a<0||b<0)return 0;c=(unsigned)(a*16+b);i+=2;}
  if(n==sizeof chars)return 0;chars[n++]=(unsigned char)c;
 }
 /* Apple's web-safe encoder may retain padding; neither standard '+'/'/'
  * nor nonzero unused bits are admitted. '.' is its URL-safe padding form. */
 if(n!=27&&(n!=28||(chars[27]!='='&&chars[27]!='.')))return 0;
 unsigned accumulator=0,bits=0,written=0;
 for(unsigned i=0;i<27;i++){int v=sextet(chars[i]);if(v<0)return 0;accumulator=(accumulator<<6)|(unsigned)v;bits+=6;if(bits>=8){bits-=8;out[written++]=(unsigned char)(accumulator>>bits);}}
 return written==20&&(accumulator&3u)==0;
}
int own_youtube_auth_parse(unsigned phase,const unsigned char *p,unsigned n,struct own_youtube_auth *out){
 if(out)*out=(struct own_youtube_auth){0};
 if(!out||!p||!n||n>OWN_YOUTUBE_AUTH_CAP||(phase!=1&&phase!=2))return 0;
 struct own_youtube_auth parsed={0};unsigned seen=0,pos=0;
 while(pos<n){unsigned start=pos;while(pos<n&&p[pos]!='&'){if(p[pos]<33||p[pos]>126)return 0;pos++;}unsigned end=pos,k=start;while(k<end&&p[k]!='=')k++;if(k==start||k==end||k+1==end)return 0;
  struct own_youtube_field *field=0;unsigned bit=0;
  if(equal(p+start,k-start,"hr1")){field=&parsed.hr1;bit=1;}
  else if(phase==2&&equal(p+start,k-start,"r1")){field=&parsed.r1;bit=2;}
  else if(phase==2&&equal(p+start,k-start,"r2")){field=&parsed.r2;bit=4;}
  else if(phase==2&&equal(p+start,k-start,"cert")){field=&parsed.cert;bit=8;}
  else if(phase==2&&equal(p+start,k-start,"sig")){field=&parsed.sig;bit=16;}
  else if(phase==2&&equal(p+start,k-start,"hmackr2")){field=&parsed.hmackr2;bit=32;}
  else return 0;
  if(seen&bit)return 0;seen|=bit;field->data=p+k+1;field->length=end-k-1;
  if(pos<n&&++pos==n)return 0;
 }
 if(seen!=(phase==1?1u:63u)||!hash20(parsed.hr1,parsed.decoded_hr1))return 0;
 /* Explicit byte copy avoids a freestanding CRT memcpy dependency. */
 for(unsigned i=0;i<sizeof parsed;i++)((unsigned char *)out)[i]=((const unsigned char *)&parsed)[i];
 return 1;
}
unsigned own_youtube_auth_response(unsigned phase,const unsigned char *p,unsigned n,unsigned char *out,unsigned cap,unsigned *size){
 if(size)*size=0;
 if(!out||!size||!p||!n||n>OWN_YOUTUBE_AUTH_CAP||(phase!=1&&phase!=2))return 0;
 struct own_youtube_auth parsed;unsigned status;const char *body;
 if(phase==2){
#if OWN_YOUTUBE_DEMO
  if(!own_youtube_auth_parse(2,p,n,&parsed)){status=400;body="Error=InvalidLocalCompatibilityForm\n";}
  else {status=200;body="Auth=iphone1-local-demo-token\n";}
#else
  status=503;body="Error=LocalDiagnosticOnly\nThis probe does not issue authentication tokens.\n";
#endif
 }
 else if(!own_youtube_auth_parse(1,p,n,&parsed)){status=400;body="Error=InvalidChallengeRequest\n";}
 else {status=200;body="r2=AAECAwQFBgcICQoLDA0ODxAREhM=\nhmackr2=iphone1-local-auth-probe\n";}
 unsigned count=length(body);if(count>cap)return 0;for(unsigned i=0;i<count;i++)out[i]=(unsigned char)body[i];*size=count;return status;
}

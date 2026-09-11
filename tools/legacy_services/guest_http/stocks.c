#include "stocks.h"
/* Bounded ASCII XML dialect shared in shape with Weather; kept freestanding. */
struct parser {const unsigned char *s;unsigned n,p;};
struct attributes {char id[257],type[257],api[257],apiver[257],version[257],encoding[257];unsigned count;};
static int same(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static int ws(unsigned c){return c==32||c==9||c==10||c==13;}
static void spaces(struct parser*p){while(p->p<p->n&&ws(p->s[p->p]))p->p++;}
static int take(struct parser*p,const char*s){unsigned i=p->p;while(*s){if(i==p->n||p->s[i++]!=(unsigned char)*s++)return 0;}p->p=i;return 1;}
static int namechar(unsigned c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c==':'||c=='-'||c=='.';}
static int name(struct parser*p,char*out){unsigned n=0;while(p->p<p->n&&namechar(p->s[p->p])){if(n==31)return 0;out[n++]=(char)p->s[p->p++];}out[n]=0;return n&&!(out[0]>='0'&&out[0]<='9')&&out[0]!='-'&&out[0]!='.';}
/* Decode entity references once; decoded '<' never becomes XML markup. */
static int character(struct parser*p,unsigned *out){
 if(p->p==p->n)return 0;
 unsigned c=p->s[p->p++];
 if(c=='&'){
  if(take(p,"amp;"))c='&';else if(take(p,"lt;"))c='<';else if(take(p,"gt;"))c='>';else if(take(p,"quot;"))c='"';else if(take(p,"apos;"))c='\'';
  else if(take(p,"#")){unsigned base=10,n=0,v=0;if(take(p,"x"))base=16;while(p->p<p->n&&p->s[p->p]!=';'){unsigned d=p->s[p->p++];if(d>='0'&&d<='9')d-='0';else if(base==16&&d>='a'&&d<='f')d=d-'a'+10;else if(base==16&&d>='A'&&d<='F')d=d-'A'+10;else return 0;if(d>=base||(base==16?v>7:(v>12||(v==12&&d>7))))return 0;v=v*base+d;n++;}if(!n||!take(p,";"))return 0;c=v;}
  else return 0;
 }
 if((c<32&&!ws(c))||c>126)return 0;
 *out=c;return 1;
}
static int value(struct parser*p,char*out,unsigned cap,unsigned delimiter){
 unsigned n=0;
 while(p->p<p->n&&p->s[p->p]!=delimiter){if(p->s[p->p]=='<')return 0;unsigned c;if(!character(p,&c)||n+1>=cap)return 0;out[n++]=(char)c;}
 out[n]=0;return p->p<p->n;
}
static void copy(char*out,const char*in){while((*out++=*in++)){} }
static int open(struct parser*p,const char*expected,struct attributes*a,int declaration){
 spaces(p);if(!take(p,declaration?"<?":"<"))return 0;char tag[32];if(!name(p,tag)||!same(tag,expected))return 0;
 char seen[16][32];unsigned count=0;*a=(struct attributes){0};
 while(p->p<p->n){unsigned before=p->p;spaces(p);if(take(p,declaration?"?>":">")){a->count=count;return 1;}if(before==p->p||count==16)return 0;
  char key[32],v[257];if(!name(p,key))return 0;if(declaration&&!same(key,"version")&&!same(key,"encoding"))return 0;for(unsigned i=0;i<count;i++)if(same(key,seen[i]))return 0;copy(seen[count++],key);
  spaces(p);if(!take(p,"="))return 0;spaces(p);if(p->p==p->n)return 0;unsigned quote=p->s[p->p++];if(quote!='\''&&quote!='"')return 0;
  if(!value(p,v,sizeof v,quote))return 0;
  p->p++;
  if(same(key,"id"))copy(a->id,v);else if(same(key,"type"))copy(a->type,v);else if(same(key,"api"))copy(a->api,v);else if(same(key,"apiver"))copy(a->apiver,v);else if(same(key,"version"))copy(a->version,v);else if(same(key,"encoding"))copy(a->encoding,v);
 }
 return 0;
}
static int close(struct parser*p,const char*expected){spaces(p);if(!take(p,"</"))return 0;char tag[32];if(!name(p,tag)||!same(tag,expected))return 0;spaces(p);return take(p,">");}
static int leaf(struct parser*p,const char*tag,char*out){
 struct attributes a;if(!open(p,tag,&a,0)||a.count)return 0;
 /* Text allows decoded '<', but never an unescaped child element. */
 unsigned n=0;while(p->p<p->n&&p->s[p->p]!='<'){if(p->p+2<p->n&&p->s[p->p]==']'&&p->s[p->p+1]==']'&&p->s[p->p+2]=='>')return 0;unsigned c;if(!character(p,&c)||n==OWN_STOCKS_MAX_TEXT)return 0;out[n++]=(char)c;}
 out[n]=0;if(!close(p,tag))return 0;unsigned first=0;while(first<n&&ws((unsigned char)out[first]))first++;while(n>first&&ws((unsigned char)out[n-1]))n--;for(unsigned i=first;i<n;i++)out[i-first]=out[i];out[n-first]=0;return 1;
}
struct stock {const char *symbol,*price,*change,*percent;};
/* Deliberately fixed examples, not historical or live quotes. */
static const struct stock demo[]={
 {"^DJI","13500.00","25.00","0.19"},
 {"AAPL","120.00","1.25","1.05"},
 {"GOOG","500.00","-2.50","-0.50"},
 {"YHOO","25.00","0.00","0.00"},
 {"T","40.00","0.20","0.50"}
};
struct writer {unsigned char*out;unsigned cap,n;int ok;};
static void append(struct writer*w,const char*s){while(*s){if(w->n==w->cap){w->ok=0;return;}w->out[w->n++]=(unsigned char)*s++;}}
static void field(struct writer*w,const char*tag,const char*value){append(w,"<");append(w,tag);append(w,">");append(w,value);append(w,"</");append(w,tag);append(w,">");}
static int query_id(const char*s){unsigned n=0,value=0;if(!*s)return 0;for(;*s;s++){if(*s<'0'||*s>'9'||++n>10)return 0;unsigned digit=(unsigned)(*s-'0');if(value>214748364u||(value==214748364u&&digit>7))return 0;value=value*10u+digit;}return 1;}
int own_stocks_request(const unsigned char*body,unsigned bytes,unsigned char*output,unsigned capacity,unsigned*length){
 if(!length)return 0;
 *length=0;if(!body||!bytes||bytes>OWN_STOCKS_MAX_REQUEST||!output)return 0;
 struct parser p={body,bytes,0};struct attributes a;spaces(&p);
 if(p.p+5<=p.n&&p.s[p.p]=='<'&&p.s[p.p+1]=='?'){
  if(!open(&p,"xml",&a,1)||!same(a.version,"1.0")||(a.encoding[0]&&!same(a.encoding,"utf-8")&&!same(a.encoding,"UTF-8")))return 0;
 }
 if(!open(&p,"request",&a,0)||!same(a.api,"finance")||!same(a.apiver,"1.0.0"))return 0;
 if(!open(&p,"query",&a,0)||!same(a.type,"getquotes")||!query_id(a.id))return 0;
 char id[257];copy(id,a.id);
 if(!open(&p,"list",&a,0)||a.count)return 0;
 unsigned count=0,selected[5],n=0,seen=0;
 for(;;){
  spaces(&p);if(p.p+2<p.n&&p.s[p.p]=='<'&&p.s[p.p+1]=='/')break;
  char symbol[129];if(count==OWN_STOCKS_MAX_SYMBOLS||!leaf(&p,"symbol",symbol)||!symbol[0])return 0;count++;
  for(unsigned i=0;i<sizeof demo/sizeof demo[0];i++)if(same(symbol,demo[i].symbol)&&!(seen&(1u<<i))){selected[n++]=i;seen|=1u<<i;}
 }
 if(!count||!close(&p,"list")||!close(&p,"query")||!close(&p,"request"))return 0;
 spaces(&p);if(p.p!=p.n)return 0;
 struct writer w={output,capacity,0,1};append(&w,"<?xml version='1.0' encoding='utf-8'?>\n<response demo=\"true\" dataset=\"original-iphone-stocks-demo-v1\"><result id=\"");append(&w,id);append(&w,"\"><list>");
 for(unsigned i=0;i<n;i++){
  const struct stock*s=&demo[selected[i]];append(&w,"<quote>");field(&w,"symbol",s->symbol);field(&w,"price",s->price);field(&w,"change",s->change);field(&w,"changepercent",s->percent);field(&w,"status","0");field(&w,"link","http://127.0.0.1/");append(&w,"</quote>");
 }
 append(&w,"</list></result></response>");if(!w.ok)return 0;*length=w.n;return 1;
}

#include "http.h"
/* Our server supports bounded GET/HTTP1.x only, and always closes connections. */
static unsigned textlen(const char*s){unsigned n=0;while(s[n])n++;return n;}
static int match(const unsigned char*p,unsigned n,const char*s){unsigned m=textlen(s);if(n!=m)return 0;for(unsigned i=0;i<n;i++)if(p[i]!=(unsigned char)s[i])return 0;return 1;}
static int lower_equal(const unsigned char*p,unsigned n,const char*s){if(n!=textlen(s))return 0;for(unsigned i=0;i<n;i++){unsigned char c=p[i];if(c>='A'&&c<='Z')c+=32;if(c!=(unsigned char)s[i])return 0;}return 1;}
static int append(unsigned char*out,unsigned cap,unsigned*used,const char*s){unsigned n=textlen(s);if(*used>cap||n>cap-*used)return 0;for(unsigned i=0;i<n;i++)out[(*used)++]=(unsigned char)s[i];return 1;}
static int decimal(unsigned value,char*out){char reverse[12];unsigned n=0;do{reverse[n++]=(char)('0'+value%10);value/=10;}while(value);for(unsigned i=0;i<n;i++)out[i]=reverse[n-1-i];out[n]=0;return 1;}
static int hex(unsigned char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static int echo_value(const unsigned char*p,unsigned n,char*out){unsigned used=0,decoded=0;for(unsigned i=0;i<n;i++){unsigned char c=p[i];if(c=='+')c=' ';else if(c=='%'){if(n-i<3)return 0;int a=hex(p[i+1]),b=hex(p[i+2]);if(a<0||b<0)return 0;c=(unsigned char)(a*16+b);i+=2;}if(c<32||c>126||++decoded>32)return 0;const char*escaped=0;switch(c){case '&':escaped="&amp;";break;case '<':escaped="&lt;";break;case '>':escaped="&gt;";break;case '"':escaped="&quot;";break;case '\'':escaped="&#39;";break;}
 if(escaped){for(unsigned j=0;escaped[j];j++)out[used++]=escaped[j];}else out[used++]=(char)c;}
 out[used]=0;return 1;}
int own_http_response(const unsigned char*req,unsigned size,unsigned char*out,unsigned cap,struct http_result*result){
 if(!req||!out||!result||size>2048)return 0;unsigned line=0;while(line+1<size&&!(req[line]=='\r'&&req[line+1]=='\n'))line++;int status=400;const char*reason="Bad Request",*route="invalid";unsigned target=0,targetn=0;int valid=0;
 if(line+1<size&&line<=256){unsigned space=0;while(space<line&&req[space]!=' ')space++;unsigned end=space+1;while(end<line&&req[end]!=' ')end++;if(space>0&&end<line&&end>space+1&&(match(req+end+1,line-end-1,"HTTP/1.0")||match(req+end+1,line-end-1,"HTTP/1.1"))){valid=1;target=space+1;targetn=end-target;for(unsigned i=0;i<line;i++)if(req[i]<32||req[i]>126)valid=0;int host=0,content=0;unsigned pos=line+2;
 while(valid&&pos<size){unsigned e=pos;while(e+1<size&&!(req[e]=='\r'&&req[e+1]=='\n'))e++;if(e+1>=size){valid=0;break;}if(e==pos){if(e+2!=size)valid=0;pos=e+2;break;}unsigned colon=pos;while(colon<e&&req[colon]!=':')colon++;if(colon==pos||colon-pos>64||colon==e){valid=0;break;}for(unsigned i=pos;i<colon;i++)if(!((req[i]>='a'&&req[i]<='z')||(req[i]>='A'&&req[i]<='Z')||(req[i]>='0'&&req[i]<='9')||req[i]=='-'))valid=0;
 unsigned v=colon+1;while(v<e&&(req[v]==' '||req[v]=='\t'))v++;unsigned ve=e;while(ve>v&&(req[ve-1]==' '||req[ve-1]=='\t'))ve--;for(unsigned i=v;i<ve;i++)if((req[i]<32&&req[i]!='\t')||req[i]>126)valid=0;
 if(lower_equal(req+pos,colon-pos,"host")){if(host++||!(match(req+v,ve-v,"127.0.0.1:18081")||match(req+v,ve-v,"127.0.0.1")))valid=0;}
 if(lower_equal(req+pos,colon-pos,"content-length")){if(content++||!match(req+v,ve-v,"0"))valid=0;}
 if(lower_equal(req+pos,colon-pos,"transfer-encoding"))valid=0;pos=e+2;}
 if(pos!=size||size<4||req[size-4]!='\r'||req[size-3]!='\n'||req[size-2]!='\r'||req[size-1]!='\n')valid=0;
 if(match(req+end+1,line-end-1,"HTTP/1.1")&&!host)valid=0;
 if(valid&&!match(req,space,"GET")){status=405;reason="Method Not Allowed";route="method";valid=0;}
 }}
 unsigned char body[2048];unsigned used=0;char value[193];
 if(valid){status=200;reason="OK";route="home";if(match(req+target,targetn,"/")){
 if(!append(body,sizeof body,&used,"<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=320, initial-scale=1\"><title>iPhone Local Demo</title></head><body><h1>Local demo</h1><p>This page is served inside this emulated phone. It is fixed demo content, not an internet service.</p><p>Page: HOME-1</p><p><a href=\"/next\">Open next page</a></p><form action=\"/echo\" method=\"get\"><label>Message <input name=\"message\" maxlength=\"32\" size=\"12\"></label><input type=\"submit\" value=\"Send\"></form></body></html>"))return 0;
 }else if(match(req+target,targetn,"/next")){route="next";if(!append(body,sizeof body,&used,"<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=320, initial-scale=1\"><title>Local Demo Next</title></head><body><h1>Next page</h1><p>Local demo: fixed content served inside this emulated phone.</p><p>Page: NEXT-1</p><p>Original Safari followed a local link.</p><p><a href=\"/\">Back to demo</a></p></body></html>"))return 0;
 }else if(targetn>=14&&match(req+target,14,"/echo?message=")){route="echo";if(!echo_value(req+target+14,targetn-14,value)){status=400;reason="Bad Request";route="bad-query";}else if(!append(body,sizeof body,&used,"<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=320, initial-scale=1\"><title>Local Demo Form</title></head><body><h1>Form received</h1><p>Local demo: fixed content served inside this emulated phone.</p><p>Page: ECHO-1</p><p>Message: ")||!append(body,sizeof body,&used,value)||!append(body,sizeof body,&used,"</p><p><a href=\"/\">Back to demo</a></p></body></html>"))return 0;
 }else{status=404;reason="Not Found";route="missing";}}
 if(status!=200){used=0;if(!append(body,sizeof body,&used,"<!DOCTYPE html><html><head><title>Local demo error</title></head><body><h1>")||!append(body,sizeof body,&used,reason)||!append(body,sizeof body,&used,"</h1><p>This bounded local demo supports only its documented pages.</p></body></html>"))return 0;}
 char digits[12];decimal(used,digits);unsigned total=0;char code[12];decimal((unsigned)status,code);
 if(!append(out,cap,&total,"HTTP/1.0 ")||!append(out,cap,&total,code)||!append(out,cap,&total," ")||!append(out,cap,&total,reason)||!append(out,cap,&total,"\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: ")||!append(out,cap,&total,digits)||!append(out,cap,&total,"\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n")||used>cap-total)return 0;
 for(unsigned i=0;i<used;i++)out[total++]=body[i];result->length=total;result->status=status;result->route=route;return 1;
}

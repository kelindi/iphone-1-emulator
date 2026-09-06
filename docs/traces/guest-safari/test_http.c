#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"http:%d %s\n",__LINE__,#x);exit(1);}}while(0)
static void check(const char*request,int expected,const char*route,const char*fragment){
 unsigned char buffer[4098];memset(buffer,0xa5,sizeof buffer);struct http_result r={0};CHECK(own_http_response((const unsigned char*)request,(unsigned)strlen(request),buffer+1,4096,&r));CHECK(r.status==expected&&!strcmp(r.route,route));CHECK(buffer[0]==0xa5&&buffer[4097]==0xa5&&r.length<4096);buffer[1+r.length]=0;
 char*s=(char*)buffer+1;unsigned status=0,n=0;CHECK(sscanf(s,"HTTP/1.0 %u",&status)==1&&status==(unsigned)expected);CHECK(sscanf(strstr(s,"Content-Length:"),"Content-Length: %u",&n)==1);char*body=strstr(s,"\r\n\r\n");CHECK(body&&n==r.length-(unsigned)(body+4-s));CHECK(strstr(s,"Connection: close\r\n")&&strstr(s,"Cache-Control: no-store\r\n"));if(fragment)CHECK(strstr(body+4,fragment));
}
int main(void){
 check("GET / HTTP/1.0\r\n\r\n",200,"home","HOME-1");
 check("GET /next HTTP/1.1\r\nHost: 127.0.0.1:18081\r\n\r\n",200,"next","NEXT-1");
 check("GET /echo?message=hi HTTP/1.1\r\nhOSt: 127.0.0.1:18081\r\nContent-Length: 0\r\n\r\n",200,"echo","Message: hi");
 check("GET /echo?message=%3C%26%22%27%3E+ok HTTP/1.0\r\n\r\n",200,"echo","&lt;&amp;&quot;&#39;&gt; ok");
 check("GET /echo?message= HTTP/1.0\r\n\r\n",200,"echo","Message: </p>");
 check("GET /favicon.ico HTTP/1.0\r\n\r\n",404,"missing","Not Found");
 check("GET http://example.test/ HTTP/1.0\r\n\r\n",404,"missing",0);
 check("POST / HTTP/1.0\r\n\r\n",405,"method",0);
 const char*invalid[]={"GET / HTTP/1.1\r\n\r\n","GET / HTTP/1.1\r\nHost: example.test\r\n\r\n","GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nHost: 127.0.0.1\r\n\r\n","GET / HTTP/1.0\r\nContent-Length: 1\r\n\r\nx","GET / HTTP/1.0\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n","GET / HTTP/1.0\r\nTransfer-Encoding: chunked\r\n\r\n","GET / HTTP/1.0\r\n Bad: fold\r\n\r\n","GET / HTTP/1.0\r\nNoColon\r\n\r\n","GET / HTTP/1.0\r\n\r\nGET /next HTTP/1.0\r\n\r\n","GET / HTTP/1.0\n\n","GET / HTTP/2.0\r\n\r\n","","G\r\n\r\n"};
 for(unsigned i=0;i<sizeof invalid/sizeof*invalid;i++)check(invalid[i],400,"invalid",0);
 const char*badquery[]={"%","%0","%gg","%00","%0a","%ff","123456789012345678901234567890123"};
 for(unsigned i=0;i<sizeof badquery/sizeof*badquery;i++){char r[256];snprintf(r,sizeof r,"GET /echo?message=%s HTTP/1.0\r\n\r\n",badquery[i]);if(i==6)continue;check(r,400,"bad-query",0);}
 check("GET /echo?message=12345678901234567890123456789012 HTTP/1.0\r\n\r\n",200,"echo","12345678901234567890123456789012");
 check("GET /echo?message=123456789012345678901234567890123 HTTP/1.0\r\n\r\n",400,"bad-query",0);
 const unsigned char good[]="GET / HTTP/1.0\r\n\r\n";unsigned char out[4098];
 for(unsigned cap=0;cap<4097;cap++){memset(out,0xa5,sizeof out);struct http_result r={99,88,(const char*)77},before=r;int ok=own_http_response(good,sizeof good-1,out+1,cap,&r);CHECK(out[0]==0xa5&&out[cap+1]==0xa5);if(ok)CHECK(r.length<=cap&&r.status==200);else CHECK(r.length==before.length&&r.status==before.status&&r.route==before.route);}
 unsigned char random[2050];unsigned seed=1;for(unsigned n=0;n<2050;n++){for(unsigned i=0;i<n;i++){seed=seed*1664525u+1013904223u;random[i]=(unsigned char)(seed>>24);}memset(out,0xa5,sizeof out);struct http_result r={0};int ok=own_http_response(random,n,out+1,4096,&r);CHECK(out[0]==0xa5&&out[4097]==0xa5);if(n>2048)CHECK(!ok);else if(ok)CHECK(r.length<=4096);}
 printf("PASS %u HTTP framing/route/escaping/capacity/random-input checks; host-only\n",checks);
}

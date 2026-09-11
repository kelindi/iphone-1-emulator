#include "weather_http.h"
#include "weather_request.h"
#include "http.h"
#include "stocks.h"
#include "youtube_auth.h"
#include "youtube_catalog.h"
#ifndef OWN_YOUTUBE_DEMO
#define OWN_YOUTUBE_DEMO 0
#endif
static unsigned len(const char*s){unsigned n=0;while(s[n])n++;return n;}
static unsigned lower(unsigned c){return c>='A'&&c<='Z'?c+32:c;}
static int eq(const unsigned char*p,unsigned n,const char*s,int folded){if(n!=len(s))return 0;for(unsigned i=0;i<n;i++)if((folded?lower(p[i]):p[i])!=(unsigned char)s[i])return 0;return 1;}
static int token(unsigned c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_';}
/* Exactly one known routing discriminator; the historical IMEI is opaque and
 * not retained by this layer. No alternate host or absolute-form proxy URLs. */
static int target(const unsigned char*p,unsigned n){
 if(eq(p,n,"/youtube/accounts/applelogin1",0))return 3;
 if(eq(p,n,"/youtube/accounts/applelogin2",0))return 4;
 if(n==4&&eq(p,n,"/dgw",0))return 1;
 if(n<6||!eq(p,5,"/dgw?",0))return 0;
 unsigned pos=5,service=0,imei=0,time_parameter=0;
 while(pos<n){unsigned start=pos;while(pos<n&&p[pos]!='&')pos++;unsigned end=pos,k=start;while(k<end&&p[k]!='=')k++;if(k==end||k+1==end)return 0;
  if(eq(p+start,k-start,"apptype",0)){if(service)return 0;if(eq(p+k+1,end-k-1,"weather",0))service=1;else if(eq(p+k+1,end-k-1,"finance",0))service=2;else return 0;}
  else if(eq(p+start,k-start,"imei",0)){if(imei++||end-k-1>128)return 0;for(unsigned j=k+1;j<end;j++)if(p[j]<33||p[j]>126||p[j]=='#')return 0;}
  else if(eq(p+start,k-start,"t",0)){
   if(time_parameter++)return 0;
   unsigned value=0;
   for(unsigned j=k+1;j<end;j++){
    if(p[j]<'0'||p[j]>'9')return 0;
    unsigned digit=p[j]-'0';
    if(value>429496729u||(value==429496729u&&digit>5))return 0;
    value=value*10+digit;
   }
  }
  else return 0;
  if(pos<n&&++pos==n)return 0;
 }
 return time_parameter&&service!=1?0:(int)service;
}
static int get_target(const unsigned char*p,unsigned n){
 return eq(p,n,"/",0)||eq(p,n,"/next",0)||(n>=14&&eq(p,14,"/echo?message=",0));
}
int own_weather_http_frame(const unsigned char*p,unsigned n,unsigned*offset,unsigned*body){
 if(offset)*offset=0;
 if(body)*body=0;
 if(!p||!offset||!body||n>OWN_WEATHER_WIRE_CAP)return -1;
 unsigned end=0;
 for(unsigned i=0;i<n&&i<OWN_WEATHER_HEADER_CAP;i++){
  unsigned c=p[i];if(c==0||c==127||(c<32&&c!='\r'&&c!='\n'&&c!='\t'))return -1;
  if(c=='\n'&&(i==0||p[i-1]!='\r'))return -1;
  if(i>=3&&p[i-3]=='\r'&&p[i-2]=='\n'&&p[i-1]=='\r'&&p[i]=='\n'){end=i+1;break;}
 }
 if(!end)return n>=OWN_WEATHER_HEADER_CAP?-1:0;
 unsigned line=0;while(line+1<end&&!(p[line]=='\r'&&p[line+1]=='\n'))line++;
 int head=line>=5&&eq(p,5,"HEAD ",0);
 int get=head||(line>=4&&eq(p,4,"GET ",0));
 if(line<14||(!get&&!eq(p,5,"POST ",0)))return -1;
 unsigned start_target=head?5:get?4:5;
 unsigned split=start_target;while(split<line&&p[split]!=' ')split++;
 int yt=own_youtube_target(p+start_target,split-start_target);
 if(head&&yt!=2)return -1;
 if(split==line||!(get?(get_target(p+start_target,split-start_target)||yt):target(p+start_target,split-start_target))||
    !(eq(p+split+1,line-split-1,"HTTP/1.0",0)||eq(p+split+1,line-split-1,"HTTP/1.1",0)))return -1;
 int auth=!get&&target(p+start_target,split-start_target)>=3;
 if(get&&(end>2048||line>256))return -1;
 unsigned pos=line+2,count=0,size=0,ctype=0,host=0;
 while(pos<end-2){unsigned start=pos;while(pos+1<end&&!(p[pos]=='\r'&&p[pos+1]=='\n'))pos++;unsigned stop=pos,k=start;pos+=2;
  while(k<stop&&token(p[k]))k++;
  if(k==start||k>=stop||p[k]!=':')return -1;
  unsigned val=k+1;while(val<stop&&(p[val]==' '||p[val]=='\t'))val++;while(stop>val&&(p[stop-1]==' '||p[stop-1]=='\t'))stop--;
  for(unsigned j=val;j<stop;j++)if(p[j]<32&&p[j]!='\t')return -1;
  if(eq(p+start,k-start,"content-length",1)){if(count++||val==stop)return -1;for(unsigned j=val;j<stop;j++){if(p[j]<'0'||p[j]>'9')return -1;unsigned digit=p[j]-'0';if(size>(OWN_WEATHER_MAX_REQUEST-digit)/10)return -1;size=size*10+digit;}}
  else if(eq(p+start,k-start,"transfer-encoding",1)||eq(p+start,k-start,"expect",1))return -1;
  else if(eq(p+start,k-start,"content-type",1)){if(get||ctype++||!(auth?eq(p+val,stop-val,"application/x-www-form-urlencoded",1):(eq(p+val,stop-val,"text/xml",1)||eq(p+val,stop-val,"text/xml; charset=utf-8",1))))return -1;}
  else if(eq(p+start,k-start,"host",1)){if(host++||!(get?(yt==1?(eq(p+val,stop-val,"gdata.youtube.com",1)||eq(p+val,stop-val,"gdata.youtube.com:80",1)):eq(p+val,stop-val,"127.0.0.1",0)):(auth?(eq(p+val,stop-val,"dm5.google.com",1)||eq(p+val,stop-val,"dm5.google.com:80",1)):(eq(p+val,stop-val,"iphone-wu.apple.com",1)||eq(p+val,stop-val,"iphone-wu.apple.com:80",1)))))return -1;}
 }
 if(get){if(size||host!=1)return -1;}
 else if(count!=1||!size||ctype!=1||host!=1)return -1;
 if(n>end+size)return -1;
 if(n<end+size)return 0;
 *offset=end;*body=size;return 1;
}
static int append(unsigned char*out,unsigned cap,unsigned*n,const char*s){unsigned k=len(s);if(k>cap-*n)return 0;for(unsigned i=0;i<k;i++)out[(*n)++]=(unsigned char)s[i];return 1;}
int own_weather_http_response(const unsigned char*p,unsigned n,unsigned char*out,unsigned cap,unsigned*length){
 if(length)*length=0;
 if(!out||!length||cap<256)return 0;
 unsigned off=0,body=0,size=0;if(own_weather_http_frame(p,n,&off,&body)!=1)return 0;
 if(body==0){unsigned a=eq(p,5,"HEAD ",0)?5:4,b=a;while(b<n&&p[b]!=' ')b++;if(own_youtube_target(p+a,b-a))return own_youtube_get_response(p,n,out,cap,length);struct http_result result;if(!own_http_response(p,n,out,cap,&result))return 0;*length=result.length;return 1;}
 unsigned split=5;while(split<off&&p[split]!=' ')split++;
 int service=target(p+5,split-5);
 /* Reserve bounded header space, then move generated XML behind exact header. */
 unsigned auth_status=0;
 int ok=service>=3?(auth_status=own_youtube_auth_response((unsigned)service-2,p+off,body,out+256,cap-256,&size),auth_status==200):service==2?own_stocks_request(p+off,body,out+256,cap-256,&size):own_weather_request(p+off,body,out+256,cap-256,&size);
 if(service>=3&&!auth_status)return 0;
 unsigned used=0;if(!append(out,cap,&used,auth_status==503?"HTTP/1.0 503 Service Unavailable\r\n":ok?"HTTP/1.0 200 OK\r\n":"HTTP/1.0 400 Bad Request\r\n"))return 0;
 if(!append(out,cap,&used,service>=3?"Content-Type: text/plain; charset=utf-8\r\nConnection: close\r\nCache-Control: no-store\r\nX-iPhone1-Demo: ":"Content-Type: text/xml; charset=utf-8\r\nConnection: close\r\nCache-Control: no-store\r\nX-iPhone1-Demo: "))return 0;
 if(!append(out,cap,&used,service>=3?(OWN_YOUTUBE_DEMO?"youtube-local-compatibility\r\nContent-Length: ":"youtube-auth-probe\r\nContent-Length: "):service==2?"fixed-stocks\r\nContent-Length: ":"fixed-weather\r\nContent-Length: "))return 0;
 char digits[12];unsigned nd=0,v=size;do{digits[nd++]=(char)('0'+v%10);v/=10;}while(v);while(nd){if(used>=cap)return 0;out[used++]=(unsigned char)digits[--nd];}
 if(!append(out,cap,&used,"\r\n\r\n")||used>256||size>cap-used)return 0;
 for(unsigned i=0;i<size;i++)out[used+i]=out[256+i];
 *length=used+size;return 1;
}

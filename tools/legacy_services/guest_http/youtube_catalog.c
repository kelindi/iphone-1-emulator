#include "youtube_catalog.h"
#if OWN_YOUTUBE_DEMO
#include "youtube_assets.h"
static unsigned length(const char*s){unsigned n=0;while(s[n])n++;return n;}
static int equal(const unsigned char*p,unsigned n,const char*s){if(n!=length(s))return 0;for(unsigned i=0;i<n;i++)if(p[i]!=(unsigned char)s[i])return 0;return 1;}
static int folded(const unsigned char*p,unsigned n,const char*s){if(n!=length(s))return 0;for(unsigned i=0;i<n;i++){unsigned c=p[i];if(c>='A'&&c<='Z')c+=32;if(c!=(unsigned char)s[i])return 0;}return 1;}
static int number(const unsigned char*p,unsigned n,unsigned*v){*v=0;if(!n)return 0;for(unsigned i=0;i<n;i++){if(p[i]<'0'||p[i]>'9')return 0;unsigned d=p[i]-'0';if(*v>429496729u||(*v==429496729u&&d>5))return 0;*v=*v*10+d;}return 1;}
static int feed(const unsigned char*p,unsigned n){
 unsigned end=0;while(end<n&&p[end]!='?')end++;
 if(!equal(p,end,"/feeds/standardfeeds/recently_featured")&&!equal(p,end,"/feeds/standardfeeds/most_viewed")&&!equal(p,end,"/feeds/standardfeeds/top_rated")&&!equal(p,end,"/feeds/videos/iphone1-demo-1"))return 0;
 if(end==n)return 1;
 unsigned pos=end+1,seen=0;if(pos==n)return 0;
 while(pos<n){unsigned start=pos;while(pos<n&&p[pos]!='&')pos++;unsigned stop=pos,k=start;while(k<stop&&p[k]!='=')k++;if(k==stop)return 0;unsigned bit=0,v=0;
  if(equal(p+start,k-start,"start-index")){bit=1;if(!number(p+k+1,stop-k-1,&v)||v!=1)return 0;}
  else if(equal(p+start,k-start,"max-results")){bit=2;if(!number(p+k+1,stop-k-1,&v)||!v||v>100)return 0;}
  else if(equal(p+start,k-start,"format")){bit=4;if(!equal(p+k+1,stop-k-1,"2,3")&&!equal(p+k+1,stop-k-1,"2%2C3")&&!equal(p+k+1,stop-k-1,"2%2c3"))return 0;}
  else if(equal(p+start,k-start,"time")){bit=8;if(!equal(p+k+1,stop-k-1,"today")&&!equal(p+k+1,stop-k-1,"this_week")&&!equal(p+k+1,stop-k-1,"this_month")&&!equal(p+k+1,stop-k-1,"all_time"))return 0;}
  else return 0;
  if(seen&bit)return 0;
  seen|=bit;if(pos<n&&++pos==n)return 0;
 }return 1;
}
int own_youtube_target(const unsigned char*p,unsigned n){if(!p||n>512)return 0;if(equal(p,n,"/demo/clip-1.mp4")||equal(p,n,"/demo/clip-1.jpg"))return 2;return feed(p,n);}
static const unsigned char catalog[]=
"<?xml version=\"1.0\" encoding=\"UTF-8\"?><feed xmlns=\"http://www.w3.org/2005/Atom\" xmlns:media=\"http://search.yahoo.com/mrss/\" xmlns:gd=\"http://schemas.google.com/g/2005\" xmlns:yt=\"http://gdata.youtube.com/schemas/2007\" xmlns:openSearch=\"http://a9.com/-/spec/opensearchrss/1.0/\">"
"<openSearch:startIndex>1</openSearch:startIndex><openSearch:totalResults>1</openSearch:totalResults><openSearch:itemsPerPage>1</openSearch:itemsPerPage>"
"<entry><id>iphone1-demo-1</id><title>Local demo: five seconds</title><author><name>iphone-1 local demo</name></author><media:group>"
"<media:thumbnail url=\"http://127.0.0.1/demo/clip-1.jpg\"/><media:content url=\"http://127.0.0.1/demo/clip-1.mp4\" format=\"2\" duration=\"5\"/><media:content url=\"http://127.0.0.1/demo/clip-1.mp4\" format=\"3\" duration=\"5\"/>"
"<media:description type=\"plain\">An independently authored local clip.</media:description></media:group><gd:rating min=\"1\" max=\"5\" average=\"5\" numRaters=\"1\"/><yt:statistics viewCount=\"1\"/></entry></feed>";
struct writer{unsigned char*p;unsigned n,cap;int ok;};
static void add(struct writer*w,const char*s){unsigned n=length(s);if(n>w->cap-w->n){w->ok=0;return;}for(unsigned i=0;i<n;i++)w->p[w->n++]=(unsigned char)s[i];}
static void decimal(struct writer*w,unsigned v){char a[11];unsigned n=0;do{a[n++]=(char)('0'+v%10);v/=10;}while(v);if(n>w->cap-w->n){w->ok=0;return;}while(n)w->p[w->n++]=(unsigned char)a[--n];}
/* Strict single byte range; all malformed, overflow and unsatisfiable forms
 * return416 rather than attempting multipart or interpreting trailing bytes. */
static int range(const unsigned char*p,unsigned n,unsigned total,unsigned*first,unsigned*last){
 if(n<7||!equal(p,6,"bytes="))return 0;
 unsigned dash=6;while(dash<n&&p[dash]!='-')dash++;if(dash==n)return 0;
 unsigned a=0,b=0;
 if(dash==6){if(!number(p+dash+1,n-dash-1,&b)||!b||!total)return 0;*first=b>=total?0:total-b;*last=total-1;return 1;}
 if(!number(p+6,dash-6,&a)||a>=total)return 0;
 if(dash+1==n)b=total-1;else if(!number(p+dash+1,n-dash-1,&b)||b<a)return 0;
 *first=a;*last=b>=total?total-1:b;return 1;
}
int own_youtube_get_response(const unsigned char*p,unsigned n,unsigned char*out,unsigned cap,unsigned*size){
 if(size)*size=0;
 if(!p||!out||!size||n>4096)return 0;
 int head=n>=5&&equal(p,5,"HEAD ");unsigned start=head?5:4;if(!head&&(n<4||!equal(p,4,"GET ")))return 0;
 unsigned end=start;while(end<n&&p[end]!=' ')end++;if(end==n)return 0;int kind=own_youtube_target(p+start,end-start);if(!kind)return 0;
 const unsigned char*data=catalog;unsigned total=sizeof catalog-1;const char*type="text/xml; charset=utf-8";
 if(kind==2){if(equal(p+start,end-start,"/demo/clip-1.mp4")){data=own_demo_mp4;total=sizeof own_demo_mp4;type="video/mp4";}else{data=own_demo_jpg;total=sizeof own_demo_jpg;type="image/jpeg";}}
 unsigned pos=end;while(pos+1<n&&!(p[pos]=='\r'&&p[pos+1]=='\n'))pos++;pos+=2;const unsigned char*rp=0;unsigned rn=0,count=0;int authorized=0;
 while(pos+1<n&&p[pos]!='\r'){unsigned a=pos;while(pos+1<n&&!(p[pos]=='\r'&&p[pos+1]=='\n'))pos++;unsigned stop=pos,k=a;pos+=2;while(k<stop&&p[k]!=':')k++;unsigned v=k+1;while(v<stop&&(p[v]==' '||p[v]=='\t'))v++;while(stop>v&&(p[stop-1]==' '||p[stop-1]=='\t'))stop--;
  if(folded(p+a,k-a,"range")){count++;rp=p+v;rn=stop-v;}
  if(folded(p+a,k-a,"authorization")){if(authorized)return 0;authorized=equal(p+v,stop-v,"GoogleLogin auth=iphone1-local-demo-token");if(!authorized)return 0;}
 }
 if(kind==1&&!authorized)return 0;
 unsigned first=0,last=total?total-1:0,status=200;
 if(count){if(kind!=2||count!=1||!range(rp,rn,total,&first,&last))status=416;else status=206;}
 unsigned bytes=status==416?0:last-first+1;
 struct writer w={out,0,cap,1};add(&w,status==416?"HTTP/1.0 416 Range Not Satisfiable\r\n":status==206?"HTTP/1.0 206 Partial Content\r\n":"HTTP/1.0 200 OK\r\n");add(&w,"Connection: close\r\nCache-Control: no-store\r\nX-iPhone1-Demo: youtube-local-compatibility\r\nContent-Type: ");add(&w,type);add(&w,"\r\nAccept-Ranges: bytes\r\n");
 if(status!=200){add(&w,"Content-Range: bytes ");if(status==416)add(&w,"*/");else{decimal(&w,first);add(&w,"-");decimal(&w,last);add(&w,"/");}decimal(&w,total);add(&w,"\r\n");}
 add(&w,"Content-Length: ");decimal(&w,bytes);add(&w,"\r\n\r\n");if(!w.ok||(!head&&bytes>w.cap-w.n))return 0;if(!head)for(unsigned i=0;i<bytes;i++)out[w.n++]=data[first+i];*size=w.n;return 1;
}
#else
int own_youtube_target(const unsigned char*p,unsigned n){(void)p;(void)n;return 0;}
int own_youtube_get_response(const unsigned char*p,unsigned n,unsigned char*out,unsigned cap,unsigned*size){(void)p;(void)n;(void)out;(void)cap;if(size)*size=0;return 0;}
#endif

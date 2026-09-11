#include "weather.h"
/* Dataset matches tools/legacy_services/weather.py; no libc/guest imports. */
static const char *const forecast[2]={
"<item><location id=\"USCA0273|12797509\" city=\"Cupertino\" /><astronomy sunrise=\"360\" sunset=\"1080\" moonfacevisible=\"0\" moonphase=\"0\" /><condition time=\"720\" temp=\"72\" code=\"32\" /><forecast high=\"74\" low=\"55\" code=\"32\" /><forecast high=\"73\" low=\"54\" code=\"32\" /><forecast high=\"75\" low=\"56\" code=\"32\" /><forecast high=\"76\" low=\"55\" code=\"32\" /><forecast high=\"72\" low=\"53\" code=\"32\" /><forecast high=\"71\" low=\"54\" code=\"32\" /></item>",
"<item><location id=\"USNY0996|2459115\" city=\"New York\" /><astronomy sunrise=\"360\" sunset=\"1080\" moonfacevisible=\"0\" moonphase=\"0\" /><condition time=\"720\" temp=\"64\" code=\"32\" /><forecast high=\"66\" low=\"50\" code=\"32\" /><forecast high=\"67\" low=\"52\" code=\"32\" /><forecast high=\"65\" low=\"49\" code=\"32\" /><forecast high=\"68\" low=\"51\" code=\"32\" /><forecast high=\"70\" low=\"54\" code=\"32\" /><forecast high=\"69\" low=\"53\" code=\"32\" /></item>",
};
static const char *const location[2]={
"<item><id>USCA0273|12797509</id><city>Cupertino</city><region>CA</region><regionname>California</regionname><country>US</country><countryname>United States</countryname></item>",
"<item><id>USNY0996|2459115</id><city>New York</city><region>NY</region><regionname>New York</regionname><country>US</country><countryname>United States</countryname></item>",
};
static const char *const identifiers[2]={"USCA0273|12797509","USNY0996|2459115"};
static const char *const names[2]={"cupertino","new york"};
static int equal(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static unsigned size(const char *s){unsigned n=0;if(!s)return 0;while(n<=OWN_WEATHER_MAX_TEXT&&s[n])n++;return n;}
static int space(char c){return c==' '||c=='\t'||c=='\r'||c=='\n';}
static char lower(char c){return c>='A'&&c<='Z'?(char)(c+'a'-'A'):c;}
struct writer {unsigned char *out;unsigned cap,n;int ok;};
static void append(struct writer *w,const char *s){while(*s){if(w->n==w->cap){w->ok=0;return;}w->out[w->n++]=(unsigned char)*s++;}}
static int response(const unsigned *cities,unsigned count,int search,unsigned char *output,unsigned capacity,unsigned *length){
 if(!length)return 0;
 *length=0;if(!output)return 0;
 struct writer w={output,capacity,0,1};
 append(&w,"<?xml version='1.0' encoding='utf-8'?>\n<response demo=\"true\" dataset=\"original-iphone-weather-demo-v1\"><result id=\"");
 append(&w,search?"3":"30");append(&w,"\"><list>");
 for(unsigned i=0;i<count;i++)append(&w,search?location[cities[i]]:forecast[cities[i]]);
 append(&w,"</list></result></response>");
 if(!w.ok)return 0;
 *length=w.n;return 1;
}
int own_weather_forecast(const char *const *ids,unsigned count,unsigned char *output,unsigned capacity,unsigned *length){
 if(!length)return 0;
 *length=0;
 if(!ids||!count||count>OWN_WEATHER_MAX_IDS)return 0;
 unsigned cities[2],n=0,seen=0;
 for(unsigned i=0;i<count;i++){
  unsigned len=size(ids[i]);if(!len||len>OWN_WEATHER_MAX_TEXT)return 0;
  for(unsigned j=0;j<2;j++)if(equal(ids[i],identifiers[j])&&!(seen&(1u<<j))){cities[n++]=j;seen|=1u<<j;}
 }
 return response(cities,n,0,output,capacity,length);
}
static int contains(const char *text,const char *part){for(;*text;text++){const char *a=text,*b=part;while(*a&&*b&&lower(*a)==*b){a++;b++;}if(!*b)return 1;}return 0;}
int own_weather_search(const char *phrase,unsigned char *output,unsigned capacity,unsigned *length){
 if(!length)return 0;
 *length=0;
 unsigned len=size(phrase);if(!len||len>OWN_WEATHER_MAX_TEXT)return 0;
 char normalized[OWN_WEATHER_MAX_TEXT+1];unsigned n=0;int pending=0;
 for(unsigned i=0;i<len;i++){unsigned char c=(unsigned char)phrase[i];if(space((char)c)){if(n)pending=1;continue;}if(c<32||c>126)return 0;if(pending){normalized[n++]=' ';pending=0;}normalized[n++]=lower((char)c);}
 normalized[n]=0;if(!n)return 0;
 unsigned cities[2],count=0;
 for(unsigned j=0;j<2;j++){
  char id[OWN_WEATHER_MAX_TEXT+1];unsigned k=0;while(identifiers[j][k]){id[k]=lower(identifiers[j][k]);k++;}id[k]=0;
  if(contains(names[j],normalized)||equal(id,normalized))cities[count++]=j;
 }
 return response(cities,count,1,output,capacity,length);
}

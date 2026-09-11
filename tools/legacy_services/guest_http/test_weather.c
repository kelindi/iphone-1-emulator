#include "weather.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned char out[4096];
static unsigned length;
static void terminate(void){assert(length<sizeof out);out[length]=0;}
static unsigned occurrences(const char *needle){unsigned n=0;const char *p=(char*)out;while((p=strstr(p,needle))){n++;p+=strlen(needle);}return n;}
int main(void){
 const char *ids[]={"USNY0996|2459115","USCA0273|12797509","USNY0996|2459115"};
 assert(own_weather_forecast(ids,3,out,sizeof out-1,&length));terminate();
 assert(occurrences("<item>")==2);assert(occurrences("<forecast ")==12);
 assert(strstr((char*)out,"New York")<strstr((char*)out,"Cupertino"));
 assert(strstr((char*)out,"demo=\"true\""));assert(strstr((char*)out,"temp=\"64\""));
 assert(strstr((char*)out,"sunrise=\"360\" sunset=\"1080\""));
 const char *unknown[]={"<bad>&\""};assert(own_weather_forecast(unknown,1,out,sizeof out-1,&length));terminate();assert(occurrences("<item>")==0);assert(!strstr((char*)out,"<bad>"));
 assert(own_weather_search("  NeW\t York \n",out,sizeof out-1,&length));terminate();assert(occurrences("<item>")==1);assert(strstr((char*)out,"<id>USNY0996|2459115</id>"));assert(strstr((char*)out,"<regionname>New York</regionname>"));
 assert(own_weather_search("usca0273|12797509",out,sizeof out-1,&length));terminate();assert(strstr((char*)out,"Cupertino"));
 assert(own_weather_search("unsupported",out,sizeof out-1,&length));terminate();assert(occurrences("<item>")==0);
 unsigned exact=length;assert(own_weather_search("unsupported",out,exact,&length));assert(length==exact);
 assert(!own_weather_search("unsupported",out,exact-1,&length));assert(length==0);
 char longtext[130];memset(longtext,'x',129);longtext[129]=0;
 assert(!own_weather_search(longtext,out,sizeof out,&length));assert(!own_weather_search(" \t",out,sizeof out,&length));assert(!own_weather_search("\xff",out,sizeof out,&length));
 assert(!own_weather_forecast(ids,0,out,sizeof out,&length));assert(!own_weather_forecast(ids,17,out,sizeof out,&length));assert(!own_weather_forecast(0,1,out,sizeof out,&length));
 const char *empty[]={0};assert(!own_weather_forecast(empty,1,out,sizeof out,&length));
 assert(!own_weather_search(0,out,sizeof out,&length));assert(!own_weather_search("ny",0,sizeof out,&length));assert(!own_weather_search("ny",out,sizeof out,0));
 puts("Weather body builder checks pass");return 0;
}

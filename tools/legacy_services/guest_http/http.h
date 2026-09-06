#ifndef OWN_HTTP_H
#define OWN_HTTP_H
/* Independent fixed local-demo HTTP protocol, no host proxy or file access. */
struct http_result { unsigned length; int status; const char *route; };
int own_http_response(const unsigned char *request,unsigned size,unsigned char *out,unsigned cap,struct http_result *result);
#endif

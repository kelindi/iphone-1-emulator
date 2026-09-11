#ifndef OWN_STOCKS_H
#define OWN_STOCKS_H
#define OWN_STOCKS_MAX_REQUEST 16384u
#define OWN_STOCKS_MAX_TEXT 128u
#define OWN_STOCKS_MAX_SYMBOLS 16u
/* Original finance/getquotes XML only. Fixed demo data; no chart/search/live
 * market support. Output is an XML body with explicit length, not a C string.
 * Return0 clears length for malformed/unsupported input or insufficient space.
 */
int own_stocks_request(const unsigned char *body,unsigned bytes,
                       unsigned char *output,unsigned capacity,unsigned *length);
#endif

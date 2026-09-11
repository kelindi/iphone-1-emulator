#ifndef OWN_WEATHER_HTTP_H
#define OWN_WEATHER_HTTP_H
#define OWN_WEATHER_HEADER_CAP 4096u
#define OWN_WEATHER_WIRE_CAP (OWN_WEATHER_HEADER_CAP + 16384u)
#define OWN_WEATHER_RESPONSE_CAP 32768u
/* 0 incomplete, 1 complete, -1 invalid. No bytes after declared body accepted.
 * Outputs are meaningful only on success; no C string input is required. */
int own_weather_http_frame(const unsigned char *, unsigned,
                           unsigned *body_offset, unsigned *body_length);
int own_weather_http_response(const unsigned char *, unsigned,
                              unsigned char *, unsigned, unsigned *length);
#endif

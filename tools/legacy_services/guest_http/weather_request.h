#ifndef OWN_WEATHER_REQUEST_H
#define OWN_WEATHER_REQUEST_H
#define OWN_WEATHER_MAX_REQUEST 16384u
/* Parse only the original OS1 gateway XML dialect and build a demo body.
 * No HTTP framing, declarations/DTDs, external entities or arbitrary XML.
 * Numeric entities are limited to ASCII, matching the bounded demo dataset.
 * Returns 0 for malformed/unsupported requests or insufficient output capacity.
 */
int own_weather_request(const unsigned char *body,unsigned bytes,
                        unsigned char *output,unsigned capacity,unsigned *length);
#endif

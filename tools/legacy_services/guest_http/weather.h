#ifndef OWN_WEATHER_H
#define OWN_WEATHER_H
/* Transport-independent fixed Weather dataset. The caller must validate the
 * original gateway XML and pass decoded text, not raw XML/HTTP fragments.
 * Output is XML body bytes (not NUL terminated); return 0 means no response.
 * Request ids are never reflected unless they exactly match a known city.
 * These are demo values, not live forecasts. Visible UI labeling is required.
 */
#define OWN_WEATHER_MAX_IDS 16u
#define OWN_WEATHER_MAX_TEXT 128u
int own_weather_forecast(const char *const *ids, unsigned count,
                         unsigned char *output, unsigned capacity, unsigned *length);
int own_weather_search(const char *phrase, unsigned char *output,
                       unsigned capacity, unsigned *length);
#endif

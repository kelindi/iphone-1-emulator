#ifndef IPHONE1_TEST_EMSCRIPTEN_H
#define IPHONE1_TEST_EMSCRIPTEN_H
/* Native API tests exercise the actual browser C frontend without an SDK.
 * This include directory belongs only to test_web_api, never a WASM build. */
#define EMSCRIPTEN_KEEPALIVE
static double emscripten_test_now_ms;
static inline double emscripten_get_now(void) { return emscripten_test_now_ms; }
#endif

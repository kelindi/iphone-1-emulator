/* Independently authored freestanding compiler support, not an SDK copy.
 * Clang lowers parser aggregate initialization to memset even with -fno-builtin.
 * Volatile byte stores prevent this loop becoming a recursive memset call.
 * No lazy libSystem stub or startup runtime is needed for this local definition. */
void *memset(void *destination, int value, __SIZE_TYPE__ count) {
    volatile unsigned char *bytes = destination;
    for (__SIZE_TYPE__ i = 0; i < count; ++i) bytes[i] = (unsigned char)value;
    return destination;
}

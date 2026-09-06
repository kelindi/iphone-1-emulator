/* Test equality in complete machine words, without alignment or aliasing
 * assumptions. Repeating one byte has the same representation in either
 * byte order. The tail never reads beyond a partial final RAM page. */
static uint8_t snap_page_class(const uint8_t *page, size_t len) {
    const uint8_t first = page[0];
    const uint64_t repeated = UINT64_C(0x0101010101010101) * first;
    size_t i = 1;
    while (len - i >= sizeof repeated) {
        uint64_t word;
        memcpy(&word, page + i, sizeof word);
        if (word != repeated) return 2;
        i += sizeof word;
    }
    for (; i < len; i++)
        if (page[i] != first) return 2;
    return first ? 1 : 0;
}


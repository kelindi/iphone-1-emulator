#ifndef OWN_DNS_PACKET_H
#define OWN_DNS_PACKET_H
#define OWN_DNS_PACKET_CAP 1232u
/* One ordinary uncompressed IN question, optional single EDNS0 OPT.
 * Returns 1 with response, or 0 (drop malformed/unsupported framing).
 * No mutation of input, allocation, recursion, or external lookup. */
int own_dns_response(const unsigned char *input, unsigned length,
                     unsigned char *output, unsigned capacity, unsigned *written);
#endif

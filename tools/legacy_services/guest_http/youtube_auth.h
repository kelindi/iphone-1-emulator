#ifndef OWN_YOUTUBE_AUTH_H
#define OWN_YOUTUBE_AUTH_H
#define OWN_YOUTUBE_AUTH_CAP 16384u
/* Borrowed immutable spans into the supplied body. Phase 1 populates hr1 and
 * decoded_hr1; phase 2 populates all six fields, without verifying signatures. */
struct own_youtube_field { const unsigned char *data; unsigned length; };
struct own_youtube_auth {
 struct own_youtube_field r1, r2, cert, sig, hr1, hmackr2;
 unsigned char decoded_hr1[20];
};
int own_youtube_auth_parse(unsigned phase, const unsigned char *, unsigned,
                           struct own_youtube_auth *);
/* Returns HTTP status, or zero for invalid arguments/capacity. Phase 2 defaults to503. OWN_YOUTUBE_DEMO=1 instead accepts a parsed form
 * and returns a local compatibility token; no signature authentication claimed. */
unsigned own_youtube_auth_response(unsigned phase, const unsigned char *, unsigned,
                                   unsigned char *, unsigned, unsigned *);
#endif

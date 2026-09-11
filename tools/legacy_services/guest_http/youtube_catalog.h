#ifndef OWN_YOUTUBE_CATALOG_H
#define OWN_YOUTUBE_CATALOG_H
/* Explicit demo build only: 1 known feed, 2 owned asset, 0 unsupported. */
int own_youtube_target(const unsigned char *,unsigned);
/* Caller first validates complete framing and exact route-specific Host. */
int own_youtube_get_response(const unsigned char *,unsigned,unsigned char *,unsigned,unsigned *);
#endif

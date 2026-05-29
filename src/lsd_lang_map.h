//
//  lsd_lang_map.h
//  liblsd
//
//  ABBYY Lingvo LCID → BCP 47 language tag lookup (internal)
//
#ifndef lsd_lang_map_h
#define lsd_lang_map_h

#include <stdint.h>

// Binary-search the sorted LCID table. Returns BCP 47 tag or NULL.
const char *lsd_lang_lookup_bcp47(uint16_t lcid);

#endif /* lsd_lang_map_h */

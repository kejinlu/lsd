//
//  lsd_reader.h
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#ifndef lingvo_lsd_h
#define lingvo_lsd_h

#include "lsd_types.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// LSD reader (opaque type)
// ============================================================
typedef struct lsd_reader lsd_reader;

// ============================================================
// Create and destroy
// ============================================================

/**
 * Open an LSD file
 * @param path File path
 * @return LSD reader, or NULL on failure
 */
lsd_reader *lsd_reader_open(const char *path);

/**
 * Close the LSD reader
 */
void lsd_reader_close(lsd_reader *reader);

// ============================================================
// Property access
// ============================================================

/**
 * Get the dictionary name (UTF-8)
 * @param reader LSD reader
 * @param name Output name (caller must free)
 * @return 0 on success
 */
int lsd_reader_get_name(const lsd_reader *reader, char **name);

/**
 * Get file header information (version, entry count, languages, etc.)
 * @param reader LSD reader
 * @return Pointer to file header, or NULL on failure
 */
const lsd_header *lsd_reader_get_header(const lsd_reader *reader);

// ============================================================
// Entry lookup (B+ tree binary search)
// ============================================================

/**
 * Find an entry (exact match)
 * @param reader LSD reader
 * @param key Search keyword (UTF-8)
 * @param heading Output entry information
 * @return true on success
 */
bool lsd_reader_find_heading(lsd_reader *reader,
                                        const char *key,
                                        lsd_heading *heading);

// ============================================================
// Prefix search
// ============================================================

/**
 * Prefix search (get suggestion list)
 * @param reader LSD reader
 * @param prefix Prefix (UTF-8)
 * @param limit Maximum number of results (0 means unlimited)
 * @param results Output entry array
 * @param result_count Output entry count
 * @return true on success
 */
bool lsd_reader_prefix(lsd_reader *reader,
                                   const char *prefix,
                                   size_t limit,
                                   lsd_heading **results,
                                   size_t *result_count);

// ============================================================
// Article reading
// ============================================================

/**
 * Read article content
 * @param reader LSD reader
 * @param reference Article reference (obtained from heading)
 * @param content Output article content (UTF-8, caller must free)
 * @return 0 on success
 */
int lsd_reader_read_article(lsd_reader *reader,
                                       uint32_t reference,
                                       char **content);

/**
 * Read annotation
 * @param reader LSD reader
 * @param annotation Output annotation (UTF-8, caller must free)
 * @return 0 on success
 */
int lsd_reader_read_annotation(lsd_reader *reader, char **annotation);

// ============================================================
// Overlay (resource) operations
// ============================================================

/**
 * Read overlay resource data
 * @param reader LSD reader
 * @param name Resource name (UTF-8)
 * @param data Output data (caller must free)
 * @param size Output data size
 * @return true on success
 */
bool lsd_reader_read_overlay(lsd_reader *reader,
                                      const char *name,
                                      uint8_t **data,
                                      size_t *size);

// ============================================================
// Iterate all entries (iterator)
// ============================================================

/**
 * Entry iterator (opaque type)
 *
 * Iterates all entries in B+ tree sort order (via leaf page next links).
 * The returned heading pointer is valid until the next lsd_heading_iter_next call or iterator destruction.
 */
typedef struct lsd_heading_iter lsd_heading_iter;

/**
 * Create an entry iterator
 * @param reader LSD reader (must remain valid during iteration)
 * @return Iterator, or NULL on failure
 */
lsd_heading_iter *lsd_heading_iter_create(lsd_reader *reader);

/**
 * Destroy the iterator
 */
void lsd_heading_iter_destroy(lsd_heading_iter *iter);

/**
 * Advance the iterator and get the next entry
 * @param iter Iterator
 * @return Pointer to current entry, or NULL when iteration is complete
 */
const lsd_heading *lsd_heading_iter_next(lsd_heading_iter *iter);

// ============================================================
// Debug helper functions
// ============================================================

/**
 * Print dictionary information (for debugging)
 * @param reader LSD reader
 */
void lsd_reader_dump_info(const lsd_reader *reader);

/**
 * Print B+ tree structure (for debugging)
 * @param reader LSD reader
 * @param max_pages Maximum number of pages to print (0 means all)
 */
void lsd_reader_dump_btree(lsd_reader *reader, uint32_t max_pages);

#ifdef __cplusplus
}
#endif

#endif /* lingvo_lsd_h */

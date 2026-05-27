//
//  dsl_reader.h
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#ifndef dsl_h
#define dsl_h

#include "lsd_types.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// DSL file encoding
// ============================================================

typedef enum {
    DSL_ENCODING_UNKNOWN,
    DSL_ENCODING_UTF8,
    DSL_ENCODING_UTF16LE,
    DSL_ENCODING_UTF16BE,
} dsl_encoding;

// ============================================================
// DSL file header
// ============================================================

typedef struct dsl_header {
    char *name;              // Dictionary name
    char *index_language;    // Source language
    char *contents_language; // Target language
    char *metadata;          // Metadata (extracted from {{ ... }})
    dsl_encoding encoding;   // File encoding
} dsl_header;

// ============================================================
// DSL article
// ============================================================

typedef struct dsl_article {
    char *heading;           // Article heading
    size_t heading_length;   // Heading length
    char *definition;        // Article definition
    size_t definition_length;// Definition length
    size_t definition_offset;// Definition start position in the file
} dsl_article;

// ============================================================
// DSL reader (opaque)
// ============================================================

typedef struct dsl_reader dsl_reader;

// ============================================================
// Article iterator (opaque)
//
// Iterates all articles in file order (sequential).
// The returned article pointer is valid until the next
// dsl_article_iter_next call or iterator destruction.
// ============================================================

typedef struct dsl_article_iter dsl_article_iter;

// ============================================================
// Create and destroy
// ============================================================

lsd_status dsl_reader_open(const char *filename, dsl_reader **out_reader);
void dsl_reader_close(dsl_reader *reader);

// ============================================================
// Property access
// ============================================================

const dsl_header *dsl_reader_get_header(const dsl_reader *reader);
lsd_status dsl_reader_get_name(const dsl_reader *reader, char **name);
const char *dsl_reader_get_source_language(const dsl_reader *reader);
const char *dsl_reader_get_target_language(const dsl_reader *reader);
const char *dsl_reader_get_metadata(const dsl_reader *reader);
dsl_encoding dsl_reader_get_encoding(const dsl_reader *reader);

// ============================================================
// Article iterator
// ============================================================

dsl_article_iter *dsl_article_iter_create(dsl_reader *reader);
void dsl_article_iter_destroy(dsl_article_iter *iter);
lsd_status dsl_article_iter_next(dsl_article_iter *iter, const dsl_article **out_article);

// ============================================================
// Utility
// ============================================================

const char *dsl_encoding_name(dsl_encoding encoding);

#ifdef __cplusplus
}
#endif

#endif /* dsl_h */

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
// DSL heading (parsed from DSL markup)
// ============================================================

// Parsed heading: pre-generated search keys + display title.
// keys[0] is the primary key (all optional parts included).
// For "hono(u)r": keys=["honour","honr"], display="hono(u)r"
// For "a(b)c(d)e": keys=["abcde","ace","abce","acde"], display="a(b)c(d)e"
typedef struct {
    char **keys;      // search key strings (owned array + owned strings)
    int key_count;    // number of keys
    char *display;    // display title: keeps () brackets, removes {} brackets but keeps content
} dsl_heading;

// Release heading resources.
void dsl_heading_cleanup(dsl_heading *h);

// ============================================================
// DSL article
// ============================================================

typedef struct dsl_article {
    dsl_heading *headings;       // Array of parsed headings (multiple heading lines)
    int heading_count;           // Number of headings
    char *definition;            // Article definition
    size_t definition_length;    // Definition length
    size_t definition_offset;    // Definition start position in the file
    struct dsl_article *sub_articles; // @ sub-entries extracted from body
    int sub_article_count;       // Number of sub-articles (always flat, no nesting)
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

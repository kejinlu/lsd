//
//  lsd_reader.c
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#include "lsd_reader.h"
#include "lsd_page_store.h"
#include "lsd_bitstream.h"
#include "lsd_decoder.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zlib.h>
#include "lsd_utils.h"

// ============================================================
// Endianness check
// ============================================================

/**
 * Runtime detection of whether the current device is little-endian
 */
static bool is_little_endian(void) {
    uint16_t x = 0x0001;
    return *(uint8_t *)&x == 1;
}

// ============================================================
// Internal structure definitions
// ============================================================

// lsd_header is defined in lsd_types.h

typedef struct lsd_overlay_heading {
    uint16_t *name;                   // Resource name (UTF-16)
    size_t name_length;
    uint32_t offset;                  // Offset
    uint32_t unk2;                    // Unknown
    uint32_t inflated_size;           // Decompressed size
    uint32_t stream_size;             // Compressed data size
} lsd_overlay_heading;

struct lsd_reader {
    FILE *file;                       // File handle
    lsd_bitstream *bstr;              // Bitstream reader
    lsd_header header;                // File header

    lsd_decoder *decoder;             // Decoder
    bool decoder_loaded;              // Whether the decoder has been loaded

    uint16_t *name;                   // Dictionary name (UTF-16)
    size_t name_length;

    uint8_t *icon;                    // Icon data
    size_t icon_size;

    uint32_t pages_end;               // End position of the pages area
    uint32_t overlay_data;            // Overlay data offset

    // Overlay cache (lazy-loaded)
    lsd_overlay_heading *overlay_entries;  // Sorted overlay entry array
    size_t overlay_count;                  // Number of valid entries
    bool overlay_loaded;                   // Whether overlay has been loaded

    uint16_t root_page;               // B+ tree root node page (0xFFFF means unknown)
    uint16_t first_leaf_page;         // First leaf page (for linear traversal)
    bool btree_valid;                 // Whether the B+ tree structure is valid

    lsd_page_store *page_store;       // Page store

    bool is_supported;                // Whether this is a supported version
};


// ============================================================
// Magic number constants
// ============================================================
static const char LSD_MAGIC[] = "LingVo";

// ============================================================
// Forward declarations
// ============================================================
static void overlay_entries_free(lsd_reader *reader);

// ============================================================
// Internal helper functions
// ============================================================

/**
 * Ensure the decoder is loaded
 */
static bool ensure_decoder_loaded(lsd_reader *reader) {
    if (!reader || !reader->is_supported) return false;

    if (!reader->decoder_loaded) {
        // Seek to the decoder data position
        lsd_bitstream_seek(reader->bstr, reader->header.dictionary_encoder_offset);

        // Load the decoder
        if (!lsd_decoder_load(reader->decoder, reader->bstr)) {
            return false;
        }

        reader->decoder_loaded = true;
    }

    return true;
}

/**
 * Read Unicode string (little-endian)
 */
// ============================================================
// Create and destroy
// ============================================================

lsd_reader *lsd_reader_open(const char *path) {
    if (!path) return NULL;

    // Endianness check: LSD file format is little-endian; big-endian devices are not supported
    if (!is_little_endian()) {
        fprintf(stderr, "LSD Reader: unsupported big-endian platform\n");
        return NULL;
    }

    FILE *file = fopen(path, "rb");
    if (!file) return NULL;

    lsd_reader *reader = calloc(1, sizeof(lsd_reader));
    if (!reader) {
        fclose(file);
        return NULL;
    }

    reader->file = file;
    reader->bstr = lsd_bitstream_create_from_file(file);
    if (!reader->bstr) {
        fclose(file);
        free(reader);
        return NULL;
    }

    // Read file header
    lsd_bitstream_read_bytes(reader->bstr, &reader->header, sizeof(lsd_header));

    // Verify magic number
    if (strncmp(reader->header.magic, LSD_MAGIC, 6) != 0) {
        lsd_reader_close(reader);
        return NULL;
    }

    // Check if version is supported
    reader->is_supported = lsd_is_version_supported(reader->header.version);

    if (!reader->is_supported) {
        // Even if not supported, return the reader (basic info can still be retrieved)
        return reader;
    }

    // Create decoder
    reader->decoder = lsd_decoder_create(reader->header.version);
    if (!reader->decoder) {
        lsd_reader_close(reader);
        return NULL;
    }

    // Read dictionary name
    uint8_t name_len;
    lsd_bitstream_read_bytes(reader->bstr, &name_len, 1);
    reader->name = lsd_bitstream_read_utf16_string(reader->bstr, name_len, false, &reader->name_length);

    // Skip first word and last word
    uint8_t first_heading_len = (uint8_t)lsd_bitstream_read(reader->bstr, 8);
    lsd_bitstream_seek(reader->bstr, lsd_bitstream_tell(reader->bstr) + first_heading_len * 2);

    uint8_t last_heading_len = (uint8_t)lsd_bitstream_read(reader->bstr, 8);
    lsd_bitstream_seek(reader->bstr, lsd_bitstream_tell(reader->bstr) + last_heading_len * 2);

    // Skip uppercase alphabet
    uint32_t capitals_len = lsd_reverse32(lsd_bitstream_read(reader->bstr, 32));
    lsd_bitstream_seek(reader->bstr, lsd_bitstream_tell(reader->bstr) + capitals_len * 2);

    // Read icon (if version supports it)
    if (reader->header.version > 0x120000) {
        uint16_t icon_len;
        lsd_bitstream_read_bytes(reader->bstr, &icon_len, 2);
        if (icon_len > 0) {
            reader->icon = malloc(icon_len);
            if (reader->icon) {
                lsd_bitstream_read_bytes(reader->bstr, reader->icon, icon_len);
                reader->icon_size = icon_len;
            }
        }
    }

    // Skip checksum (if version supports it)
    if (reader->header.version > 0x140000) {
        lsd_bitstream_seek(reader->bstr, lsd_bitstream_tell(reader->bstr) + 4);
    }

    // Read pages_end and overlay_data
    lsd_bitstream_read_bytes(reader->bstr, &reader->pages_end, 4);
    lsd_bitstream_read_bytes(reader->bstr, &reader->overlay_data, 4);

    // Handle older versions
    if (reader->header.version < 0x120000) {
        reader->overlay_data = (uint32_t)-1;
    } else if (reader->header.version < 0x140000) {
        reader->overlay_data = 0;
    }

    // Initialize B+ tree info
    reader->root_page = 0xFFFF;
    reader->first_leaf_page = 0xFFFF;
    reader->btree_valid = false;

    // Create page store
    uint32_t pages_count = reader->header.last_page + 1;
    reader->page_store = lsd_page_store_create(reader->bstr, reader->decoder,
                                               reader->header.pages_offset,
                                               pages_count);

    // Scan pages to find the root node and the first leaf page
    for (uint32_t i = 0; i < pages_count; i++) {
        lsd_page_header header;
        if (!lsd_page_store_read_header(reader->page_store, i, &header)) {
            continue;
        }

        // Root node: parent points to itself or is the invalid value 0xFFFF
        if (header.parent == header.number || header.parent == 0xFFFF) {
            reader->root_page = (uint16_t)i;
            reader->btree_valid = true;
        }

        // First leaf page: a leaf page whose prev is 0xFFFF
        if (header.is_leaf && header.prev == 0xFFFF) {
            reader->first_leaf_page = (uint16_t)i;
        }

        // Both found, exit early
        if (reader->root_page != 0xFFFF && reader->first_leaf_page != 0xFFFF) {
            break;
        }
    }

    // If the first leaf page was not found, find any leaf page
    if (reader->first_leaf_page == 0xFFFF) {
        for (uint32_t i = 0; i < pages_count; i++) {
            lsd_page_header header;
            if (lsd_page_store_read_header(reader->page_store, i, &header) && header.is_leaf) {
                reader->first_leaf_page = (uint16_t)i;
                break;
            }
        }
    }

    return reader;
}

void lsd_reader_close(lsd_reader *reader) {
    if (!reader) return;

    overlay_entries_free(reader);

    if (reader->page_store) {
        lsd_page_store_destroy(reader->page_store);
    }
    if (reader->decoder) {
        lsd_decoder_destroy(reader->decoder);
    }
    if (reader->bstr) {
        lsd_bitstream_destroy(reader->bstr);
    }
    if (reader->file) {
        fclose(reader->file);
    }
    if (reader->name) {
        free(reader->name);
    }
    if (reader->icon) {
        free(reader->icon);
    }

    free(reader);
}

// ============================================================
// Property access
// ============================================================

int lsd_reader_get_name(const lsd_reader *reader, char **name) {
    if (!reader || !name) return -1;

    if (!reader->name || reader->name_length == 0) {
        *name = strdup("");
        return 0;
    }

    return lsd_utf16_to_utf8(reader->name, reader->name_length, name);
}

const lsd_header *lsd_reader_get_header(const lsd_reader *reader) {
    return reader ? &reader->header : NULL;
}

// ============================================================
// B+ tree binary search
// ============================================================

/**
 * Find a child page within a node page (binary search, find leftmost)
 *
 * B+ tree internal node structure:
 * keys:     [key0] [key1] [key2] ... [keyN-1]
 * children: [c0]   [c1]   [c2]   [c3] ... [cN]
 *
 * Search rules:
 * - If word < key0, go to children[0]
 * - If key0 <= word < key1, go to children[1]
 * - If keyN-1 <= word, go to children[N]
 *
 * Use binary search to find the first position i where keys[i] > key, then return children[i]
 */
static uint32_t lsd_node_page_child_bsearch_left(lsd_node_page *node,
                                 const uint16_t *key, size_t key_len) {
    if (!node || !node->children || node->child_count == 0) {
        return 0;
    }

    if (node->key_count == 0) {
        return node->children[0];
    }

    // Number of valid keys (the last key is an empty string, not included in comparison)
    size_t valid_key_count = node->key_count > 0 ? node->key_count - 1 : 0;

    if (valid_key_count == 0) {
        return node->children[0];
    }

    // Binary search: find the first position where keys[i] > key
    // i.e., find the leftmost position i such that keys[i] > key
    size_t left = 0;
    size_t right = valid_key_count;

    while (left < right) {
        size_t mid = left + (right - left) / 2;

        // Skip empty keys
        if (node->key_lengths[mid] == 0) {
            // If an empty key is encountered, move right
            left = mid + 1;
            continue;
        }

        int cmp = lsd_utf16_casecmp(key, key_len,
                                   node->keys[mid], node->key_lengths[mid]);

        if (cmp < 0) {
            // key < keys[mid], answer is on the left or is mid
            right = mid;
        } else {
            // key >= keys[mid], answer is on the right
            left = mid + 1;
        }
    }

    // left is now the first position where keys[i] > key
    // Return children[left]
    if (left >= node->child_count) {
        return node->children[node->child_count - 1];
    }

    return node->children[left];
}

/**
 * Binary search for a heading in a leaf page (find leftmost match)
 */
static int lsd_leaf_page_heading_bsearch_left(lsd_leaf_page *leaf,
                                 const uint16_t *key, size_t key_len,
                                 lsd_heading *result) {
    if (!leaf || leaf->heading_count == 0) return -1;

    size_t left = 0;
    size_t right = leaf->heading_count;
    bool found_exact = false;  // Track whether an exact match was found

    // Binary search to find the first position >= key
    while (left < right) {
        size_t mid = left + (right - left) / 2;

        // Compare: key vs headings[mid]
        int cmp = lsd_utf16_casecmp(key, key_len,
                                   leaf->headings[mid].text,
                                   leaf->headings[mid].text_length);

        if (cmp > 0) {
            // key > headings[mid], answer is on the right
            left = mid + 1;
        } else if (cmp == 0) {
            // Found exact match, continue searching left for the first one
            found_exact = true;
            right = mid;
        } else {
            // key < headings[mid], answer is on the left
            right = mid;
        }
    }

    // left is now the first position >= key
    // Check if it is an exact match (using the state recorded in the loop)
    if (found_exact && left < leaf->heading_count) {
        // Found, copy the result
        if (result) {
            result->text = lsd_utf16_dup(leaf->headings[left].text,
                                            leaf->headings[left].text_length);
            result->text_length = leaf->headings[left].text_length;
            result->reference = leaf->headings[left].reference;
            result->ext_data = NULL;
            result->ext_data_length = 0;
        }
        return 0;
    }

    return -1;  // Not found
}

static bool lsd_reader_find_heading_utf16(lsd_reader *reader,
                                              const uint16_t *key,
                                              size_t key_len,
                                              lsd_heading *out_heading) {
    if (!reader || !key || !reader->is_supported) return false;
    if (!ensure_decoder_loaded(reader)) return false;

    uint32_t pages_count = reader->header.last_page + 1;
    if (pages_count == 0) return false;

    // If B+ tree structure is valid and cached, use the cached tree search
    if (reader->btree_valid && reader->root_page != 0xFFFF && reader->page_store) {
        uint16_t current_page = reader->root_page;

        // Traverse the B+ tree (using cache)
        while (current_page < pages_count) {
            lsd_page_header header;
            if (!lsd_page_store_read_header(reader->page_store, current_page, &header)) {
                break;
            }

            if (header.is_leaf) {
                // Leaf page, search within it
                lsd_leaf_page *leaf = lsd_page_store_get_leaf(reader->page_store, current_page, NULL, 0);
                if (!leaf) break;
                return lsd_leaf_page_heading_bsearch_left(leaf, key, key_len, out_heading) == 0;
            } else {
                // Internal node, find the next child page to visit
                lsd_node_page *node = lsd_page_store_get_node(reader->page_store, current_page);
                if (!node) break;
                current_page = (uint16_t)lsd_node_page_child_bsearch_left(node, key, key_len);
            }
        }
    }

    // Fallback to linear search: iterate through all leaf pages
    for (uint32_t page_num = 0; page_num < pages_count; page_num++) {
        lsd_page_header header;
        if (!lsd_page_store_read_header(reader->page_store, page_num, &header)) {
            continue;
        }

        // Only process leaf pages
        if (!header.is_leaf) continue;

        lsd_leaf_page *leaf = lsd_page_store_get_leaf(reader->page_store, (uint16_t)page_num, NULL, 0);
        if (!leaf) continue;

        if (lsd_leaf_page_heading_bsearch_left(leaf, key, key_len, out_heading) == 0) {
            return true;
        }
    }

    return false;
}

bool lsd_reader_find_heading(lsd_reader *reader,
                                        const char *key,
                                        lsd_heading *heading) {
    if (!reader || !key) return false;

    // Convert to UTF-16
    uint16_t *key_utf16 = NULL;
    size_t key_len = 0;
    if (lsd_utf8_to_utf16(key, &key_utf16, &key_len) != 0) {
        return false;
    }

    // Normalize: trim + space compression
    lsd_utf16_normalize(key_utf16);
    key_len = lsd_utf16_len(key_utf16);
    if (key_len == 0) { free(key_utf16); return false; }

    bool result = lsd_reader_find_heading_utf16(reader, key_utf16, key_len, heading);

    free(key_utf16);
    return result;
}

// ============================================================
// Prefix search
// ============================================================

bool lsd_reader_prefix(lsd_reader *reader,
                                   const char *prefix,
                                   size_t limit,
                                   lsd_heading **results,
                                   size_t *result_count) {
    if (!reader || !prefix || !results || !result_count) return false;
    if (!reader->is_supported || !ensure_decoder_loaded(reader)) return false;

    *results = NULL;
    *result_count = 0;

    // Convert to UTF-16
    uint16_t *prefix_utf16 = NULL;
    size_t prefix_len = 0;
    if (lsd_utf8_to_utf16(prefix, &prefix_utf16, &prefix_len) != 0) {
        return false;
    }

    // Normalize: trim + space compression
    lsd_utf16_normalize(prefix_utf16);
    prefix_len = lsd_utf16_len(prefix_utf16);
    if (prefix_len == 0) { free(prefix_utf16); return false; }

    // Allocate results array
    size_t capacity = limit > 0 ? limit : 64;
    lsd_heading *headings = calloc(capacity, sizeof(lsd_heading));
    if (!headings) {
        free(prefix_utf16);
        return false;
    }

    size_t count = 0;

    // Find the first matching leaf page
    uint32_t pages_count = reader->header.last_page + 1;
    uint32_t current_page = reader->root_page;

    // Navigate to the leaf page
    while (current_page < pages_count) {
        lsd_page_header header;
        if (!lsd_page_store_read_header(reader->page_store, current_page, &header)) {
            break;
        }

        if (header.is_leaf) {
            break;
        }

        lsd_node_page *node = lsd_page_store_get_node(reader->page_store, (uint16_t)current_page);
        if (!node) break;

        current_page = lsd_node_page_child_bsearch_left(node, prefix_utf16, prefix_len);
    }

    // Iterate through leaf pages collecting matching headings
    while (current_page < pages_count && (limit == 0 || count < limit)) {
        lsd_leaf_page *leaf = lsd_page_store_get_leaf(reader->page_store, (uint16_t)current_page, NULL, 0);
        if (!leaf) break;

        bool found_any = false;
        bool past_prefix = false;

        for (size_t i = 0; i < leaf->heading_count && !past_prefix; i++) {
            int cmp = lsd_utf16_prefix_cmp(leaf->headings[i].text,
                                                  leaf->headings[i].text_length,
                                                  prefix_utf16, prefix_len);

            if (cmp == 0) {
                // Prefix match
                found_any = true;

                if (limit > 0 && count >= limit) break;

                // Expand array if needed
                if (count >= capacity) {
                    capacity *= 2;
                    lsd_heading *new_headings = realloc(headings, capacity * sizeof(lsd_heading));
                    if (!new_headings) break;
                    headings = new_headings;
                }

                // Copy heading
                headings[count].text = lsd_utf16_dup(leaf->headings[i].text,
                                                           leaf->headings[i].text_length);
                headings[count].text_length = leaf->headings[i].text_length;
                headings[count].reference = leaf->headings[i].reference;
                headings[count].ext_data = NULL;
                headings[count].ext_data_length = 0;
                count++;
            } else if (cmp > 0 && found_any) {
                // Past the prefix range
                past_prefix = true;
            }
        }

        // Get next page number
        uint16_t next_page = leaf->header.next;

        if (past_prefix || next_page == 0xFFFF) {
            break;
        }

        current_page = next_page;
    }

    free(prefix_utf16);

    *results = headings;
    *result_count = count;

    return true;
}

// ============================================================
// Article reading
// ============================================================

static int lsd_reader_read_article_utf16(lsd_reader *reader,
                                             uint32_t reference,
                                             uint16_t **content,
                                             size_t *content_len) {
    if (!reader || !content || !content_len) return -1;
    if (!reader->is_supported || !ensure_decoder_loaded(reader)) return -1;

    // Seek to the article position
    size_t article_offset = reader->header.articles_offset + reference;
    lsd_bitstream_seek(reader->bstr, article_offset);

    // Decode the article
    if (!lsd_decoder_decode_article(reader->decoder, reader->bstr, content, content_len)) {
        return -2;
    }

    return 0;
}

int lsd_reader_read_article(lsd_reader *reader,
                                       uint32_t reference,
                                       char **content) {
    if (!reader || !content) return -1;

    uint16_t *content_utf16 = NULL;
    size_t content_len = 0;

    int result = lsd_reader_read_article_utf16(reader, reference, &content_utf16, &content_len);
    if (result != 0) return result;

    result = lsd_utf16_to_utf8(content_utf16, content_len, content);
    free(content_utf16);

    return result;
}

int lsd_reader_read_annotation(lsd_reader *reader, char **annotation) {
    if (!reader || !annotation) return -1;
    if (!reader->is_supported || !ensure_decoder_loaded(reader)) return -1;

    // Seek to the annotation position
    lsd_bitstream_seek(reader->bstr, reader->header.annotation_offset);

    // Decode the annotation
    uint16_t *anno_utf16 = NULL;
    size_t anno_len = 0;

    if (!lsd_decoder_decode_article(reader->decoder, reader->bstr, &anno_utf16, &anno_len)) {
        return -2;
    }

    int result = lsd_utf16_to_utf8(anno_utf16, anno_len, annotation);
    free(anno_utf16);

    return result;
}

// ============================================================
// Overlay operations
// ============================================================

/**
 * Comparison function for overlay entries sorted by name (used by qsort)
 */
static int overlay_entry_cmp(const void *a, const void *b) {
    const lsd_overlay_heading *ea = (const lsd_overlay_heading *)a;
    const lsd_overlay_heading *eb = (const lsd_overlay_heading *)b;
    return lsd_utf16_casecmp(ea->name, ea->name_length,
                              eb->name, eb->name_length);
}

/**
 * Lazy-load the overlay entry list and sort it
 */
static bool ensure_overlay_loaded(lsd_reader *reader) {
    if (reader->overlay_loaded) return true;

    reader->overlay_entries = NULL;
    reader->overlay_count = 0;

    if (reader->overlay_data == (uint32_t)-1) {
        reader->overlay_loaded = true;
        return true;
    }

    // Seek to the overlay header list
    lsd_bitstream_seek(reader->bstr, reader->pages_end);

    // Read entry count
    uint32_t entries_count;
    lsd_bitstream_read_bytes(reader->bstr, &entries_count, 4);

    if (entries_count == 0) {
        reader->overlay_loaded = true;
        return true;
    }

    // Allocate array
    lsd_overlay_heading *entries = calloc(entries_count, sizeof(lsd_overlay_heading));
    if (!entries) return false;

    size_t valid_count = 0;

    for (uint32_t i = 0; i < entries_count; i++) {
        // Read name length
        uint8_t name_len = (uint8_t)lsd_bitstream_read(reader->bstr, 8);

        // Read name
        entries[valid_count].name = lsd_bitstream_read_utf16_string(reader->bstr, name_len, false,
                                                            &entries[valid_count].name_length);

        // Read other fields
        lsd_bitstream_read_bytes(reader->bstr, &entries[valid_count].offset, 4);
        lsd_bitstream_read_bytes(reader->bstr, &entries[valid_count].unk2, 4);
        lsd_bitstream_read_bytes(reader->bstr, &entries[valid_count].inflated_size, 4);
        lsd_bitstream_read_bytes(reader->bstr, &entries[valid_count].stream_size, 4);

        // Only keep valid entries
        if (entries[valid_count].inflated_size > 0) {
            valid_count++;
        } else {
            if (entries[valid_count].name) {
                free(entries[valid_count].name);
            }
        }
    }

    // Sort by name to support binary search
    if (valid_count > 1) {
        qsort(entries, valid_count, sizeof(lsd_overlay_heading), overlay_entry_cmp);
    }

    reader->overlay_entries = entries;
    reader->overlay_count = valid_count;
    reader->overlay_loaded = true;

    return true;
}

/**
 * Free overlay cache
 */
static void overlay_entries_free(lsd_reader *reader) {
    if (!reader->overlay_entries) return;

    for (size_t i = 0; i < reader->overlay_count; i++) {
        if (reader->overlay_entries[i].name) {
            free(reader->overlay_entries[i].name);
        }
    }
    free(reader->overlay_entries);
    reader->overlay_entries = NULL;
    reader->overlay_count = 0;
}

bool lsd_reader_read_overlay(lsd_reader *reader,
                                      const char *name,
                                      uint8_t **data,
                                      size_t *size) {
    if (!reader || !name || !data || !size) return false;

    *data = NULL;
    *size = 0;

    if (!ensure_overlay_loaded(reader)) return false;
    if (reader->overlay_count == 0) return false;

    // Convert name to UTF-16
    uint16_t *name_utf16 = NULL;
    size_t name_len = 0;
    if (lsd_utf8_to_utf16(name, &name_utf16, &name_len) != 0) return false;

    // Binary search
    size_t left = 0, right = reader->overlay_count;
    const lsd_overlay_heading *found = NULL;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        int cmp = lsd_utf16_casecmp(name_utf16, name_len,
                                     reader->overlay_entries[mid].name,
                                     reader->overlay_entries[mid].name_length);
        if (cmp == 0) {
            found = &reader->overlay_entries[mid];
            break;
        } else if (cmp < 0) {
            right = mid;
        } else {
            left = mid + 1;
        }
    }

    free(name_utf16);

    if (!found) return false;

    // Seek to the data position
    lsd_bitstream_seek(reader->bstr, found->offset + reader->overlay_data);

    // Read compressed data
    uint8_t *compressed = malloc(found->stream_size);
    if (!compressed) return false;

    lsd_bitstream_read_bytes(reader->bstr, compressed, found->stream_size);

    // Decompress data
    uint8_t *inflated = malloc(found->inflated_size);
    if (!inflated) {
        free(compressed);
        return false;
    }

    uLongf dest_len = found->inflated_size;
    int ret = uncompress(inflated, &dest_len, compressed, found->stream_size);

    free(compressed);

    if (ret != Z_OK) {
        free(inflated);
        return false;
    }

    *data = inflated;
    *size = dest_len;

    return true;
}

// ============================================================
// Heading iterator
// ============================================================

struct lsd_heading_iter {
    lsd_reader *reader;
    lsd_leaf_page *current_leaf;  // Reference, managed by page_store
    uint16_t current_page_num;
    size_t heading_index;
    bool exhausted;
    lsd_heading current;          // Internally held current heading (deep copy)
};

/**
 * Free dynamic data of the iterator's current heading
 */
static void iter_current_clear(lsd_heading_iter *iter) {
    if (iter->current.text) {
        free(iter->current.text);
        iter->current.text = NULL;
    }
    if (iter->current.ext_data) {
        free(iter->current.ext_data);
        iter->current.ext_data = NULL;
    }
}

/**
 * Deep-copy a heading from leaf into iter->current
 */
static void iter_current_copy(lsd_heading_iter *iter,
                                      const lsd_heading *src) {
    iter_current_clear(iter);

    iter->current.text_length = 0;
    iter->current.reference = src->reference;
    iter->current.ext_data_length = 0;

    if (src->text && src->text_length > 0) {
        iter->current.text = lsd_utf16_dup(src->text, src->text_length);
        iter->current.text_length = src->text_length;
    }

    if (src->ext_data && src->ext_data_length > 0) {
        iter->current.ext_data = malloc(src->ext_data_length);
        if (iter->current.ext_data) {
            memcpy(iter->current.ext_data, src->ext_data, src->ext_data_length);
            iter->current.ext_data_length = src->ext_data_length;
        }
    }
}

lsd_heading_iter *lsd_heading_iter_create(lsd_reader *reader) {
    if (!reader || !reader->is_supported) return NULL;
    if (!ensure_decoder_loaded(reader)) return NULL;
    if (reader->first_leaf_page == 0xFFFF) return NULL;

    lsd_heading_iter *iter = calloc(1, sizeof(lsd_heading_iter));
    if (!iter) return NULL;

    iter->reader = reader;
    iter->current_leaf = NULL;
    iter->current_page_num = reader->first_leaf_page;
    iter->heading_index = 0;
    iter->exhausted = false;

    return iter;
}

void lsd_heading_iter_destroy(lsd_heading_iter *iter) {
    if (!iter) return;
    iter_current_clear(iter);
    free(iter);
}

const lsd_heading *lsd_heading_iter_next(lsd_heading_iter *iter) {
    if (!iter || iter->exhausted) return NULL;

    lsd_reader *reader = iter->reader;

    // If the page has not been loaded yet, load the first leaf page
    if (!iter->current_leaf) {
        iter->current_leaf = lsd_page_store_get_leaf(
            reader->page_store, iter->current_page_num, NULL, 0);
        if (!iter->current_leaf || iter->current_leaf->heading_count == 0) {
            iter->exhausted = true;
            return NULL;
        }
        iter->heading_index = 0;
    }

    // If the current page's headings have been exhausted, jump to the next leaf page
    while (iter->heading_index >= iter->current_leaf->heading_count) {
        uint16_t next_page = iter->current_leaf->header.next;

        if (next_page == 0xFFFF) {
            iter->exhausted = true;
            return NULL;
        }

        iter->current_leaf = lsd_page_store_get_leaf(
            reader->page_store, next_page, NULL, 0);
        if (!iter->current_leaf) {
            iter->exhausted = true;
            return NULL;
        }

        iter->current_page_num = next_page;
        iter->heading_index = 0;

        // Skip empty pages
        if (iter->current_leaf->heading_count == 0) continue;
    }

    // Deep-copy the current heading into iter->current and return its pointer
    iter_current_copy(iter, &iter->current_leaf->headings[iter->heading_index]);
    iter->heading_index++;

    return &iter->current;
}

void lsd_reader_dump_info(const lsd_reader *reader) {
    if (!reader) {
        fprintf(stderr, "LSD Reader: NULL\n");
        return;
    }

    fprintf(stderr, "=== LSD Dictionary Info ===\n");
    fprintf(stderr, "Magic: %.6s\n", reader->header.magic);
    fprintf(stderr, "Version: 0x%08X\n", reader->header.version);
    fprintf(stderr, "Entries count: %u\n", reader->header.entries_count);
    fprintf(stderr, "Source language: %u\n", reader->header.source_language);
    fprintf(stderr, "Target language: %u\n", reader->header.target_language);
    fprintf(stderr, "Pages: %u (last_page=%u)\n",
            reader->header.last_page + 1, reader->header.last_page);
    fprintf(stderr, "Pages offset: 0x%08X\n", reader->header.pages_offset);
    fprintf(stderr, "Pages end: 0x%08X\n", reader->pages_end);
    fprintf(stderr, "Supported: %s\n", reader->is_supported ? "yes" : "no");

    // B+ tree info
    fprintf(stderr, "B+ Tree valid: %s\n", reader->btree_valid ? "yes" : "no");
    if (reader->root_page != 0xFFFF) {
        fprintf(stderr, "Root page: %u\n", reader->root_page);
    } else {
        fprintf(stderr, "Root page: not found\n");
    }
    if (reader->first_leaf_page != 0xFFFF) {
        fprintf(stderr, "First leaf page: %u\n", reader->first_leaf_page);
    } else {
        fprintf(stderr, "First leaf page: not found\n");
    }

    // Print dictionary name
    char *name_utf8 = NULL;
    lsd_utf16_to_utf8(reader->name, reader->name_length, &name_utf8);
    fprintf(stderr, "Name (UTF-16 len=%zu): %s\n",
            reader->name_length, name_utf8 ? name_utf8 : "(null)");
    free(name_utf8);

    fprintf(stderr, "===========================\n");
}

void lsd_reader_dump_btree(lsd_reader *reader, uint32_t max_pages) {
    if (!reader || !reader->is_supported) {
        fprintf(stderr, "Invalid reader or unsupported format\n");
        return;
    }

    uint32_t pages_count = reader->header.last_page + 1;
    if (max_pages == 0 || max_pages > pages_count) {
        max_pages = pages_count;
    }

    fprintf(stderr, "\n=== B+ Tree Structure (%u pages) ===\n", pages_count);

    for (uint32_t i = 0; i < max_pages; i++) {
        lsd_page_header header;
        if (!lsd_page_store_read_header(reader->page_store, i, &header)) {
            fprintf(stderr, "Page %u: [read error]\n", i);
            continue;
        }

        const char *type = header.is_leaf ? "LEAF" : "NODE";
        const char *is_root = "";
        if (header.parent == header.number || header.parent == 0xFFFF) {
            is_root = " [ROOT]";
        }

        fprintf(stderr, "Page %u: %s%s | num=%u parent=%u prev=%u next=%u count=%u\n",
                i, type, is_root,
                header.number, header.parent, header.prev, header.next,
                header.headings_count);
    }

    fprintf(stderr, "=====================================\n\n");
}

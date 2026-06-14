//
//  dsl_reader.c
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#include "dsl_reader.h"
#include "dictzip.h"
#include "lsd_platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================
// Constants
// ============================================================
#define READ_BUFFER_SIZE (64 * 1024)

// ============================================================
// Struct definitions (opaque)
// ============================================================

struct dsl_reader {
    FILE *file;
    dictzip *dz;
    char *filename;

    dsl_header header;

    size_t file_size;
    size_t data_offset;
    size_t current_offset;
    bool is_dz;

    // Read buffer (for dictzip)
    char *read_buffer;
    size_t buffer_size;
    size_t buffer_pos;
    size_t buffer_valid;

    // First data line after header (consumed by iterator)
    char *pending_line;
};

struct dsl_article_iter {
    dsl_reader *reader;
    dsl_article current;
    char *peek_line;
};

// ============================================================
// Internal helpers
// ============================================================

static int unicode_to_utf8(uint32_t codepoint, char *utf8_buf) {
    if (codepoint <= 0x7F) {
        utf8_buf[0] = (char)codepoint;
        return 1;
    } else if (codepoint <= 0x7FF) {
        utf8_buf[0] = 0xC0 | (char)((codepoint >> 6) & 0x1F);
        utf8_buf[1] = 0x80 | (char)(codepoint & 0x3F);
        return 2;
    } else if (codepoint <= 0xFFFF) {
        utf8_buf[0] = 0xE0 | (char)((codepoint >> 12) & 0x0F);
        utf8_buf[1] = 0x80 | (char)((codepoint >> 6) & 0x3F);
        utf8_buf[2] = 0x80 | (char)(codepoint & 0x3F);
        return 3;
    } else if (codepoint <= 0x10FFFF) {
        utf8_buf[0] = 0xF0 | (char)((codepoint >> 18) & 0x07);
        utf8_buf[1] = 0x80 | (char)((codepoint >> 12) & 0x3F);
        utf8_buf[2] = 0x80 | (char)((codepoint >> 6) & 0x3F);
        utf8_buf[3] = 0x80 | (char)(codepoint & 0x3F);
        return 4;
    }
    return 0;
}

static int dsl_getc(dsl_reader *reader) {
    if (!reader) return EOF;

    if (reader->is_dz) {
        if (reader->buffer_pos >= reader->buffer_valid) {
            uint32_t bytes_read;
            unsigned char *data = dictzip_read(reader->dz,
                                              (uint32_t)reader->current_offset,
                                              READ_BUFFER_SIZE,
                                              &bytes_read);
            if (!data || bytes_read == 0) return EOF;

            if (reader->buffer_size < bytes_read) {
                char *nb = realloc(reader->read_buffer, bytes_read);
                if (!nb) { free(data); return EOF; }
                reader->read_buffer = nb;
                reader->buffer_size = bytes_read;
            }

            memcpy(reader->read_buffer, data, bytes_read);
            free(data);

            reader->buffer_pos = 0;
            reader->buffer_valid = bytes_read;
            reader->current_offset += bytes_read;
        }

        return (unsigned char)reader->read_buffer[reader->buffer_pos++];
    } else {
        return reader->file ? fgetc(reader->file) : EOF;
    }
}

static int dsl_get_utf16_char(dsl_reader *reader) {
    if (!reader) return EOF;

    int byte1, byte2;

    if (reader->is_dz) {
        if (reader->buffer_pos + 1 >= reader->buffer_valid) {
            uint32_t bytes_read;
            unsigned char *data = dictzip_read(reader->dz,
                                              (uint32_t)reader->current_offset,
                                              READ_BUFFER_SIZE,
                                              &bytes_read);
            if (!data || bytes_read == 0) return EOF;

            if (reader->buffer_size < bytes_read) {
                char *nb = realloc(reader->read_buffer, bytes_read);
                if (!nb) { free(data); return EOF; }
                reader->read_buffer = nb;
                reader->buffer_size = bytes_read;
            }

            memcpy(reader->read_buffer, data, bytes_read);
            free(data);

            reader->buffer_pos = 0;
            reader->buffer_valid = bytes_read;
            reader->current_offset += bytes_read;
        }

        if (reader->buffer_pos + 1 >= reader->buffer_valid) return EOF;

        unsigned char *buf = (unsigned char *)reader->read_buffer;
        if (reader->header.encoding == DSL_ENCODING_UTF16LE) {
            byte1 = buf[reader->buffer_pos++];
            byte2 = buf[reader->buffer_pos++];
        } else {
            byte2 = buf[reader->buffer_pos++];
            byte1 = buf[reader->buffer_pos++];
        }
    } else {
        if (!reader->file) return EOF;

        byte1 = fgetc(reader->file);
        if (byte1 == EOF) return EOF;

        byte2 = fgetc(reader->file);
        if (byte2 == EOF) return EOF;

        if (reader->header.encoding == DSL_ENCODING_UTF16BE) {
            int temp = byte1;
            byte1 = byte2;
            byte2 = temp;
        }
    }

    uint16_t utf16_char = (uint16_t)((byte2 << 8) | byte1);

    // Surrogate pair
    if (utf16_char >= 0xD800 && utf16_char <= 0xDBFF) {
        int byte3, byte4;

        if (reader->is_dz) {
            if (reader->buffer_pos + 1 >= reader->buffer_valid) return EOF;

            unsigned char *buf = (unsigned char *)reader->read_buffer;
            if (reader->header.encoding == DSL_ENCODING_UTF16LE) {
                byte3 = buf[reader->buffer_pos++];
                byte4 = buf[reader->buffer_pos++];
            } else {
                byte4 = buf[reader->buffer_pos++];
                byte3 = buf[reader->buffer_pos++];
            }
        } else {
            byte3 = fgetc(reader->file);
            if (byte3 == EOF) return EOF;
            byte4 = fgetc(reader->file);
            if (byte4 == EOF) return EOF;

            if (reader->header.encoding == DSL_ENCODING_UTF16BE) {
                int temp = byte3;
                byte3 = byte4;
                byte4 = temp;
            }
        }

        uint16_t utf16_char2 = (uint16_t)((byte4 << 8) | byte3);
        uint32_t high = utf16_char - 0xD800;
        uint32_t low = utf16_char2 - 0xDC00;
        return (int)((high << 10) + low + 0x10000);
    }

    return (int)utf16_char;
}

static char *dsl_read_line(dsl_reader *reader) {
    if (!reader) return NULL;

    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;

    int ch = 0;

    bool is_utf16 = (reader->header.encoding == DSL_ENCODING_UTF16LE ||
                     reader->header.encoding == DSL_ENCODING_UTF16BE);

    if (is_utf16) {
        while (1) {
            ch = dsl_get_utf16_char(reader);
            if (ch == EOF) break;
            if (ch == '\n') break;
            if (ch == '\r') continue;

            char utf8_buf[4];
            int utf8_len = unicode_to_utf8((uint32_t)ch, utf8_buf);
            if (utf8_len <= 0) continue;

            if (len + utf8_len + 1 >= cap) {
                cap = (len + utf8_len + 1) * 2;
                char *nb = realloc(buf, cap);
                if (!nb) { free(buf); return NULL; }
                buf = nb;
            }
            memcpy(buf + len, utf8_buf, utf8_len);
            len += utf8_len;
        }
    } else {
        while (1) {
            ch = dsl_getc(reader);
            if (ch == EOF || ch == '\n') break;
            if (ch == '\r') continue;

            if (len + 2 >= cap) {
                cap = (len + 2) * 2;
                char *nb = realloc(buf, cap);
                if (!nb) { free(buf); return NULL; }
                buf = nb;
            }
            buf[len++] = (char)ch;
        }
    }

    if (len == 0 && ch == EOF) {
        free(buf);
        return NULL;
    }

    buf[len] = '\0';
    return buf;
}

static dsl_encoding detect_bom_from_bytes(const unsigned char *bom, size_t len, size_t *out_bom_size) {
    if (out_bom_size) *out_bom_size = 0;

    if (len >= 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF) {
        if (out_bom_size) *out_bom_size = 3;
        return DSL_ENCODING_UTF8;
    }

    if (len >= 2) {
        if (bom[0] == 0xFF && bom[1] == 0xFE) {
            if (out_bom_size) *out_bom_size = 2;
            return DSL_ENCODING_UTF16LE;
        }
        if (bom[0] == 0xFE && bom[1] == 0xFF) {
            if (out_bom_size) *out_bom_size = 2;
            return DSL_ENCODING_UTF16BE;
        }
    }

    // Heuristic: no BOM, guess from null-byte patterns
    if (len >= 2) {
        if (bom[0] != 0 && bom[1] == 0) return DSL_ENCODING_UTF16LE;
        if (bom[0] == 0 && bom[1] != 0) return DSL_ENCODING_UTF16BE;
    }

    // No BOM and no 16-bit pattern: assume UTF-8
    return DSL_ENCODING_UTF8;
}

// ============================================================
// Heading cleanup
// ============================================================

void dsl_heading_cleanup(dsl_heading *h) {
    if (!h) return;
    if (h->keys) {
        for (int i = 0; i < h->key_count; i++) free(h->keys[i]);
        free(h->keys);
    }
    free(h->display);
    h->keys = NULL;
    h->key_count = 0;
    h->display = NULL;
}

// ============================================================
// Article cleanup helper
// ============================================================

static void dsl_article_cleanup(dsl_article *art) {
    if (!art) return;
    for (int i = 0; i < art->heading_count; i++)
        dsl_heading_cleanup(&art->headings[i]);
    free(art->headings);
    art->headings = NULL;
    art->heading_count = 0;
    free(art->definition);
    art->definition = NULL;
    art->definition_length = 0;
    for (int i = 0; i < art->sub_article_count; i++)
        dsl_article_cleanup(&art->sub_articles[i]);
    free(art->sub_articles);
    art->sub_articles = NULL;
    art->sub_article_count = 0;
}

// ============================================================
// Heading parser (internal segment-based)
// ============================================================

typedef struct {
    char *text;
    bool optional;
} dsl_segment;

static char *dsl_generate_key(const dsl_segment *segs, int seg_count,
                               int optional_count, int mask) {
    size_t len = 1;
    int opt_idx = 0;
    for (int i = 0; i < seg_count; i++) {
        bool include = segs[i].optional ? (mask & (1 << opt_idx++)) : true;
        if (include && segs[i].text) len += strlen(segs[i].text);
    }

    char *result = malloc(len);
    if (!result) return NULL;
    result[0] = '\0';

    opt_idx = 0;
    for (int i = 0; i < seg_count; i++) {
        bool include = segs[i].optional ? (mask & (1 << opt_idx++)) : true;
        if (include && segs[i].text) strcat(result, segs[i].text);
    }

    return result;
}

static bool push_segment(dsl_segment **segs, int *count, int *cap,
                          char *text_buf, size_t *text_len, bool optional) {
    if (*text_len == 0) return true;
    text_buf[*text_len] = '\0';
    if (*count >= *cap) {
        *cap *= 2;
        dsl_segment *ns = realloc(*segs, (size_t)*cap * sizeof(dsl_segment));
        if (!ns) return false;
        *segs = ns;
    }
    char *text = strdup(text_buf);
    if (!text) return false;
    (*segs)[*count].text = text;
    (*segs)[*count].optional = optional;
    (*count)++;
    *text_len = 0;
    return true;
}

static dsl_heading dsl_parse_heading(const char *src) {
    dsl_heading h = {0};
    if (!src) return h;

    size_t len = strlen(src);

    // Internal segment array
    int seg_count = 0;
    int seg_cap = 8;
    int optional_count = 0;
    dsl_segment *segs = calloc(seg_cap, sizeof(dsl_segment));
    if (!segs) return h;

    size_t text_cap = len + 1;
    char *text_buf = malloc(text_cap);
    if (!text_buf) { free(segs); return h; }
    size_t text_len = 0;

    char *display = malloc(len + 1);
    if (!display) { free(text_buf); free(segs); return h; }
    size_t disp_len = 0;

    enum { S_ROOT, S_BACKSLASH, S_PAREN, S_PAREN_BS, S_CURLY, S_CURLY_BS } state = S_ROOT;

    for (size_t i = 0; i < len; i++) {
        char c = src[i];
        switch (state) {
        case S_ROOT:
            if (c == '\\') {
                state = S_BACKSLASH;
            } else if (c == '(') {
                if (!push_segment(&segs, &seg_count, &seg_cap, text_buf, &text_len, false))
                    goto fail;
                optional_count++;
                display[disp_len++] = '(';
                state = S_PAREN;
            } else if (c == '{') {
                if (!push_segment(&segs, &seg_count, &seg_cap, text_buf, &text_len, false))
                    goto fail;
                state = S_CURLY;
            } else if (c != '\n' && c != '\r') {
                text_buf[text_len++] = c;
                display[disp_len++] = c;
            }
            break;
        case S_BACKSLASH:
            text_buf[text_len++] = c;
            display[disp_len++] = c;
            state = S_ROOT;
            break;
        case S_PAREN:
            if (c == '\\') {
                state = S_PAREN_BS;
            } else if (c == ')') {
                if (!push_segment(&segs, &seg_count, &seg_cap, text_buf, &text_len, true))
                    goto fail;
                display[disp_len++] = ')';
                state = S_ROOT;
            } else if (c != '\n' && c != '\r') {
                text_buf[text_len++] = c;
                display[disp_len++] = c;
            }
            break;
        case S_PAREN_BS:
            text_buf[text_len++] = c;
            display[disp_len++] = c;
            state = S_PAREN;
            break;
        case S_CURLY:
            if (c == '\\') {
                state = S_CURLY_BS;
            } else if (c == '}') {
                state = S_ROOT;
            } else if (c != '\n' && c != '\r') {
                display[disp_len++] = c;
            }
            break;
        case S_CURLY_BS:
            display[disp_len++] = c;
            state = S_CURLY;
            break;
        }
    }

    if (state != S_CURLY && state != S_CURLY_BS) {
        if (!push_segment(&segs, &seg_count, &seg_cap, text_buf, &text_len, state == S_PAREN))
            goto fail;
    }
    display[disp_len] = '\0';

    free(text_buf);

    // Trim display trailing whitespace
    while (disp_len > 0 && (display[disp_len - 1] == ' ' || display[disp_len - 1] == '\t')) {
        display[--disp_len] = '\0';
    }

    // Generate all 2^N keys. keys[0] = primary (all optional included, mask = all 1s).
    int num_masks = 1 << optional_count;
    int key_cap = num_masks;
    h.keys = calloc(key_cap, sizeof(char *));
    if (!h.keys) goto fail;

    // Primary key first (mask = all 1s), then the rest
    for (int i = 0; i < num_masks; i++) {
        int mask = (num_masks - 1) - i;
        char *key = dsl_generate_key(segs, seg_count, optional_count, mask);
        if (!key) goto fail;
        if (key[0] != '\0') {
            h.keys[h.key_count++] = key;
        } else {
            free(key);
        }
    }

    h.display = disp_len > 0 ? display : (free(display), NULL);

    // Free internal segments
    for (int i = 0; i < seg_count; i++) free(segs[i].text);
    free(segs);

    return h;

fail:
    free(text_buf);
    for (int i = 0; i < seg_count; i++) free(segs[i].text);
    free(segs);
    free(display);
    dsl_heading_cleanup(&h);
    return h;
}

// ============================================================
// Sub-article (@) scanner
// ============================================================

typedef enum {
    DSL_SUB_LINE_NONE,     // regular body line
    DSL_SUB_LINE_CLOSE,    // standalone "@"
    DSL_SUB_LINE_OPEN,     // "@ heading"
} dsl_sub_line_type;

static dsl_sub_line_type dsl_classify_sub_line(const char *stripped, const char **heading_out) {
    if (stripped[0] != '@') return DSL_SUB_LINE_NONE;
    if (stripped[1] == '\0') return DSL_SUB_LINE_CLOSE;
    if (stripped[1] == ' ' || stripped[1] == '\t') {
        if (heading_out) {
            const char *p = stripped + 2;
            while (*p == ' ' || *p == '\t') p++;
            *heading_out = p;
        }
        return DSL_SUB_LINE_OPEN;
    }
    return DSL_SUB_LINE_NONE;
}

// Finish current sub-article: expand array, parse heading, fill struct, append [ref].
// On success: heading_raw is freed, sub_def ownership transferred, returns 0.
// On failure: returns -1, caller must free heading_raw and sub_def via goto fail.
static int finish_sub_article(
    dsl_article *parent,
    char **heading_raw,
    char **def, size_t *def_cap, size_t *def_len,
    char **main_buf, size_t *main_len, size_t *main_cap,
    int *cap, int found)
{
    if (found >= *cap) {
        *cap = *cap == 0 ? 4 : *cap * 2;
        dsl_article *ns = realloc(parent->sub_articles, *cap * sizeof(dsl_article));
        if (!ns) return -1;
        parent->sub_articles = ns;
    }

    dsl_article *sa = &parent->sub_articles[found];
    memset(sa, 0, sizeof(*sa));

    dsl_heading h = dsl_parse_heading(*heading_raw);
    if (!h.keys) return -1;

    sa->headings = calloc(1, sizeof(dsl_heading));
    if (!sa->headings) { dsl_heading_cleanup(&h); return -1; }
    sa->headings[0] = h;
    sa->heading_count = 1;
    sa->definition = *def;
    sa->definition_length = *def_len;

    // Append [ref] to main buffer
    const char *ref_key = h.keys[0];
    size_t ref_key_len = strlen(ref_key);
    size_t need = 18 + ref_key_len;
    if (*main_len + need >= *main_cap) {
        *main_cap = (*main_len + need + 1) * 2;
        char *np = realloc(*main_buf, *main_cap);
        if (!np) return -1;
        *main_buf = np;
    }
    memcpy(*main_buf + *main_len, "\t[m2][ref]", 10);
    *main_len += 10;
    memcpy(*main_buf + *main_len, ref_key, ref_key_len);
    *main_len += ref_key_len;
    memcpy(*main_buf + *main_len, "[/ref][/m]\n", 11);
    *main_len += 11;

    free(*heading_raw);
    *heading_raw = NULL;
    *def = NULL;
    *def_cap = 0;
    *def_len = 0;

    return 0;
}

// Scan definition body for @ sub-entries.
// - Splits into main_def (with [ref] links) and sub-articles.
// - Caller must free *out_main_def.
// - sub_articles are stored in parent->sub_articles.
// Returns number of sub-articles found, or -1 on error.
static int dsl_scan_sub_articles(dsl_article *parent,
                                  const char *def_buf,
                                  char **out_parent_def) {
    if (!def_buf || def_buf[0] == '\0') {
        *out_parent_def = NULL;
        return 0;
    }

    size_t def_len = strlen(def_buf);

    size_t parent_cap = def_len + 1;
    char *parent_buf = malloc(parent_cap);
    if (!parent_buf) return -1;
    size_t parent_len = 0;

    int sub_cap = 0;
    int found = 0;

    char *sub_heading_raw = NULL;
    char *sub_def = NULL;
    size_t sub_def_cap = 0;
    size_t sub_def_len = 0;
    bool in_sub = false;

    // Iterate lines in def_buf
    size_t pos = 0;
    while (pos < def_len) {
        size_t eol = pos;
        while (eol < def_len && def_buf[eol] != '\n') eol++;

        size_t line_len = eol - pos;

        size_t content_len = line_len;
        if (content_len > 0 && def_buf[pos + content_len - 1] == '\r')
            content_len--;

        size_t start = pos;
        while (start < pos + content_len && (def_buf[start] == ' ' || def_buf[start] == '\t'))
            start++;
        size_t stripped_len = (pos + content_len) - start;

        char *stripped = malloc(stripped_len + 1);
        if (!stripped) goto fail;
        memcpy(stripped, def_buf + start, stripped_len);
        stripped[stripped_len] = '\0';

        const char *heading_text = NULL;
        dsl_sub_line_type cls = dsl_classify_sub_line(stripped, &heading_text);

        if (cls == DSL_SUB_LINE_CLOSE) {
            if (in_sub) {
                if (finish_sub_article(parent, &sub_heading_raw,
                        &sub_def, &sub_def_cap, &sub_def_len,
                        &parent_buf, &parent_len, &parent_cap, &sub_cap, found) < 0) {
                    free(stripped); goto fail;
                }
                found++;
                in_sub = false;
            }
            free(stripped);
        } else if (cls == DSL_SUB_LINE_OPEN) {
            if (in_sub) {
                if (finish_sub_article(parent, &sub_heading_raw,
                        &sub_def, &sub_def_cap, &sub_def_len,
                        &parent_buf, &parent_len, &parent_cap, &sub_cap, found) < 0) {
                    free(stripped); goto fail;
                }
                found++;
            }
            sub_heading_raw = strdup(heading_text);
            in_sub = true;
            free(stripped);
            if (!sub_heading_raw) goto fail;
        } else {
            if (in_sub) {
                size_t need = content_len + 1;
                if (sub_def_len + need >= sub_def_cap) {
                    sub_def_cap = (sub_def_len + need + 1) * 2;
                    char *ns = realloc(sub_def, sub_def_cap);
                    if (!ns) { free(stripped); goto fail; }
                    sub_def = ns;
                }
                memcpy(sub_def + sub_def_len, def_buf + pos, content_len);
                sub_def_len += content_len;
                sub_def[sub_def_len++] = '\n';
            } else {
                if (parent_len + content_len + 1 >= parent_cap) {
                    parent_cap = (parent_len + content_len + 2) * 2;
                    char *np = realloc(parent_buf, parent_cap);
                    if (!np) { free(stripped); goto fail; }
                    parent_buf = np;
                }
                memcpy(parent_buf + parent_len, def_buf + pos, content_len);
                parent_len += content_len;
                parent_buf[parent_len++] = '\n';
            }
            free(stripped);
        }

        pos = (eol < def_len) ? eol + 1 : eol;
    }

    if (in_sub) {
        if (finish_sub_article(parent, &sub_heading_raw,
                &sub_def, &sub_def_cap, &sub_def_len,
                &parent_buf, &parent_len, &parent_cap, &sub_cap, found) < 0) {
            goto fail;
        }
        found++;
    }

    parent_buf[parent_len] = '\0';
    *out_parent_def = parent_buf;
    parent->sub_article_count = found;
    return found;

fail:
    free(parent_buf);
    free(sub_def);
    free(sub_heading_raw);
    for (int i = 0; i < found; i++)
        dsl_article_cleanup(&parent->sub_articles[i]);
    free(parent->sub_articles);
    parent->sub_articles = NULL;
    parent->sub_article_count = 0;
    return -1;
}

// ============================================================
// Create and destroy
// ============================================================

lsd_status dsl_reader_open(const char *filename, dsl_reader **out_reader) {
    if (!filename || !out_reader) return LSD_ERR_INVALID_PARAM;

    size_t len = strlen(filename);
    bool is_dz = (len >= 3 && strcmp(filename + len - 3, ".dz") == 0);

    dsl_reader *reader = calloc(1, sizeof(dsl_reader));
    if (!reader) return LSD_ERR_MEMORY;

    reader->filename = strdup(filename);
    if (!reader->filename) {
        free(reader);
        return LSD_ERR_MEMORY;
    }
    reader->is_dz = is_dz;

    if (is_dz) {
        reader->dz = dictzip_open(filename);
        if (!reader->dz) {
            free(reader);
            return LSD_ERR_IO;
        }
        reader->file_size = dictzip_get_uncompressed_size(reader->dz);
    } else {
        reader->file = fopen(filename, "rb");
        if (!reader->file) {
            free(reader);
            return LSD_ERR_IO;
        }
        lsd_fseek(reader->file, 0, SEEK_END);
        reader->file_size = lsd_ftell(reader->file);
        rewind(reader->file);
    }

    // Detect encoding and skip BOM
    size_t bom_size = 0;
    if (reader->file) {
        unsigned char bom[4];
        size_t n = fread(bom, 1, 4, reader->file);
        reader->header.encoding = detect_bom_from_bytes(bom, n, &bom_size);
        rewind(reader->file);
        if (bom_size > 0) lsd_fseek(reader->file, bom_size, SEEK_SET);
    } else {
        uint32_t bytes_read;
        unsigned char *data = dictzip_read(reader->dz, 0, 4, &bytes_read);
        if (data && bytes_read > 0) {
            reader->header.encoding = detect_bom_from_bytes(data, bytes_read, &bom_size);
            free(data);
        } else {
            free(data);
            reader->header.encoding = DSL_ENCODING_UTF8;
            bom_size = 0;
        }
        reader->current_offset = bom_size;
    }

    // Parse header comment lines
    char *line;
    while ((line = dsl_read_line(reader)) != NULL) {
        size_t line_len = strlen(line);

        if (line_len == 0) { free(line); continue; }

        // Double-brace metadata {{ ... }}
        if (line_len >= 4 && line[0] == '{' && line[1] == '{') {
            char *end = strstr(line, "}}");
            if (end) {
                size_t content_len = end - (line + 2);
                char *content = strndup(line + 2, content_len);
                char *start = content;
                char *finish = content + content_len - 1;
                while (start <= finish && (*start == ' ' || *start == '\t')) start++;
                while (finish >= start && (*finish == ' ' || *finish == '\t')) finish--;
                *(finish + 1) = '\0';

                if (!reader->header.metadata && strlen(start) > 0)
                    reader->header.metadata = strdup(start);

                free(content);
            }
            free(line);
            continue;
        }

        // Non-comment line = first data line
        if (line[0] != '#') {
            reader->pending_line = line; // consumed by iterator
            break;
        }

        // Parse #NAME / #INDEX_LANGUAGE / #CONTENTS_LANGUAGE
        char *saveptr;
        char *key = strtok_r(line + 1, " \t\"", &saveptr);
        char *value = strtok_r(NULL, "\"", &saveptr);
        if (key && value) {
            if (strcmp(key, "NAME") == 0)
                reader->header.name = strdup(value);
            else if (strcmp(key, "INDEX_LANGUAGE") == 0)
                reader->header.index_language = strdup(value);
            else if (strcmp(key, "CONTENTS_LANGUAGE") == 0)
                reader->header.contents_language = strdup(value);
            else if (strcmp(key, "INCLUDE") == 0) {
                free(line);
                dsl_reader_close(reader);
                return LSD_ERR_FORMAT;
            }
        }

        free(line);
    }

    reader->data_offset = reader->current_offset;
    if (!reader->is_dz && reader->file)
        reader->data_offset = lsd_ftell(reader->file);

    *out_reader = reader;
    return LSD_OK;
}

void dsl_reader_close(dsl_reader *reader) {
    if (!reader) return;

    if (reader->file) fclose(reader->file);
    if (reader->dz) dictzip_close(reader->dz);
    free(reader->read_buffer);
    free(reader->pending_line);
    free(reader->filename);
    free(reader->header.name);
    free(reader->header.index_language);
    free(reader->header.contents_language);
    free(reader->header.metadata);
    free(reader);
}

// ============================================================
// Property access
// ============================================================

const dsl_header *dsl_reader_get_header(const dsl_reader *reader) {
    return reader ? &reader->header : NULL;
}

lsd_status dsl_reader_get_name(const dsl_reader *reader, char **name) {
    if (!reader || !name) return LSD_ERR_INVALID_PARAM;
    const char *n = reader->header.name ? reader->header.name : "";
    *name = strdup(n);
    return *name ? LSD_OK : LSD_ERR_MEMORY;
}

const char *dsl_reader_get_source_language(const dsl_reader *reader) {
    return reader ? reader->header.index_language : NULL;
}

const char *dsl_reader_get_target_language(const dsl_reader *reader) {
    return reader ? reader->header.contents_language : NULL;
}

const char *dsl_reader_get_metadata(const dsl_reader *reader) {
    return reader ? reader->header.metadata : NULL;
}

dsl_encoding dsl_reader_get_encoding(const dsl_reader *reader) {
    return reader ? reader->header.encoding : DSL_ENCODING_UNKNOWN;
}

// ============================================================
// Article iterator
// ============================================================

dsl_article_iter *dsl_article_iter_create(dsl_reader *reader) {
    if (!reader) return NULL;

    dsl_article_iter *iter = calloc(1, sizeof(dsl_article_iter));
    if (!iter) return NULL;

    iter->reader = reader;
    iter->peek_line = reader->pending_line;
    reader->pending_line = NULL;

    return iter;
}

void dsl_article_iter_destroy(dsl_article_iter *iter) {
    if (!iter) return;
    dsl_article_cleanup(&iter->current);
    free(iter->peek_line);
    free(iter);
}

lsd_status dsl_article_iter_next(dsl_article_iter *iter, const dsl_article **out_article) {
    if (!iter || !out_article) return LSD_ERR_INVALID_PARAM;

    dsl_reader *reader = iter->reader;

    // Free previous current data
    dsl_article_cleanup(&iter->current);

    // --- Collect heading lines ---

    int hline_cap = 4;
    int hline_count = 0;
    char **hlines = malloc(hline_cap * sizeof(char *));
    if (!hlines) return LSD_ERR_MEMORY;

    char *line = iter->peek_line;
    iter->peek_line = NULL;

    if (!line) {
        while ((line = dsl_read_line(reader)) != NULL) {
            size_t len = strlen(line);
            if (len == 0 || line[0] == '#') { free(line); continue; }
            if (line[0] != ' ' && line[0] != '\t') break;
            free(line);
        }
    }

    if (!line) {
        free(hlines);
        *out_article = NULL;
        return LSD_DONE;
    }

    hlines[hline_count++] = line;

    // Read additional heading lines
    while ((line = dsl_read_line(reader)) != NULL) {
        size_t len = strlen(line);
        if (len == 0) { free(line); continue; }
        if (line[0] == ' ' || line[0] == '\t') {
            iter->peek_line = line;
            break;
        }
        if (line[0] == '#') { free(line); continue; }
        if (hline_count >= hline_cap) {
            hline_cap *= 2;
            char **new_hl = realloc(hlines, hline_cap * sizeof(char *));
            if (!new_hl) {
                for (int i = 0; i < hline_count; i++) free(hlines[i]);
                free(hlines);
                return LSD_ERR_MEMORY;
            }
            hlines = new_hl;
        }
        hlines[hline_count++] = line;
    }

    // --- Parse headings ---

    dsl_heading *headings = calloc(hline_count, sizeof(dsl_heading));
    if (!headings) {
        for (int i = 0; i < hline_count; i++) free(hlines[i]);
        free(hlines);
        return LSD_ERR_MEMORY;
    }

    bool parse_ok = true;
    for (int i = 0; i < hline_count; i++) {
        headings[i] = dsl_parse_heading(hlines[i]);
        free(hlines[i]);
        if (!headings[i].keys) { parse_ok = false; break; }
    }
    free(hlines);

    if (!parse_ok) {
        for (int i = 0; i < hline_count; i++) dsl_heading_cleanup(&headings[i]);
        free(headings);
        return LSD_ERR_MEMORY;
    }

    size_t article_offset = 0;
    if (!reader->is_dz && reader->file) {
        article_offset = (size_t)lsd_ftell(reader->file);
    } else {
        article_offset = reader->current_offset;
    }

    // --- Read definition (dynamic buffer) ---

    size_t def_cap = 4096;
    size_t def_len = 0;
    char *def_buf = malloc(def_cap);
    if (!def_buf) {
        for (int i = 0; i < hline_count; i++) dsl_heading_cleanup(&headings[i]);
        free(headings);
        return LSD_ERR_MEMORY;
    }
    def_buf[0] = '\0';

    line = iter->peek_line;
    iter->peek_line = NULL;

    if (line) {
        size_t len = strlen(line);
        if (def_len + len + 2 >= def_cap) {
            def_cap = (def_len + len + 2) * 2;
            char *new_buf = realloc(def_buf, def_cap);
            if (!new_buf) {
                free(def_buf);
                for (int i = 0; i < hline_count; i++) dsl_heading_cleanup(&headings[i]);
                free(headings);
                free(line);
                return LSD_ERR_MEMORY;
            }
            def_buf = new_buf;
        }
        memcpy(def_buf + def_len, line, len);
        def_len += len;
        def_buf[def_len++] = '\n';
        free(line);
    }

    while ((line = dsl_read_line(reader)) != NULL) {
        size_t len = strlen(line);

        if (len > 0 && line[0] != ' ' && line[0] != '\t') {
            iter->peek_line = line;
            break;
        }

        if (def_len + len + 2 >= def_cap) {
            def_cap = (def_len + len + 2) * 2;
            char *new_buf = realloc(def_buf, def_cap);
            if (!new_buf) {
                free(def_buf);
                for (int i = 0; i < hline_count; i++) dsl_heading_cleanup(&headings[i]);
                free(headings);
                free(line);
                return LSD_ERR_MEMORY;
            }
            def_buf = new_buf;
        }

        memcpy(def_buf + def_len, line, len);
        def_len += len;
        def_buf[def_len++] = '\n';
        free(line);
    }

    def_buf[def_len] = '\0';

    // --- Scan for @ sub-entries ---

    char *parent_def = NULL;
    int sub_count = dsl_scan_sub_articles(&iter->current, def_buf, &parent_def);

    iter->current.headings = headings;
    iter->current.heading_count = hline_count;
    iter->current.definition_offset = article_offset;

    if (sub_count > 0 && parent_def) {
        free(def_buf);
        iter->current.definition = parent_def;
        iter->current.definition_length = strlen(parent_def);
    } else {
        free(parent_def);
        iter->current.definition = def_buf;
        iter->current.definition_length = def_len;
    }

    *out_article = &iter->current;
    return LSD_OK;
}

// ============================================================
// Utility
// ============================================================

const char *dsl_encoding_name(dsl_encoding encoding) {
    switch (encoding) {
        case DSL_ENCODING_UTF8:    return "UTF-8";
        case DSL_ENCODING_UTF16LE: return "UTF-16LE";
        case DSL_ENCODING_UTF16BE: return "UTF-16BE";
        default:                   return "UNKNOWN";
    }
}

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
#define LINE_BUFFER_SIZE 4096

// ============================================================
// Struct definitions (opaque)
// ============================================================

struct dsl_reader {
    FILE *file;
    dictzip *dz;
    const char *filename;

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
                reader->read_buffer = realloc(reader->read_buffer, bytes_read);
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
                reader->read_buffer = realloc(reader->read_buffer, bytes_read);
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

    char buffer[LINE_BUFFER_SIZE];
    size_t offset = 0;
    int ch = 0;

    bool is_utf16 = (reader->header.encoding == DSL_ENCODING_UTF16LE ||
                     reader->header.encoding == DSL_ENCODING_UTF16BE);

    if (is_utf16) {
        while (offset < sizeof(buffer) - 4) {
            ch = dsl_get_utf16_char(reader);
            if (ch == EOF) break;
            if (ch == '\n') break;
            if (ch == '\r') continue;

            char utf8_buf[4];
            int utf8_len = unicode_to_utf8((uint32_t)ch, utf8_buf);
            if (utf8_len > 0 && offset + utf8_len < sizeof(buffer)) {
                memcpy(buffer + offset, utf8_buf, utf8_len);
                offset += utf8_len;
            }
        }

        if (offset == 0 && ch == EOF) return NULL;

        buffer[offset] = '\0';
        return strdup(buffer);
    } else {
        while (offset < sizeof(buffer) - 1) {
            ch = dsl_getc(reader);
            if (ch == EOF || ch == '\n') break;
            if (ch != '\r') buffer[offset++] = (char)ch;
        }

        if (offset == 0 && ch == EOF) return NULL;

        buffer[offset] = '\0';
        return strdup(buffer);
    }
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

    return DSL_ENCODING_UTF8;
}

static char *clean_heading(const char *src) {
    size_t len = strlen(src);
    char *str = malloc(len + 1);
    if (!str) return NULL;
    int idx = 0;
    int opened = 0;

    for (size_t i = 0; i < len; i++) {
        if (src[i] == '{') {
            opened = 1;
        } else if (src[i] == '}') {
            opened = 0;
        } else if (!opened && src[i] != '\n' && src[i] != '\r') {
            if (i > 0 && src[i-1] == '\\' && src[i-2] != '\\') {
                str[idx-1] = src[i];
            } else if (i == 1 && src[0] == '\\') {
                str[idx-1] = src[i];
            } else {
                str[idx++] = src[i];
            }
        }
    }
    str[idx] = '\0';
    return str;
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

    reader->filename = filename;
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
    free(iter->current.heading);
    free(iter->current.definition);
    free(iter->peek_line);
    free(iter);
}

lsd_status dsl_article_iter_next(dsl_article_iter *iter, const dsl_article **out_article) {
    if (!iter || !out_article) return LSD_ERR_INVALID_PARAM;

    dsl_reader *reader = iter->reader;

    // Free previous article data
    free(iter->current.heading);
    iter->current.heading = NULL;
    free(iter->current.definition);
    iter->current.definition = NULL;

    // --- Read heading ---

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
        *out_article = NULL;
        return LSD_DONE;
    }

    char *heading = clean_heading(line);
    free(line);
    if (!heading) return LSD_ERR_MEMORY;

    size_t heading_len = strlen(heading);
    size_t article_offset = 0;
    if (!reader->is_dz && reader->file) {
        article_offset = (size_t)lsd_ftell(reader->file) - heading_len;
    } else {
        article_offset = reader->current_offset;
    }

    // --- Read definition (dynamic buffer) ---

    size_t def_cap = 4096;
    size_t def_len = 0;
    char *def_buf = malloc(def_cap);
    if (!def_buf) { free(heading); return LSD_ERR_MEMORY; }
    def_buf[0] = '\0';

    while ((line = dsl_read_line(reader)) != NULL) {
        size_t len = strlen(line);

        // Next heading encountered
        if (len > 0 && line[0] != ' ' && line[0] != '\t') {
            iter->peek_line = line; // saved for next iteration
            break;
        }

        // Grow buffer if needed
        if (def_len + len + 2 >= def_cap) {
            def_cap = (def_len + len + 2) * 2;
            char *new_buf = realloc(def_buf, def_cap);
            if (!new_buf) {
                free(def_buf);
                free(heading);
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

    // Fill article
    iter->current.heading = heading;
    iter->current.heading_length = heading_len;
    iter->current.definition = def_buf;
    iter->current.definition_length = def_len;
    iter->current.definition_offset = article_offset;

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

//
//  lsa_reader.c
//  libud
//
//  Created by kejinlu on 2026/04/14.
//

#include "lsa_reader.h"
#include "lsd_platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <vorbis/vorbisfile.h>

// ============================================================
// Constants
// ============================================================
#define LSA_ENTRIES_INIT_CAPACITY 64

// ============================================================
// Internal structures
// ============================================================
struct lsa_entry {
    char *name;              // UTF-8
    uint32_t sample_offset;
    uint32_t sample_size;
};

typedef struct lsa_entry lsa_entry;

struct lsa_reader {
    FILE *file;
    char *filename;

    lsa_entry *entries;
    size_t entry_count;
    size_t entry_capacity;

    unsigned int raw_count;
    long ogg_offset;
    long file_size;
};

// ============================================================
// Internal helper functions
// ============================================================

/**
 * Read LSA-encoded string (UTF-16LE, terminated by 0xFF byte)
 * Returns a UTF-8 string; caller must free it
 */
static char *read_lsa_string(FILE *file, size_t *out_length) {
    // First collect UTF-16LE characters
    uint16_t *utf16 = NULL;
    size_t utf16_len = 0;
    size_t utf16_cap = 32;
    utf16 = (uint16_t *)malloc(utf16_cap * sizeof(uint16_t));
    if (!utf16) return NULL;

    for (;;) {
        uint8_t chr, nextchr;

        if (fread(&chr, 1, 1, file) != 1) break;
        if (chr == 0xFF) break;

        if (fread(&nextchr, 1, 1, file) != 1) break;
        if (nextchr == 0xFF) break;

        if (utf16_len >= utf16_cap) {
            utf16_cap *= 2;
            uint16_t *tmp = (uint16_t *)realloc(utf16, utf16_cap * sizeof(uint16_t));
            if (!tmp) {
                free(utf16);
                return NULL;
            }
            utf16 = tmp;
        }

        utf16[utf16_len++] = (uint16_t)(chr | (nextchr << 8));
    }

    if (utf16_len == 0) {
        free(utf16);
        if (out_length) *out_length = 0;
        return NULL;
    }

    // UTF-16LE -> UTF-8
    // Each UTF-16 character produces at most 3 bytes of UTF-8 (within BMP)
    size_t utf8_cap = utf16_len * 3 + 1;
    char *utf8 = (char *)malloc(utf8_cap);
    if (!utf8) {
        free(utf16);
        return NULL;
    }

    size_t pos = 0;
    for (size_t i = 0; i < utf16_len; i++) {
        uint16_t ch = utf16[i];
        if (ch < 0x80) {
            utf8[pos++] = (char)ch;
        } else if (ch < 0x800) {
            utf8[pos++] = (char)(0xC0 | (ch >> 6));
            utf8[pos++] = (char)(0x80 | (ch & 0x3F));
        } else {
            utf8[pos++] = (char)(0xE0 | (ch >> 12));
            utf8[pos++] = (char)(0x80 | ((ch >> 6) & 0x3F));
            utf8[pos++] = (char)(0x80 | (ch & 0x3F));
        }
    }
    utf8[pos] = '\0';

    free(utf16);

    if (out_length) *out_length = pos;
    return utf8;
}

/**
 * Read little-endian uint32
 */
static bool read_le32(FILE *file, uint32_t *value) {
    uint8_t buf[4];
    if (fread(buf, 1, 4, file) != 4) return false;
    *value = (uint32_t)buf[0]
           | ((uint32_t)buf[1] << 8)
           | ((uint32_t)buf[2] << 16)
           | ((uint32_t)buf[3] << 24);
    return true;
}

/**
 * Read uint8
 */
static bool read_u8(FILE *file, uint8_t *value) {
    return fread(value, 1, 1, file) == 1;
}

/**
 * Add an entry to the entries array
 */
static bool add_entry(lsa_reader *reader, char *name, uint32_t sample_offset, uint32_t sample_size) {
    if (reader->entry_count >= reader->entry_capacity) {
        size_t new_cap = reader->entry_capacity * 2;
        lsa_entry *tmp = (lsa_entry *)realloc(reader->entries, new_cap * sizeof(lsa_entry));
        if (!tmp) return false;
        reader->entries = tmp;
        reader->entry_capacity = new_cap;
    }

    lsa_entry *entry = &reader->entries[reader->entry_count++];
    entry->name = name;
    entry->sample_offset = sample_offset;
    entry->sample_size = sample_size;
    return true;
}

// ============================================================
// Create and destroy
// ============================================================

lsa_reader *lsa_reader_open(const char *filename) {
    if (!filename) return NULL;

    FILE *file = fopen(filename, "rb");
    if (!file) return NULL;

    lsa_reader *reader = (lsa_reader *)calloc(1, sizeof(lsa_reader));
    if (!reader) {
        fclose(file);
        return NULL;
    }

    reader->file = file;
    reader->filename = strdup(filename);
    if (!reader->filename) {
        fclose(file);
        free(reader);
        return NULL;
    }

    // Get file size
    lsd_fseek(file, 0, SEEK_END);
    reader->file_size = lsd_ftell(file);
    rewind(file);

    // Read magic string
    char *magic = read_lsa_string(file, NULL);
    if (!magic || strcmp(magic, "L9SA") != 0) {
        free(magic);
        fclose(file);
        free(reader);
        return NULL;
    }
    free(magic);

    // Read total entry count
    if (!read_le32(file, &reader->raw_count)) {
        fclose(file);
        free(reader);
        return NULL;
    }

    // Initialize entries array
    reader->entry_capacity = LSA_ENTRIES_INIT_CAPACITY;
    if (reader->raw_count < reader->entry_capacity) {
        reader->entry_capacity = reader->raw_count;
    }
    if (reader->entry_capacity == 0) {
        reader->entry_capacity = 1;
    }
    reader->entries = (lsa_entry *)calloc(reader->entry_capacity, sizeof(lsa_entry));
    if (!reader->entries) {
        fclose(file);
        free(reader);
        return NULL;
    }
    reader->entry_count = 0;

    // Parse index region
    for (unsigned int i = 0; i < reader->raw_count; i++) {
        char *name = read_lsa_string(file, NULL);

        if (i == 0) {
            // First entry: name + sampleSize, no offset or marker
            uint32_t sample_size;
            if (!read_le32(file, &sample_size)) {
                free(name);
                break;
            }
            if (name && sample_size > 0) {
                add_entry(reader, name, 0, sample_size);
            } else {
                free(name);
            }
        } else {
            // Subsequent entries: name + sampleOffset + marker + sampleSize
            uint32_t sample_offset;
            if (!read_le32(file, &sample_offset)) {
                free(name);
                break;
            }

            uint8_t marker;
            if (!read_u8(file, &marker)) {
                free(name);
                break;
            }

            if (marker == 0) {
                // Group entry, skip (no sampleSize field)
                free(name);
                continue;
            }

            if (marker != 0xFF) {
                // Invalid marker
                free(name);
                break;
            }

            uint32_t sample_size;
            if (!read_le32(file, &sample_size)) {
                free(name);
                break;
            }

            if (name && sample_size > 0) {
                add_entry(reader, name, sample_offset, sample_size);
            } else {
                free(name);
            }
        }
    }

    // Record Ogg stream start position
    reader->ogg_offset = lsd_ftell(file);

    return reader;
}

void lsa_reader_close(lsa_reader *reader) {
    if (!reader) return;

    if (reader->entries) {
        for (size_t i = 0; i < reader->entry_count; i++) {
            if (reader->entries[i].name) {
                free(reader->entries[i].name);
            }
        }
        free(reader->entries);
    }

    if (reader->file) {
        fclose(reader->file);
    }

    free(reader->filename);
    free(reader);
}

// ============================================================
// Property access
// ============================================================

size_t lsa_reader_get_entry_count(const lsa_reader *reader) {
    return reader ? reader->entry_count : 0;
}

// ============================================================
// Entry lookup
// ============================================================

const lsa_entry *lsa_reader_find_entry(const lsa_reader *reader, const char *name) {
    if (!reader || !name) return NULL;

    for (size_t i = 0; i < reader->entry_count; i++) {
        if (reader->entries[i].name && strcmp(reader->entries[i].name, name) == 0) {
            return &reader->entries[i];
        }
    }
    return NULL;
}

const lsa_entry *lsa_reader_get_entry(const lsa_reader *reader, size_t index) {
    if (!reader || index >= reader->entry_count) return NULL;
    return &reader->entries[index];
}

const char *lsa_reader_get_entry_name(const lsa_reader *reader, size_t index) {
    if (!reader || index >= reader->entry_count) return NULL;
    return reader->entries[index].name;
}

// ============================================================
// Ogg Vorbis callbacks (FILE*-based, with base offset)
// ============================================================

typedef struct {
    FILE *file;
    long base_offset;
} ogg_source;

static size_t ogg_read_cb(void *ptr, size_t size, size_t nmemb, void *datasource) {
    ogg_source *src = (ogg_source *)datasource;
    return fread(ptr, size, nmemb, src->file);
}

static int ogg_seek_cb(void *datasource, ogg_int64_t offset, int whence) {
    ogg_source *src = (ogg_source *)datasource;
    if (whence == SEEK_SET) {
        return lsd_fseek(src->file, (long long)src->base_offset + offset, SEEK_SET);
    }
    return lsd_fseek(src->file, (long long)offset, whence);
}

static long ogg_tell_cb(void *datasource) {
    ogg_source *src = (ogg_source *)datasource;
    return (long)(lsd_ftell(src->file) - src->base_offset);
}

static ov_callbacks ogg_callbacks = {
    ogg_read_cb,
    ogg_seek_cb,
    NULL,  // close: don't close, FILE* managed by lsa_reader
    ogg_tell_cb
};

// ============================================================
// Audio decoding
// ============================================================

bool lsa_reader_decode(lsa_reader *reader, size_t index,
                        int16_t **pcm_data, size_t *size,
                        int *rate, int *channels) {
    if (!reader || index >= reader->entry_count || !pcm_data || !size)
        return false;

    *pcm_data = NULL;
    *size = 0;

    const lsa_entry *entry = &reader->entries[index];

    lsd_fseek(reader->file, reader->ogg_offset, SEEK_SET);

    ogg_source src = { reader->file, reader->ogg_offset };
    OggVorbis_File vf;
    memset(&vf, 0, sizeof(vf));
    if (ov_open_callbacks(&src, &vf, NULL, 0, ogg_callbacks) != 0)
        return false;

    if (ov_pcm_seek(&vf, entry->sample_offset) != 0) {
        ov_clear(&vf);
        return false;
    }

    vorbis_info *info = ov_info(&vf, -1);
    if (!info) {
        ov_clear(&vf);
        return false;
    }
    if (rate) *rate = info->rate;
    if (channels) *channels = info->channels;

    size_t total_bytes = entry->sample_size * sizeof(int16_t);
    int16_t *buffer = (int16_t *)malloc(total_bytes);
    if (!buffer) {
        ov_clear(&vf);
        return false;
    }

    size_t bytes_read = 0;
    int bitstream = 0;
    while (bytes_read < total_bytes) {
        // ov_read args: 0=LE, 2=16bit, 1=signed -> outputs 16-bit signed LE PCM
        long got = ov_read(&vf, (char *)buffer + bytes_read,
                           (int)(total_bytes - bytes_read),
                           0, 2, 1, &bitstream);
        if (got == 0) break;
        if (got < 0) {
            free(buffer);
            ov_clear(&vf);
            return false;
        }
        bytes_read += got;
    }

    ov_clear(&vf);

    *pcm_data = buffer;
    *size = bytes_read;
    return true;
}

bool lsa_reader_decode_by_name(lsa_reader *reader, const char *name,
                                int16_t **pcm_data, size_t *size,
                                int *rate, int *channels) {
    if (!reader || !name) return false;

    // Linear search to get the index
    for (size_t i = 0; i < reader->entry_count; i++) {
        if (reader->entries[i].name && strcmp(reader->entries[i].name, name) == 0) {
            return lsa_reader_decode(reader, i, pcm_data, size, rate, channels);
        }
    }
    return false;
}

// ============================================================
// WAV writing (internal)
// ============================================================

#pragma pack(push, 1)
typedef struct {
    char     riff_id[4];
    uint32_t riff_size;
    char     wave_id[4];
    char     fmt_id[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char     data_id[4];
    uint32_t data_size;
} wav_header;
#pragma pack(pop)

static bool write_wav(const char *path, const int16_t *pcm_data, size_t size,
                      int sample_rate, int channels) {
    if (!path || !pcm_data || size == 0) return false;

    FILE *fp = fopen(path, "wb");
    if (!fp) return false;

    uint32_t data_size = (uint32_t)size;

    wav_header header;
    memcpy(header.riff_id, "RIFF", 4);
    header.riff_size = 36 + data_size;
    memcpy(header.wave_id, "WAVE", 4);
    memcpy(header.fmt_id, "fmt ", 4);
    header.fmt_size = 16;
    header.audio_format = 1;
    header.num_channels = (uint16_t)channels;
    header.sample_rate = (uint32_t)sample_rate;
    header.bits_per_sample = 16;
    header.block_align = (uint16_t)(channels * 2);
    header.byte_rate = (uint32_t)(sample_rate * channels * 2);
    memcpy(header.data_id, "data", 4);
    header.data_size = data_size;

    bool ok = (fwrite(&header, sizeof(wav_header), 1, fp) == 1 &&
               fwrite(pcm_data, 1, data_size, fp) == data_size);

    fclose(fp);
    return ok;
}

// ============================================================
// Batch export
// ============================================================

/**
 * Internal: Decode all entries sequentially and write WAV
 * Sequential read instead of per-entry ov_pcm_seek for better performance
 */
size_t lsa_reader_dump(lsa_reader *reader, const char *output_dir) {
    if (!reader || !output_dir || reader->entry_count == 0) return 0;

    // Open Ogg stream
    lsd_fseek(reader->file, reader->ogg_offset, SEEK_SET);

    ogg_source src = { reader->file, reader->ogg_offset };
    OggVorbis_File vf;
    memset(&vf, 0, sizeof(vf));
    if (ov_open_callbacks(&src, &vf, NULL, 0, ogg_callbacks) != 0)
        return 0;

    vorbis_info *info = ov_info(&vf, -1);
    if (!info) {
        ov_clear(&vf);
        return 0;
    }

    int rate = info->rate;
    int channels = info->channels;
    size_t exported = 0;

    for (size_t i = 0; i < reader->entry_count; i++) {
        const lsa_entry *entry = &reader->entries[i];

        // Calculate samples to read
        uint32_t read_size = entry->sample_size;
        if (i < reader->entry_count - 1) {
            read_size = reader->entries[i + 1].sample_offset - entry->sample_offset;
        }

        size_t total_bytes = read_size * sizeof(int16_t);
        int16_t *buffer = (int16_t *)malloc(total_bytes);
        if (!buffer) break;

        // Sequential PCM read
        size_t bytes_read = 0;
        int bitstream = 0;
        while (bytes_read < total_bytes) {
            long got = ov_read(&vf, (char *)buffer + bytes_read,
                               (int)(total_bytes - bytes_read),
                               0, 2, 1, &bitstream);
            if (got == 0) break;
            if (got < 0) break;
            bytes_read += got;
        }

        // Trim to actual size
        size_t actual_bytes = entry->sample_size * sizeof(int16_t);
        if (bytes_read > actual_bytes) {
            bytes_read = actual_bytes;
        }

        // Build output path: output_dir/name.wav
        // Strip possible .wav suffix from entry name, add .wav uniformly
        char path[1024];
        const char *name = entry->name;
        size_t name_len = name ? strlen(name) : 0;
        if (name_len > 4 && strcmp(name + name_len - 4, ".wav") == 0) {
            snprintf(path, sizeof(path), "%s/%.*s.wav",
                     output_dir, (int)(name_len - 4), name);
        } else {
            snprintf(path, sizeof(path), "%s/%s.wav", output_dir, name ? name : "unknown");
        }

        if (write_wav(path, buffer, bytes_read, rate, channels)) {
            exported++;
        }

        free(buffer);
    }

    ov_clear(&vf);
    return exported;
}

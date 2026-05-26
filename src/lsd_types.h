//
//  lsd_types.h
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#ifndef lsd_types_h
#define lsd_types_h

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Status codes
// ============================================================

typedef enum {
    LSD_OK                = 0,
    LSD_NOT_FOUND         = 1,
    LSD_DONE              = 2,

    LSD_ERR_INVALID_PARAM = -1,
    LSD_ERR_IO            = -2,
    LSD_ERR_MEMORY        = -3,
    LSD_ERR_FORMAT        = -4,
    LSD_ERR_INTERNAL      = -5,
    LSD_ERR_STATE         = -6,
} lsd_status;

// ============================================================
// LSD file header (disk format, little-endian, packed)
// ============================================================

#pragma pack(push, 1)
typedef struct lsd_header {
    char magic[8];                      // "LingVo\0\0"
    uint32_t version;                   // Version number
    uint32_t unk;                       // Unknown
    uint32_t checksum;                  // Checksum
    uint32_t entries_count;             // Number of entries
    uint32_t annotation_offset;         // Annotation offset
    uint32_t dictionary_encoder_offset; // Decoder offset
    uint32_t articles_offset;           // Articles offset
    uint32_t pages_offset;              // Pages offset
    uint32_t unk1;                      // Unknown
    uint16_t last_page;                 // Last page number
    uint16_t unk3;                      // Unknown
    uint16_t source_language;           // Source language
    uint16_t target_language;           // Target language
} lsd_header;
#pragma pack(pop)

// ============================================================
// Entry heading (ArticleHeading)
// ============================================================
typedef struct lsd_heading {
    uint16_t *text;          // Entry text (UTF-16)
    size_t text_length;      // Text length
    uint32_t reference;      // Article reference (offset)

    // Extended information (used for DSL format output)
    uint8_t *ext_data;       // Extended data
    size_t ext_data_length;
} lsd_heading;

// ============================================================
// Entry operations
// ============================================================

/**
 * Free an entry heading
 */
void lsd_heading_destroy(lsd_heading *heading);

/**
 * Get the plain text of an entry heading (UTF-16)
 * @param heading The entry heading
 * @return Text (do not free)
 */
const uint16_t *lsd_heading_get_text(const lsd_heading *heading);

/**
 * Get the article reference of an entry heading
 */
uint32_t lsd_heading_get_reference(const lsd_heading *heading);

#ifdef __cplusplus
}
#endif

#endif /* lsd_types_h */

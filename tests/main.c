//
//  main.c
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#include "unity.h"
#include "lsd_reader.h"
#include "lsd_decoder.h"
#include "lsa_reader.h"
#include "lsd_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test suite declarations
void run_utils_tests(void);
void run_bitstream_tests(void);
void run_reader_tests(void);

// ============================================================
// 打印前 N 个词条
// ============================================================

static void print_first_n_entries(const char *path, int n) {
    printf("Opening: %s\n\n", path);

    lsd_reader *reader = NULL;
    if (lsd_reader_open(path, &reader) != LSD_OK) {
        fprintf(stderr, "Failed to open: %s\n", path);
        return;
    }

    const lsd_header *hdr = lsd_reader_get_header(reader);
    if (!hdr || !lsd_is_version_supported(hdr->version)) {
        fprintf(stderr, "Unsupported version: 0x%08X\n", hdr ? hdr->version : 0);
        lsd_reader_close(reader);
        return;
    }

    // 打印词典基本信息
    char *name = NULL;
    lsd_reader_get_name(reader, &name);
    printf("Dictionary: %s\n", name ? name : "(unknown)");
    printf("Version:    0x%08X\n", hdr->version);
    printf("Entries:    %u\n", hdr->entries_count);
    printf("\n--- First %d entries ---\n\n", n);

    uint8_t *odata = NULL;
    size_t osize;
    lsd_reader_read_overlay(reader, "for_sale_2.jpg", &odata, &osize);
    FILE *fp = fopen("/Users/kejinlu/Desktop/tttt.jpg", "wb");
    fwrite(odata, 1, osize, fp);
    fclose(fp);

    // 使用迭代器遍历并打印前 n 个词条
    lsd_heading_iter *it = lsd_heading_iter_create(reader);
    const lsd_heading *h;
    int count = 0;
    while (lsd_heading_iter_next(it, &h) == LSD_OK && count < n) {
        char *text = NULL;
        lsd_utf16_to_utf8(h->text, h->text_length, &text);
        printf("%4d. %s\n", count + 1, text ? text : "(null)");

        char *article = NULL;
        if (lsd_reader_read_article(reader, h->reference, &article) == LSD_OK && article) {
            printf("     %s\n", article);
            free(article);
        }

        free(text);
        count++;
    }
    lsd_heading_iter_destroy(it);

    printf("\n--- End (showed %d entries) ---\n", count);

    free(name);
    lsd_reader_close(reader);
}

// ============================================================
// LSA 测试
// ============================================================

static void test_lsa_reader(const char *path) {
    printf("\n========================================\n");
    printf("  LSA Reader Test\n");
    printf("========================================\n\n");
    printf("Opening: %s\n\n", path);

    lsa_reader *lsa = lsa_reader_open(path);
    if (!lsa) {
        fprintf(stderr, "Failed to open LSA file: %s\n", path);
        return;
    }

    size_t count = lsa_reader_get_entry_count(lsa);
    printf("Entries: %zu\n\n", count);

    // 列出前 10 个条目名称
    printf("--- First 10 entries ---\n\n");
    for (size_t i = 0; i < count && i < 10; i++) {
        const char *name = lsa_reader_get_entry_name(lsa, i);
        printf("%4zu. %s\n", i + 1, name ? name : "(null)");
    }

    // 导出前两条音频到桌面
    printf("\n--- Decode & Export ---\n\n");
    for (size_t i = 0; i < 2 && i < count; i++) {
        int16_t *pcm = NULL;
        size_t size = 0;
        int rate = 0, ch = 0;

        if (lsa_reader_decode(lsa, i, &pcm, &size, &rate, &ch)) {
            char out_path[256];
            snprintf(out_path, sizeof(out_path),
                     "/Users/kejinlu/Desktop/lsa_entry%zu.wav", i + 1);

            // 手动写 WAV
            FILE *fp = fopen(out_path, "wb");
            if (fp && size > 0) {
                // WAV header
                uint32_t data_size = (uint32_t)size;
                uint32_t riff_size = 36 + data_size;
                uint16_t fmt = 1, bits = 16, nch = (uint16_t)ch;
                uint32_t sr = (uint32_t)rate;
                uint16_t block_align = nch * 2;
                uint32_t byte_rate = sr * block_align;
                fwrite("RIFF", 1, 4, fp);
                fwrite(&riff_size, 4, 1, fp);
                fwrite("WAVEfmt ", 1, 8, fp);
                uint32_t fmt_size = 16;
                fwrite(&fmt_size, 4, 1, fp);
                fwrite(&fmt, 2, 1, fp);
                fwrite(&nch, 2, 1, fp);
                fwrite(&sr, 4, 1, fp);
                fwrite(&byte_rate, 4, 1, fp);
                fwrite(&block_align, 2, 1, fp);
                fwrite(&bits, 2, 1, fp);
                fwrite("data", 1, 4, fp);
                fwrite(&data_size, 4, 1, fp);
                fwrite(pcm, 1, size, fp);
                fclose(fp);
                printf("Saved: %s (%dHz, %dch, %zu bytes)\n",
                       out_path, rate, ch, size);
            }
            free(pcm);
        } else {
            printf("Decode failed: entry %zu\n", i);
        }
    }

    lsa_reader_close(lsa);
}

// ============================================================
// main
// ============================================================

int main(void) {
    run_utils_tests();
    run_bitstream_tests();
    run_reader_tests();
    return 0;
}

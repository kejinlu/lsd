//
//  test_reader.c
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#include "unity.h"
#include "lsd_reader.h"
#include "lsd_decoder.h"
#include "lsd_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// Test data path helper
// ============================================================

static const char *data_dir(void) {
    static char dir[1024] = {0};
    if (dir[0] == '\0') {
        const char *file = __FILE__;
        const char *last_slash = NULL;
        for (const char *p = file; *p; p++) {
            if (*p == '/' || *p == '\\') last_slash = p;
        }
        if (last_slash) {
            int dir_len = (int)(last_slash - file);
            snprintf(dir, sizeof(dir), "%.*s/data/", dir_len, file);
        }
    }
    return dir;
}

static const char *lsd_path(const char *filename) {
    static char path[1024];
    snprintf(path, sizeof(path), "%s%s", data_dir(), filename);
    return path;
}

// ============================================================
// Helper: open + assert
// ============================================================

static lsd_reader *open_assert(const char *filename) {
    lsd_reader *r = NULL;
    lsd_status st = lsd_reader_open(lsd_path(filename), &r);
    TEST_ASSERT_EQUAL_MESSAGE(LSD_OK, st, filename);
    TEST_ASSERT_NOT_NULL_MESSAGE(r, filename);
    return r;
}

// ============================================================
// Tests: open / close
// ============================================================

void test_open_null_path(void) {
    lsd_reader *r = NULL;
    TEST_ASSERT_EQUAL(LSD_ERR_INVALID_PARAM, lsd_reader_open(NULL, &r));
    TEST_ASSERT_NULL(r);
}

void test_open_nonexistent(void) {
    lsd_reader *r = NULL;
    lsd_status st = lsd_reader_open("/tmp/nonexistent_lsd_file_12345.lsd", &r);
    TEST_ASSERT_TRUE(st != LSD_OK);
    TEST_ASSERT_NULL(r);
}

void test_close_null(void) {
    lsd_reader_close(NULL);  // should not crash
}

// ============================================================
// Tests: system_14_activederu (v14 system, De-Ru, 1678 entries)
// ============================================================

void test_system14_open(void) {
    lsd_reader *r = open_assert("system_14_activederu.lsd");
    const lsd_header *h = lsd_reader_get_header(r);
    TEST_ASSERT_TRUE(lsd_is_version_supported(h->version));
    TEST_ASSERT_EQUAL_UINT(0x00141004, h->version);
    TEST_ASSERT_EQUAL_UINT(1678, h->entries_count);
    TEST_ASSERT_EQUAL_UINT(73, h->last_page + 1);
    TEST_ASSERT_EQUAL_UINT16(0x0407, h->source_language);
    TEST_ASSERT_EQUAL_UINT16(0x0419, h->target_language);
    lsd_reader_close(r);
}

void test_system14_name(void) {
    lsd_reader *r = open_assert("system_14_activederu.lsd");
    char *name = NULL;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_get_name(r, &name));
    TEST_ASSERT_EQUAL_STRING("Active (De-Ru)", name);
    free(name);
    lsd_reader_close(r);
}

void test_system14_iter_first2(void) {
    lsd_reader *r = open_assert("system_14_activederu.lsd");
    lsd_heading_iter *it = lsd_heading_iter_create(r);
    TEST_ASSERT_NOT_NULL(it);

    // heading 1: 'rein
    const lsd_heading *h = NULL;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_heading_iter_next(it, &h));
    TEST_ASSERT_NOT_NULL(h);
    char *text = NULL;
    lsd_utf16_to_utf8(h->text, h->text_length, &text);
    TEST_ASSERT_EQUAL_STRING("'rein", text);
    free(text);

    // heading 2: Abend
    TEST_ASSERT_EQUAL(LSD_OK, lsd_heading_iter_next(it, &h));
    TEST_ASSERT_NOT_NULL(h);
    lsd_utf16_to_utf8(h->text, h->text_length, &text);
    TEST_ASSERT_EQUAL_STRING("Abend", text);
    free(text);

    lsd_heading_iter_destroy(it);
    lsd_reader_close(r);
}

void test_system14_find_rein(void) {
    lsd_reader *r = open_assert("system_14_activederu.lsd");
    lsd_heading h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_find_heading(r, "'rein", &h));
    TEST_ASSERT_EQUAL_UINT32(0, h.reference);
    lsd_heading_destroy(&h);
    lsd_reader_close(r);
}

void test_system14_find_Abend(void) {
    lsd_reader *r = open_assert("system_14_activederu.lsd");
    lsd_heading h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_find_heading(r, "Abend", &h));
    TEST_ASSERT_EQUAL_UINT32(29, h.reference);
    lsd_heading_destroy(&h);
    lsd_reader_close(r);
}

void test_system14_read_article_Abend(void) {
    lsd_reader *r = open_assert("system_14_activederu.lsd");
    char *article = NULL;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_read_article(r, 29, &article));
    TEST_ASSERT_NOT_NULL(article);
    TEST_ASSERT_NOT_NULL(strstr(article, "[/trn]"));
    TEST_ASSERT_NOT_NULL(strstr(article, "вечер"));
    free(article);
    lsd_reader_close(r);
}

void test_system14_iter_count(void) {
    lsd_reader *r = open_assert("system_14_activederu.lsd");
    lsd_heading_iter *it = lsd_heading_iter_create(r);
    int count = 0;
    const lsd_heading *h = NULL;
    while (lsd_heading_iter_next(it, &h) == LSD_OK) count++;
    TEST_ASSERT_EQUAL_INT(1678, count);
    lsd_heading_iter_destroy(it);
    lsd_reader_close(r);
}

void test_system14_prefix(void) {
    lsd_reader *r = open_assert("system_14_activederu.lsd");
    lsd_heading *results = NULL;
    size_t count = 0;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_prefix(r, "Ab", 0, &results, &count));
    TEST_ASSERT_EQUAL_size_t(13, count);
    char *text = NULL;
    lsd_utf16_to_utf8(results[0].text, results[0].text_length, &text);
    TEST_ASSERT_EQUAL_STRING("Abend", text);
    free(text);
    for (size_t i = 0; i < count; i++) lsd_heading_destroy(&results[i]);
    free(results);
    lsd_reader_close(r);
}

// ============================================================
// Tests: system_15_activederu (v15 system, De-Ru, 1678 entries)
// ============================================================

void test_system15_open(void) {
    lsd_reader *r = open_assert("system_15_activederu.lsd");
    const lsd_header *h = lsd_reader_get_header(r);
    TEST_ASSERT_EQUAL_UINT(0x00151005, h->version);
    TEST_ASSERT_EQUAL_UINT(1678, h->entries_count);
    lsd_reader_close(r);
}

void test_system15_iter_first2(void) {
    lsd_reader *r = open_assert("system_15_activederu.lsd");
    lsd_heading_iter *it = lsd_heading_iter_create(r);
    const lsd_heading *h = NULL;

    TEST_ASSERT_EQUAL(LSD_OK, lsd_heading_iter_next(it, &h));
    TEST_ASSERT_NOT_NULL(h);
    char *text = NULL;
    lsd_utf16_to_utf8(h->text, h->text_length, &text);
    TEST_ASSERT_EQUAL_STRING("'rein", text);
    free(text);

    TEST_ASSERT_EQUAL(LSD_OK, lsd_heading_iter_next(it, &h));
    TEST_ASSERT_NOT_NULL(h);
    lsd_utf16_to_utf8(h->text, h->text_length, &text);
    TEST_ASSERT_EQUAL_STRING("Abend", text);
    free(text);

    lsd_heading_iter_destroy(it);
    lsd_reader_close(r);
}

void test_system15_find_Haus(void) {
    lsd_reader *r = open_assert("system_15_activederu.lsd");
    lsd_heading h1, h2;
    memset(&h1, 0, sizeof(h1));
    memset(&h2, 0, sizeof(h2));
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_find_heading(r, "Haus", &h1));
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_find_heading(r, "haus", &h2));
    TEST_ASSERT_EQUAL_UINT32(h1.reference, h2.reference);
    lsd_heading_destroy(&h1);
    lsd_heading_destroy(&h2);
    lsd_reader_close(r);
}

void test_system15_read_article_Haus(void) {
    lsd_reader *r = open_assert("system_15_activederu.lsd");
    lsd_heading h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_find_heading(r, "Haus", &h));
    char *article = NULL;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_read_article(r, h.reference, &article));
    TEST_ASSERT_NOT_NULL(article);
    TEST_ASSERT_NOT_NULL(strstr(article, "[/trn]"));
    TEST_ASSERT_NOT_NULL(strstr(article, "дом"));
    free(article);
    lsd_heading_destroy(&h);
    lsd_reader_close(r);
}

void test_system15_annotation(void) {
    lsd_reader *r = open_assert("system_15_activederu.lsd");
    char *annotation = NULL;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_read_annotation(r, &annotation));
    TEST_ASSERT_NOT_NULL(annotation);
    TEST_ASSERT_NOT_NULL(strstr(annotation, "Russian"));
    TEST_ASSERT_NOT_NULL(strstr(annotation, "Active (De-Ru)"));
    free(annotation);
    lsd_reader_close(r);
}

void test_system15_prefix(void) {
    lsd_reader *r = open_assert("system_15_activederu.lsd");
    lsd_heading *results = NULL;
    size_t count = 0;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_prefix(r, "Ab", 0, &results, &count));
    TEST_ASSERT_EQUAL_size_t(13, count);
    char *text = NULL;
    lsd_utf16_to_utf8(results[0].text, results[0].text_length, &text);
    TEST_ASSERT_EQUAL_STRING("Abend", text);
    free(text);
    for (size_t i = 0; i < count; i++) lsd_heading_destroy(&results[i]);
    free(results);
    lsd_reader_close(r);
}

// ============================================================
// Tests: user_11 (v11 user, En-Ru, 503 entries)
// ============================================================

void test_user11_open(void) {
    lsd_reader *r = open_assert("user_11_international_lighting_vocabulary_cie_publ_no_17.lsd");
    const lsd_header *h = lsd_reader_get_header(r);
    TEST_ASSERT_EQUAL_UINT(0x00110001, h->version);
    TEST_ASSERT_EQUAL_UINT(503, h->entries_count);
    TEST_ASSERT_EQUAL_UINT(13, h->last_page + 1);
    lsd_reader_close(r);
}

void test_user11_iter_first2(void) {
    lsd_reader *r = open_assert("user_11_international_lighting_vocabulary_cie_publ_no_17.lsd");
    lsd_heading_iter *it = lsd_heading_iter_create(r);
    const lsd_heading *h = NULL;
    char *text = NULL;

    TEST_ASSERT_EQUAL(LSD_OK, lsd_heading_iter_next(it, &h));
    TEST_ASSERT_NOT_NULL(h);
    lsd_utf16_to_utf8(h->text, h->text_length, &text);
    TEST_ASSERT_EQUAL_STRING("Abbey's law", text);
    free(text);

    TEST_ASSERT_EQUAL(LSD_OK, lsd_heading_iter_next(it, &h));
    TEST_ASSERT_NOT_NULL(h);
    lsd_utf16_to_utf8(h->text, h->text_length, &text);
    TEST_ASSERT_EQUAL_STRING("Abney phenomenon", text);
    free(text);

    lsd_heading_iter_destroy(it);
    lsd_reader_close(r);
}

void test_user11_find_and_article(void) {
    lsd_reader *r = open_assert("user_11_international_lighting_vocabulary_cie_publ_no_17.lsd");
    lsd_heading h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_find_heading(r, "Abbey's law", &h));
    TEST_ASSERT_EQUAL_UINT32(0, h.reference);

    char *article = NULL;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_read_article(r, h.reference, &article));
    TEST_ASSERT_NOT_NULL(strstr(article, "закон Эбни"));
    free(article);
    lsd_heading_destroy(&h);
    lsd_reader_close(r);
}

void test_user11_prefix(void) {
    lsd_reader *r = open_assert("user_11_international_lighting_vocabulary_cie_publ_no_17.lsd");
    lsd_heading *results = NULL;
    size_t count = 0;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_prefix(r, "Ab", 0, &results, &count));
    TEST_ASSERT_EQUAL_size_t(6, count);
    char *text = NULL;
    lsd_utf16_to_utf8(results[0].text, results[0].text_length, &text);
    TEST_ASSERT_EQUAL_STRING("Abbey's law", text);
    free(text);
    for (size_t i = 0; i < count; i++) lsd_heading_destroy(&results[i]);
    free(results);
    lsd_reader_close(r);
}

// ============================================================
// Tests: user_12 (v12 user, En-Ru, 4833 entries)
// ============================================================

void test_user12_open(void) {
    lsd_reader *r = open_assert("user_12_accountingenru.lsd");
    const lsd_header *h = lsd_reader_get_header(r);
    TEST_ASSERT_EQUAL_UINT(0x00120001, h->version);
    TEST_ASSERT_EQUAL_UINT(4833, h->entries_count);
    lsd_reader_close(r);
}

void test_user12_iter_first2(void) {
    lsd_reader *r = open_assert("user_12_accountingenru.lsd");
    lsd_heading_iter *it = lsd_heading_iter_create(r);
    const lsd_heading *h = NULL;
    char *text = NULL;

    TEST_ASSERT_EQUAL(LSD_OK, lsd_heading_iter_next(it, &h));
    TEST_ASSERT_NOT_NULL(h);
    lsd_utf16_to_utf8(h->text, h->text_length, &text);
    TEST_ASSERT_EQUAL_STRING("150 percent declining balance depreciation", text);
    free(text);

    TEST_ASSERT_EQUAL(LSD_OK, lsd_heading_iter_next(it, &h));
    TEST_ASSERT_NOT_NULL(h);
    lsd_utf16_to_utf8(h->text, h->text_length, &text);
    TEST_ASSERT_EQUAL_STRING("150 percent declining balance depreciation method", text);
    free(text);

    lsd_heading_iter_destroy(it);
    lsd_reader_close(r);
}

void test_user12_find_and_article(void) {
    lsd_reader *r = open_assert("user_12_accountingenru.lsd");
    lsd_heading h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_find_heading(r, "150 percent declining balance depreciation", &h));
    TEST_ASSERT_EQUAL_UINT32(0, h.reference);

    char *article = NULL;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_read_article(r, h.reference, &article));
    TEST_ASSERT_NOT_NULL(strstr(article, "амортизация"));
    free(article);
    lsd_heading_destroy(&h);
    lsd_reader_close(r);
}

void test_user12_prefix(void) {
    lsd_reader *r = open_assert("user_12_accountingenru.lsd");
    lsd_heading *results = NULL;
    size_t count = 0;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_prefix(r, "acc", 0, &results, &count));
    TEST_ASSERT_EQUAL_size_t(204, count);
    char *text = NULL;
    lsd_utf16_to_utf8(results[0].text, results[0].text_length, &text);
    TEST_ASSERT_EQUAL_STRING("ACCA", text);
    free(text);
    for (size_t i = 0; i < count; i++) lsd_heading_destroy(&results[i]);
    free(results);
    lsd_reader_close(r);
}

// ============================================================
// Tests: user_13 (v13 user, Ru-Be, 36 entries)
// ============================================================

void test_user13_open(void) {
    lsd_reader *r = open_assert("user_13_ru_be_false_friends_yzb_1_0_x3.lsd");
    const lsd_header *h = lsd_reader_get_header(r);
    TEST_ASSERT_EQUAL_UINT(0x00132001, h->version);
    TEST_ASSERT_EQUAL_UINT(36, h->entries_count);
    TEST_ASSERT_EQUAL_UINT(1, h->last_page + 1);
    TEST_ASSERT_EQUAL_UINT16(0x0419, h->source_language);
    TEST_ASSERT_EQUAL_UINT16(0x0423, h->target_language);
    lsd_reader_close(r);
}

void test_user13_iter_first2(void) {
    lsd_reader *r = open_assert("user_13_ru_be_false_friends_yzb_1_0_x3.lsd");
    lsd_heading_iter *it = lsd_heading_iter_create(r);
    const lsd_heading *h = NULL;
    char *text = NULL;

    TEST_ASSERT_EQUAL(LSD_OK, lsd_heading_iter_next(it, &h));
    TEST_ASSERT_NOT_NULL(h);
    lsd_utf16_to_utf8(h->text, h->text_length, &text);
    TEST_ASSERT_EQUAL_STRING("арбуз", text);
    free(text);

    TEST_ASSERT_EQUAL(LSD_OK, lsd_heading_iter_next(it, &h));
    TEST_ASSERT_NOT_NULL(h);
    lsd_utf16_to_utf8(h->text, h->text_length, &text);
    TEST_ASSERT_EQUAL_STRING("бабка", text);
    free(text);

    lsd_heading_iter_destroy(it);
    lsd_reader_close(r);
}

void test_user13_find_and_article(void) {
    lsd_reader *r = open_assert("user_13_ru_be_false_friends_yzb_1_0_x3.lsd");
    lsd_heading h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_find_heading(r, "арбуз", &h));
    TEST_ASSERT_EQUAL_UINT32(0, h.reference);

    char *article = NULL;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_read_article(r, h.reference, &article));
    TEST_ASSERT_NOT_NULL(strstr(article, "кавун"));
    free(article);
    lsd_heading_destroy(&h);
    lsd_reader_close(r);
}

void test_user13_prefix(void) {
    lsd_reader *r = open_assert("user_13_ru_be_false_friends_yzb_1_0_x3.lsd");
    lsd_heading *results = NULL;
    size_t count = 0;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_prefix(r, "бабк", 0, &results, &count));
    TEST_ASSERT_EQUAL_size_t(1, count);
    char *text = NULL;
    lsd_utf16_to_utf8(results[0].text, results[0].text_length, &text);
    TEST_ASSERT_EQUAL_STRING("бабка", text);
    free(text);
    for (size_t i = 0; i < count; i++) lsd_heading_destroy(&results[i]);
    free(results);
    lsd_reader_close(r);
}

// ============================================================
// Tests: user_14 (v14 user, Eng-Rus, 9537 entries)
// ============================================================

void test_user14_open(void) {
    lsd_reader *r = open_assert("user_14_eng_rus_greatbritain_x5.lsd");
    const lsd_header *h = lsd_reader_get_header(r);
    TEST_ASSERT_EQUAL_UINT(0x00142001, h->version);
    TEST_ASSERT_EQUAL_UINT(9537, h->entries_count);
    TEST_ASSERT_EQUAL_UINT(204, h->last_page + 1);
    lsd_reader_close(r);
}

void test_user14_iter_first2(void) {
    lsd_reader *r = open_assert("user_14_eng_rus_greatbritain_x5.lsd");
    lsd_heading_iter *it = lsd_heading_iter_create(r);
    const lsd_heading *h = NULL;
    char *text = NULL;

    TEST_ASSERT_EQUAL(LSD_OK, lsd_heading_iter_next(it, &h));
    TEST_ASSERT_NOT_NULL(h);
    lsd_utf16_to_utf8(h->text, h->text_length, &text);
    TEST_ASSERT_EQUAL_STRING("'Arry", text);
    free(text);

    TEST_ASSERT_EQUAL(LSD_OK, lsd_heading_iter_next(it, &h));
    TEST_ASSERT_NOT_NULL(h);
    lsd_utf16_to_utf8(h->text, h->text_length, &text);
    TEST_ASSERT_EQUAL_STRING("-shire", text);
    free(text);

    lsd_heading_iter_destroy(it);
    lsd_reader_close(r);
}

void test_user14_find_and_article(void) {
    lsd_reader *r = open_assert("user_14_eng_rus_greatbritain_x5.lsd");
    lsd_heading h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_find_heading(r, "'Arry", &h));
    TEST_ASSERT_EQUAL_UINT32(0, h.reference);

    char *article = NULL;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_read_article(r, h.reference, &article));
    TEST_ASSERT_NOT_NULL(strstr(article, "[/trn]"));
    free(article);
    lsd_heading_destroy(&h);
    lsd_reader_close(r);
}

void test_user14_iter_count(void) {
    lsd_reader *r = open_assert("user_14_eng_rus_greatbritain_x5.lsd");
    lsd_heading_iter *it = lsd_heading_iter_create(r);
    int count = 0;
    const lsd_heading *h = NULL;
    while (lsd_heading_iter_next(it, &h) == LSD_OK) count++;
    TEST_ASSERT_EQUAL_INT(9537, count);
    lsd_heading_iter_destroy(it);
    lsd_reader_close(r);
}

void test_user14_prefix(void) {
    lsd_reader *r = open_assert("user_14_eng_rus_greatbritain_x5.lsd");
    lsd_heading *results = NULL;
    size_t count = 0;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_prefix(r, "ab", 0, &results, &count));
    TEST_ASSERT_EQUAL_size_t(22, count);
    char *text = NULL;
    lsd_utf16_to_utf8(results[0].text, results[0].text_length, &text);
    TEST_ASSERT_EQUAL_STRING("abbess", text);
    free(text);
    for (size_t i = 0; i < count; i++) lsd_heading_destroy(&results[i]);
    free(results);
    lsd_reader_close(r);
}

// ============================================================
// Tests: user_legacy (v13 legacy user, En-Ru, 4832 entries)
// ============================================================

void test_user_legacy_open(void) {
    lsd_reader *r = open_assert("user_legacy_accountingenru.lsd");
    const lsd_header *h = lsd_reader_get_header(r);
    TEST_ASSERT_EQUAL_UINT(0x00131001, h->version);
    TEST_ASSERT_EQUAL_UINT(4832, h->entries_count);
    lsd_reader_close(r);
}

void test_user_legacy_iter_first2(void) {
    lsd_reader *r = open_assert("user_legacy_accountingenru.lsd");
    lsd_heading_iter *it = lsd_heading_iter_create(r);
    const lsd_heading *h = NULL;
    char *text = NULL;

    TEST_ASSERT_EQUAL(LSD_OK, lsd_heading_iter_next(it, &h));
    TEST_ASSERT_NOT_NULL(h);
    lsd_utf16_to_utf8(h->text, h->text_length, &text);
    TEST_ASSERT_EQUAL_STRING("150 percent declining balance depreciation", text);
    free(text);

    TEST_ASSERT_EQUAL(LSD_OK, lsd_heading_iter_next(it, &h));
    TEST_ASSERT_NOT_NULL(h);
    lsd_utf16_to_utf8(h->text, h->text_length, &text);
    TEST_ASSERT_EQUAL_STRING("150 percent declining balance depreciation method", text);
    free(text);

    lsd_heading_iter_destroy(it);
    lsd_reader_close(r);
}

void test_user_legacy_find_and_article(void) {
    lsd_reader *r = open_assert("user_legacy_accountingenru.lsd");
    lsd_heading h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_find_heading(r, "150 percent declining balance depreciation", &h));
    TEST_ASSERT_EQUAL_UINT32(0, h.reference);

    char *article = NULL;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_read_article(r, h.reference, &article));
    TEST_ASSERT_NOT_NULL(strstr(article, "амортизация"));
    free(article);
    lsd_heading_destroy(&h);
    lsd_reader_close(r);
}

void test_user_legacy_prefix(void) {
    lsd_reader *r = open_assert("user_legacy_accountingenru.lsd");
    lsd_heading *results = NULL;
    size_t count = 0;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_prefix(r, "acc", 0, &results, &count));
    TEST_ASSERT_EQUAL_size_t(204, count);
    char *text = NULL;
    lsd_utf16_to_utf8(results[0].text, results[0].text_length, &text);
    TEST_ASSERT_EQUAL_STRING("ACCA", text);
    free(text);
    for (size_t i = 0; i < count; i++) lsd_heading_destroy(&results[i]);
    free(results);
    lsd_reader_close(r);
}

// ============================================================
// Tests: abbr_14 (v14 abbreviation, 137 entries)
// ============================================================

void test_abbr14_open(void) {
    lsd_reader *r = open_assert("abbr_14_eng_rus_greatbritain_x5_abrv.lsd");
    const lsd_header *h = lsd_reader_get_header(r);
    TEST_ASSERT_EQUAL_UINT(0x00145001, h->version);
    TEST_ASSERT_EQUAL_UINT(137, h->entries_count);
    TEST_ASSERT_EQUAL_UINT(3, h->last_page + 1);
    lsd_reader_close(r);
}

void test_abbr14_iter_first2(void) {
    lsd_reader *r = open_assert("abbr_14_eng_rus_greatbritain_x5_abrv.lsd");
    lsd_heading_iter *it = lsd_heading_iter_create(r);
    const lsd_heading *h = NULL;
    char *text = NULL;

    TEST_ASSERT_EQUAL(LSD_OK, lsd_heading_iter_next(it, &h));
    TEST_ASSERT_NOT_NULL(h);
    lsd_utf16_to_utf8(h->text, h->text_length, &text);
    TEST_ASSERT_EQUAL_STRING("австрал.", text);
    free(text);

    TEST_ASSERT_EQUAL(LSD_OK, lsd_heading_iter_next(it, &h));
    TEST_ASSERT_NOT_NULL(h);
    lsd_utf16_to_utf8(h->text, h->text_length, &text);
    TEST_ASSERT_EQUAL_STRING("амер.", text);
    free(text);

    lsd_heading_iter_destroy(it);
    lsd_reader_close(r);
}

void test_abbr14_find_and_article(void) {
    lsd_reader *r = open_assert("abbr_14_eng_rus_greatbritain_x5_abrv.lsd");
    lsd_heading h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_find_heading(r, "австрал.", &h));
    TEST_ASSERT_EQUAL_UINT32(0, h.reference);

    char *article = NULL;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_read_article(r, h.reference, &article));
    TEST_ASSERT_NOT_NULL(strstr(article, "Австралии"));
    free(article);
    lsd_heading_destroy(&h);
    lsd_reader_close(r);
}

void test_abbr14_prefix(void) {
    lsd_reader *r = open_assert("abbr_14_eng_rus_greatbritain_x5_abrv.lsd");
    lsd_heading *results = NULL;
    size_t count = 0;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_prefix(r, "ам", 0, &results, &count));
    TEST_ASSERT_EQUAL_size_t(1, count);
    char *text = NULL;
    lsd_utf16_to_utf8(results[0].text, results[0].text_length, &text);
    TEST_ASSERT_EQUAL_STRING("амер.", text);
    free(text);
    for (size_t i = 0; i < count; i++) lsd_heading_destroy(&results[i]);
    free(results);
    lsd_reader_close(r);
}

// ============================================================
// Tests: abbr_15 (v15 abbreviation, 942 entries)
// ============================================================

void test_abbr15_open(void) {
    lsd_reader *r = open_assert("abbr_15_abbrev.lsd");
    const lsd_header *h = lsd_reader_get_header(r);
    TEST_ASSERT_EQUAL_UINT(0x00155001, h->version);
    TEST_ASSERT_EQUAL_UINT(942, h->entries_count);
    lsd_reader_close(r);
}

void test_abbr15_iter_first2(void) {
    lsd_reader *r = open_assert("abbr_15_abbrev.lsd");
    lsd_heading_iter *it = lsd_heading_iter_create(r);
    const lsd_heading *h = NULL;
    char *text = NULL;

    TEST_ASSERT_EQUAL(LSD_OK, lsd_heading_iter_next(it, &h));
    TEST_ASSERT_NOT_NULL(h);
    lsd_utf16_to_utf8(h->text, h->text_length, &text);
    TEST_ASSERT_EQUAL_STRING("&=", text);
    free(text);

    TEST_ASSERT_EQUAL(LSD_OK, lsd_heading_iter_next(it, &h));
    TEST_ASSERT_NOT_NULL(h);
    lsd_utf16_to_utf8(h->text, h->text_length, &text);
    TEST_ASSERT_EQUAL_STRING("*", text);
    free(text);

    lsd_heading_iter_destroy(it);
    lsd_reader_close(r);
}

void test_abbr15_find_and_article(void) {
    lsd_reader *r = open_assert("abbr_15_abbrev.lsd");
    lsd_heading h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_find_heading(r, "&=", &h));
    TEST_ASSERT_EQUAL_UINT32(0, h.reference);

    char *article = NULL;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_read_article(r, h.reference, &article));
    TEST_ASSERT_NOT_NULL(strstr(article, "родительного"));
    free(article);
    lsd_heading_destroy(&h);
    lsd_reader_close(r);
}

void test_abbr15_prefix(void) {
    lsd_reader *r = open_assert("abbr_15_abbrev.lsd");
    lsd_heading *results = NULL;
    size_t count = 0;
    TEST_ASSERT_EQUAL(LSD_OK, lsd_reader_prefix(r, "ад", 0, &results, &count));
    TEST_ASSERT_EQUAL_size_t(1, count);
    char *text = NULL;
    lsd_utf16_to_utf8(results[0].text, results[0].text_length, &text);
    TEST_ASSERT_EQUAL_STRING("адм.-терр.", text);
    free(text);
    for (size_t i = 0; i < count; i++) lsd_heading_destroy(&results[i]);
    free(results);
    lsd_reader_close(r);
}

// ============================================================
// Tests: cross-version find_heading edge cases
// ============================================================

void test_find_heading_null_args(void) {
    lsd_heading h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL(LSD_ERR_INVALID_PARAM, lsd_reader_find_heading(NULL, "test", &h));

    lsd_reader *r = open_assert("system_15_activederu.lsd");
    TEST_ASSERT_EQUAL(LSD_ERR_INVALID_PARAM, lsd_reader_find_heading(r, NULL, &h));
    lsd_reader_close(r);
}

void test_find_heading_not_exists(void) {
    lsd_reader *r = open_assert("system_15_activederu.lsd");
    lsd_heading h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL(LSD_NOT_FOUND, lsd_reader_find_heading(r, "ZZZZZnonexistent", &h));
    lsd_reader_close(r);
}

// ============================================================
// Tests: prefix search edge cases
// ============================================================

void test_prefix_no_match(void) {
    lsd_reader *r = open_assert("system_15_activederu.lsd");
    lsd_heading *results = NULL;
    size_t count = 0;
    TEST_ASSERT_EQUAL(LSD_NOT_FOUND, lsd_reader_prefix(r, "zzzzz", 10, &results, &count));
    lsd_reader_close(r);
}

void test_prefix_null_args(void) {
    lsd_heading *results = NULL;
    size_t count = 0;
    TEST_ASSERT_EQUAL(LSD_ERR_INVALID_PARAM, lsd_reader_prefix(NULL, "Ab", 5, &results, &count));

    lsd_reader *r = open_assert("system_15_activederu.lsd");
    TEST_ASSERT_EQUAL(LSD_ERR_INVALID_PARAM, lsd_reader_prefix(r, NULL, 5, &results, &count));
    TEST_ASSERT_EQUAL(LSD_ERR_INVALID_PARAM, lsd_reader_prefix(r, "Ab", 5, NULL, &count));
    TEST_ASSERT_EQUAL(LSD_ERR_INVALID_PARAM, lsd_reader_prefix(r, "Ab", 5, &results, NULL));
    lsd_reader_close(r);
}

void test_iter_null_args(void) {
    TEST_ASSERT_NULL(lsd_heading_iter_create(NULL));
    const lsd_heading *h = NULL;
    TEST_ASSERT_EQUAL(LSD_ERR_INVALID_PARAM, lsd_heading_iter_next(NULL, &h));
    lsd_heading_iter_destroy(NULL);  // should not crash
}

// ============================================================
// Tests: get_source_lang / get_target_lang
// ============================================================

void test_lang_null_reader(void) {
    TEST_ASSERT_NULL(lsd_reader_get_source_lang(NULL));
    TEST_ASSERT_NULL(lsd_reader_get_target_lang(NULL));
}

void test_system14_lang(void) {
    lsd_reader *r = open_assert("system_14_activederu.lsd");
    TEST_ASSERT_EQUAL_STRING("de", lsd_reader_get_source_lang(r));
    TEST_ASSERT_EQUAL_STRING("ru", lsd_reader_get_target_lang(r));
    lsd_reader_close(r);
}

void test_system15_lang(void) {
    lsd_reader *r = open_assert("system_15_activederu.lsd");
    TEST_ASSERT_EQUAL_STRING("de", lsd_reader_get_source_lang(r));
    TEST_ASSERT_EQUAL_STRING("ru", lsd_reader_get_target_lang(r));
    lsd_reader_close(r);
}

void test_user13_lang(void) {
    lsd_reader *r = open_assert("user_13_ru_be_false_friends_yzb_1_0_x3.lsd");
    TEST_ASSERT_EQUAL_STRING("ru", lsd_reader_get_source_lang(r));
    TEST_ASSERT_EQUAL_STRING("be", lsd_reader_get_target_lang(r));
    lsd_reader_close(r);
}

// ============================================================
// Test Runner
// ============================================================

void run_reader_tests(void) {
    UnityBegin("test_reader.c");

    // open / close
    RUN_TEST(test_open_null_path);
    RUN_TEST(test_open_nonexistent);
    RUN_TEST(test_close_null);

    // system_14 (v14 system, De-Ru)
    RUN_TEST(test_system14_open);
    RUN_TEST(test_system14_name);
    RUN_TEST(test_system14_iter_first2);
    RUN_TEST(test_system14_find_rein);
    RUN_TEST(test_system14_find_Abend);
    RUN_TEST(test_system14_read_article_Abend);
    RUN_TEST(test_system14_iter_count);
    RUN_TEST(test_system14_prefix);

    // system_15 (v15 system, De-Ru)
    RUN_TEST(test_system15_open);
    RUN_TEST(test_system15_iter_first2);
    RUN_TEST(test_system15_find_Haus);
    RUN_TEST(test_system15_read_article_Haus);
    RUN_TEST(test_system15_annotation);
    RUN_TEST(test_system15_prefix);

    // user_11 (v11 user, En-Ru)
    RUN_TEST(test_user11_open);
    RUN_TEST(test_user11_iter_first2);
    RUN_TEST(test_user11_find_and_article);
    RUN_TEST(test_user11_prefix);

    // user_12 (v12 user, En-Ru)
    RUN_TEST(test_user12_open);
    RUN_TEST(test_user12_iter_first2);
    RUN_TEST(test_user12_find_and_article);
    RUN_TEST(test_user12_prefix);

    // user_13 (v13 user, Ru-Be, Cyrillic)
    RUN_TEST(test_user13_open);
    RUN_TEST(test_user13_iter_first2);
    RUN_TEST(test_user13_find_and_article);
    RUN_TEST(test_user13_prefix);

    // user_14 (v14 user, Eng-Rus)
    RUN_TEST(test_user14_open);
    RUN_TEST(test_user14_iter_first2);
    RUN_TEST(test_user14_find_and_article);
    RUN_TEST(test_user14_iter_count);
    RUN_TEST(test_user14_prefix);

    // user_legacy (v13 legacy user)
    RUN_TEST(test_user_legacy_open);
    RUN_TEST(test_user_legacy_iter_first2);
    RUN_TEST(test_user_legacy_find_and_article);
    RUN_TEST(test_user_legacy_prefix);

    // abbr_14 (v14 abbreviation)
    RUN_TEST(test_abbr14_open);
    RUN_TEST(test_abbr14_iter_first2);
    RUN_TEST(test_abbr14_find_and_article);
    RUN_TEST(test_abbr14_prefix);

    // abbr_15 (v15 abbreviation)
    RUN_TEST(test_abbr15_open);
    RUN_TEST(test_abbr15_iter_first2);
    RUN_TEST(test_abbr15_find_and_article);
    RUN_TEST(test_abbr15_prefix);

    // cross-version edge cases
    RUN_TEST(test_find_heading_null_args);
    RUN_TEST(test_find_heading_not_exists);
    RUN_TEST(test_prefix_no_match);
    RUN_TEST(test_prefix_null_args);
    RUN_TEST(test_iter_null_args);

    // source/target lang
    RUN_TEST(test_lang_null_reader);
    RUN_TEST(test_system14_lang);
    RUN_TEST(test_system15_lang);
    RUN_TEST(test_user13_lang);

    UnityEnd();
}

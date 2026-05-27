//
//  test_dsl.c
//  lsd tests
//
//  Unit tests for dsl_reader (DSL / DSL.DZ format)
//

#include "unity.h"
#include "dsl_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// Test data path helper
// ============================================================

static const char *dsl_data_dir(void) {
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

static const char *dsl_path(const char *filename) {
    static char path[1024];
    snprintf(path, sizeof(path), "%s%s", dsl_data_dir(), filename);
    return path;
}

// ============================================================
// Helper: open + assert
// ============================================================

static dsl_reader *dsl_open_assert(const char *filename) {
    dsl_reader *r = NULL;
    lsd_status st = dsl_reader_open(dsl_path(filename), &r);
    TEST_ASSERT_EQUAL_MESSAGE(LSD_OK, st, filename);
    TEST_ASSERT_NOT_NULL_MESSAGE(r, filename);
    return r;
}

// ============================================================
// Tests: open / close null safety
// ============================================================

void test_dsl_open_null_path(void) {
    dsl_reader *r = NULL;
    TEST_ASSERT_EQUAL(LSD_ERR_INVALID_PARAM, dsl_reader_open(NULL, &r));
    TEST_ASSERT_NULL(r);
}

void test_dsl_open_null_out(void) {
    TEST_ASSERT_EQUAL(LSD_ERR_INVALID_PARAM, dsl_reader_open("test.dsl", NULL));
}

void test_dsl_open_nonexistent(void) {
    dsl_reader *r = NULL;
    lsd_status st = dsl_reader_open("/tmp/nonexistent_dsl_file_12345.dsl", &r);
    TEST_ASSERT_TRUE(st != LSD_OK);
    TEST_ASSERT_NULL(r);
}

void test_dsl_close_null(void) {
    dsl_reader_close(NULL);  // should not crash
}

// ============================================================
// Tests: property accessors null safety
// ============================================================

void test_dsl_get_header_null(void) {
    TEST_ASSERT_NULL(dsl_reader_get_header(NULL));
}

void test_dsl_get_name_null(void) {
    char *name = NULL;
    TEST_ASSERT_EQUAL(LSD_ERR_INVALID_PARAM, dsl_reader_get_name(NULL, &name));
}

void test_dsl_get_source_language_null(void) {
    TEST_ASSERT_NULL(dsl_reader_get_source_language(NULL));
}

void test_dsl_get_target_language_null(void) {
    TEST_ASSERT_NULL(dsl_reader_get_target_language(NULL));
}

void test_dsl_get_metadata_null(void) {
    TEST_ASSERT_NULL(dsl_reader_get_metadata(NULL));
}

void test_dsl_get_encoding_null(void) {
    TEST_ASSERT_EQUAL(DSL_ENCODING_UNKNOWN, dsl_reader_get_encoding(NULL));
}

// ============================================================
// Tests: iterator null safety
// ============================================================

void test_dsl_iter_create_null(void) {
    TEST_ASSERT_NULL(dsl_article_iter_create(NULL));
}

void test_dsl_iter_destroy_null(void) {
    dsl_article_iter_destroy(NULL);  // should not crash
}

void test_dsl_iter_next_null(void) {
    const dsl_article *art = NULL;
    TEST_ASSERT_EQUAL(LSD_ERR_INVALID_PARAM, dsl_article_iter_next(NULL, &art));
}

void test_dsl_iter_next_null_out(void) {
    dsl_reader *r = dsl_open_assert("en_us_ipa.dsl.dz");
    dsl_article_iter *iter = dsl_article_iter_create(r);
    TEST_ASSERT_EQUAL(LSD_ERR_INVALID_PARAM, dsl_article_iter_next(iter, NULL));
    dsl_article_iter_destroy(iter);
    dsl_reader_close(r);
}

// ============================================================
// Tests: en_us_ipa.dsl.dz (dictzip, UTF-16LE, 125927 entries)
// ============================================================

void test_dsl_ipa_open(void) {
    dsl_reader *r = dsl_open_assert("en_us_ipa.dsl.dz");

    const dsl_header *hdr = dsl_reader_get_header(r);
    TEST_ASSERT_NOT_NULL(hdr);
    TEST_ASSERT_EQUAL_STRING("IPA Dictionary - English", hdr->name);
    TEST_ASSERT_EQUAL_STRING("English", hdr->index_language);
    TEST_ASSERT_EQUAL_STRING("English", hdr->contents_language);
    TEST_ASSERT_NULL(hdr->metadata);
    TEST_ASSERT_EQUAL(DSL_ENCODING_UTF16LE, hdr->encoding);

    dsl_reader_close(r);
}

void test_dsl_ipa_get_name(void) {
    dsl_reader *r = dsl_open_assert("en_us_ipa.dsl.dz");

    char *name = NULL;
    TEST_ASSERT_EQUAL(LSD_OK, dsl_reader_get_name(r, &name));
    TEST_ASSERT_EQUAL_STRING("IPA Dictionary - English", name);
    free(name);

    dsl_reader_close(r);
}

void test_dsl_ipa_get_languages(void) {
    dsl_reader *r = dsl_open_assert("en_us_ipa.dsl.dz");

    TEST_ASSERT_EQUAL_STRING("English", dsl_reader_get_source_language(r));
    TEST_ASSERT_EQUAL_STRING("English", dsl_reader_get_target_language(r));

    dsl_reader_close(r);
}

void test_dsl_ipa_encoding(void) {
    dsl_reader *r = dsl_open_assert("en_us_ipa.dsl.dz");
    TEST_ASSERT_EQUAL(DSL_ENCODING_UTF16LE, dsl_reader_get_encoding(r));
    dsl_reader_close(r);
}

void test_dsl_ipa_encoding_name(void) {
    TEST_ASSERT_EQUAL_STRING("UTF-16LE", dsl_encoding_name(DSL_ENCODING_UTF16LE));
    TEST_ASSERT_EQUAL_STRING("UTF-8", dsl_encoding_name(DSL_ENCODING_UTF8));
    TEST_ASSERT_EQUAL_STRING("UTF-16BE", dsl_encoding_name(DSL_ENCODING_UTF16BE));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", dsl_encoding_name(DSL_ENCODING_UNKNOWN));
}

void test_dsl_ipa_iter_first3(void) {
    dsl_reader *r = dsl_open_assert("en_us_ipa.dsl.dz");
    dsl_article_iter *iter = dsl_article_iter_create(r);
    const dsl_article *art = NULL;

    // Article 1: 'bout
    TEST_ASSERT_EQUAL(LSD_OK, dsl_article_iter_next(iter, &art));
    TEST_ASSERT_NOT_NULL(art);
    TEST_ASSERT_EQUAL_STRING("'bout", art->heading);
    TEST_ASSERT_TRUE(art->definition_length > 0);

    // Article 2: 'cause
    TEST_ASSERT_EQUAL(LSD_OK, dsl_article_iter_next(iter, &art));
    TEST_ASSERT_EQUAL_STRING("'cause", art->heading);

    // Article 3: 'course
    TEST_ASSERT_EQUAL(LSD_OK, dsl_article_iter_next(iter, &art));
    TEST_ASSERT_EQUAL_STRING("'course", art->heading);

    dsl_article_iter_destroy(iter);
    dsl_reader_close(r);
}

void test_dsl_ipa_iter_done(void) {
    dsl_reader *r = dsl_open_assert("en_us_ipa.dsl.dz");
    dsl_article_iter *iter = dsl_article_iter_create(r);
    const dsl_article *art = NULL;

    // Iterate to end — just count, don't print
    int count = 0;
    while (dsl_article_iter_next(iter, &art) == LSD_OK) {
        count++;
    }

    // Should return LSD_DONE after exhaustion
    lsd_status st = dsl_article_iter_next(iter, &art);
    TEST_ASSERT_EQUAL(LSD_DONE, st);
    TEST_ASSERT_NULL(art);

    dsl_article_iter_destroy(iter);
    dsl_reader_close(r);
}

void test_dsl_ipa_iter_definition_content(void) {
    dsl_reader *r = dsl_open_assert("en_us_ipa.dsl.dz");
    dsl_article_iter *iter = dsl_article_iter_create(r);
    const dsl_article *art = NULL;

    // First article definition should contain IPA markers
    TEST_ASSERT_EQUAL(LSD_OK, dsl_article_iter_next(iter, &art));
    TEST_ASSERT_NOT_NULL(strstr(art->definition, "[m1]"));
    TEST_ASSERT_NOT_NULL(strstr(art->definition, "[/m]"));

    dsl_article_iter_destroy(iter);
    dsl_reader_close(r);
}

void test_dsl_ipa_article_fields(void) {
    dsl_reader *r = dsl_open_assert("en_us_ipa.dsl.dz");
    dsl_article_iter *iter = dsl_article_iter_create(r);
    const dsl_article *art = NULL;

    TEST_ASSERT_EQUAL(LSD_OK, dsl_article_iter_next(iter, &art));
    // heading_length must match strlen of heading
    TEST_ASSERT_EQUAL_UINT(strlen(art->heading), art->heading_length);
    // definition_length must be > 0
    TEST_ASSERT_TRUE(art->definition_length > 0);
    // definition_offset must be set
    TEST_ASSERT_TRUE(art->definition_offset > 0);

    dsl_article_iter_destroy(iter);
    dsl_reader_close(r);
}

// ============================================================
// Test Runner
// ============================================================

void run_dsl_tests(void) {
    UnityBegin("test_dsl.c");

    // open / close null safety
    RUN_TEST(test_dsl_open_null_path);
    RUN_TEST(test_dsl_open_null_out);
    RUN_TEST(test_dsl_open_nonexistent);
    RUN_TEST(test_dsl_close_null);

    // property accessor null safety
    RUN_TEST(test_dsl_get_header_null);
    RUN_TEST(test_dsl_get_name_null);
    RUN_TEST(test_dsl_get_source_language_null);
    RUN_TEST(test_dsl_get_target_language_null);
    RUN_TEST(test_dsl_get_metadata_null);
    RUN_TEST(test_dsl_get_encoding_null);

    // iterator null safety
    RUN_TEST(test_dsl_iter_create_null);
    RUN_TEST(test_dsl_iter_destroy_null);
    RUN_TEST(test_dsl_iter_next_null);
    RUN_TEST(test_dsl_iter_next_null_out);

    // en_us_ipa.dsl.dz tests
    RUN_TEST(test_dsl_ipa_open);
    RUN_TEST(test_dsl_ipa_get_name);
    RUN_TEST(test_dsl_ipa_get_languages);
    RUN_TEST(test_dsl_ipa_encoding);
    RUN_TEST(test_dsl_ipa_encoding_name);
    RUN_TEST(test_dsl_ipa_iter_first3);
    RUN_TEST(test_dsl_ipa_iter_done);
    RUN_TEST(test_dsl_ipa_iter_definition_content);
    RUN_TEST(test_dsl_ipa_article_fields);

    UnityEnd();
}

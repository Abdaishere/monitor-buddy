#include <unity.h>
#include "odoo_fmt.h"

void setUp() {}
void tearDown() {}

static void expectHours(float hours, const char *want) {
  char buf[12];
  odooHours(hours, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING(want, buf);
}

static void expectStage(const char *stage, const char *want) {
  char buf[24];
  odooStage(stage, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING(want, buf);
}

static void expectAscii(const char *in, size_t maxChars, const char *want) {
  char buf[16];
  odooAscii(in, buf, sizeof(buf), maxChars);
  TEST_ASSERT_EQUAL_STRING(want, buf);
}

void test_hours_round_to_the_minute() {
  expectHours(2.0f, "2:00");
  expectHours(7.75f, "7:45");
  expectHours(0.999f, "1:00");
  expectHours(10.5f, "10:30");
  expectHours(-1.0f, "0:00");
}

void test_stage_drops_the_trailing_parenthesis() {
  expectStage("Development (Team)", "Development");
  expectStage("Code Review", "Code Review");
  expectStage("(Team)", "(Team)");
  expectStage("", "");
}

void test_ascii_replaces_each_non_ascii_character_once() {
  expectAscii("Caf\xC3\xA9 \xD9\x85\xD8\xB1", 14, "Caf? ??");
  expectAscii("a\tb", 14, "a b");
}

void test_ascii_cuts_long_text_with_dots() {
  expectAscii("Monthly sales", 14, "Monthly sales");
  expectAscii("Monthly sales dashboard", 14, "Monthly sale..");
  expectAscii("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 3, "?..");
}

void test_epoch_reads_odoo_utc_times() {
  TEST_ASSERT_EQUAL_INT64(0, odooEpoch("1970-01-01 00:00:00"));
  TEST_ASSERT_EQUAL_INT64(1794909600LL, odooEpoch("2026-11-17 10:00:00"));
  TEST_ASSERT_EQUAL_INT64(1709251199LL, odooEpoch("2024-02-29 23:59:59"));
  TEST_ASSERT_EQUAL_INT64(1798761600LL, odooEpoch("2027-01-01 00:00:00"));
}

void test_epoch_refuses_anything_else() {
  TEST_ASSERT_EQUAL_INT64(-1, odooEpoch(""));
  TEST_ASSERT_EQUAL_INT64(-1, odooEpoch(nullptr));
  TEST_ASSERT_EQUAL_INT64(-1, odooEpoch("2026-11-17"));
  TEST_ASSERT_EQUAL_INT64(-1, odooEpoch("2026-13-17 10:00:00"));
  TEST_ASSERT_EQUAL_INT64(-1, odooEpoch("2026-11-17 10:00:00x"));
}

void test_wrap_breaks_at_spaces() {
  char lines[3][11];
  TEST_ASSERT_EQUAL_UINT8(3, odooWrap("tests done on my module", &lines[0][0], 10, 3));
  TEST_ASSERT_EQUAL_STRING("tests done", lines[0]);
  TEST_ASSERT_EQUAL_STRING("on my", lines[1]);
  TEST_ASSERT_EQUAL_STRING("module", lines[2]);
}

void test_wrap_cuts_a_long_word_and_drops_what_does_not_fit() {
  char lines[2][6];
  TEST_ASSERT_EQUAL_UINT8(2, odooWrap("abcdefgh ij kl mn", &lines[0][0], 5, 2));
  TEST_ASSERT_EQUAL_STRING("abcde", lines[0]);
  TEST_ASSERT_EQUAL_STRING("fgh", lines[1]);
  TEST_ASSERT_EQUAL_UINT8(0, odooWrap("   ", &lines[0][0], 5, 2));
}

void test_ascii_never_overruns_the_buffer() {
  char buf[6];
  odooAscii("Monthly sales", buf, sizeof(buf), 40);
  TEST_ASSERT_EQUAL_STRING("Mon..", buf);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_hours_round_to_the_minute);
  RUN_TEST(test_stage_drops_the_trailing_parenthesis);
  RUN_TEST(test_ascii_replaces_each_non_ascii_character_once);
  RUN_TEST(test_ascii_cuts_long_text_with_dots);
  RUN_TEST(test_ascii_never_overruns_the_buffer);
  RUN_TEST(test_epoch_reads_odoo_utc_times);
  RUN_TEST(test_epoch_refuses_anything_else);
  RUN_TEST(test_wrap_breaks_at_spaces);
  RUN_TEST(test_wrap_cuts_a_long_word_and_drops_what_does_not_fit);
  return UNITY_END();
}

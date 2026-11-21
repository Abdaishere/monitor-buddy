#include <unity.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include "github_scan.h"

void setUp() {}
void tearDown() {}

static std::string fixture(const char *name) {
  std::ifstream f(std::string("test/fixtures/") + name, std::ios::binary);
  std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

// Feeds `text` in pieces of `chunk` bytes, as the network delivers it.
template <typename Scan>
static void feedChunks(Scan &s, const std::string &text, size_t chunk) {
  for (size_t i = 0; i < text.size(); i += chunk)
    s.feed(reinterpret_cast<const uint8_t *>(text.data()) + i, std::min(chunk, text.size() - i));
}

static const char REPOS[] =
  "[{\"name\":\"a\",\"stargazers_count\":3,\"watchers_count\":3},"
  "{\"name\":\"b\",\"stargazers_count\": 12,\"forks\":1},"
  "{\"name\":\"c\",\"stargazers_count\":0}]";

static void test_stars_sum_whatever_the_chunk_size() {
  for (size_t chunk : {1u, 2u, 7u, 4096u}) {
    ghscan::StarScan s;
    feedChunks(s, REPOS, chunk);
    TEST_ASSERT_EQUAL(15, s.stars);
    TEST_ASSERT_EQUAL(3, s.repos);
  }
}

static void test_stars_empty_list() {
  ghscan::StarScan s;
  feedChunks(s, "[]", 1);
  TEST_ASSERT_EQUAL(0, s.stars);
  TEST_ASSERT_EQUAL(0, s.repos);
}

// The fixture's days, oldest first: 8th 0, 9th 1, 10th 0, 11th 2, 12th 1024,
// 13th 7, 14th 3, 15th 4, 16th 0. The cells come in weekday rows, not in date
// order, as on github.com.
static void test_contributions_streak_survives_an_empty_today() {
  const std::string html = fixture("github-contributions.html");
  TEST_ASSERT_TRUE(html.size() > 0);
  for (size_t chunk : {1u, 3u, 64u, 100000u}) {
    ghscan::ContribScan s;
    feedChunks(s, html, chunk);
    TEST_ASSERT_EQUAL(9, s.days());
    TEST_ASSERT_EQUAL(0, s.today());
    TEST_ASSERT_EQUAL(5, s.streak());     // 11th to 15th
    const long newest = s.newestDay();    // the 16th
    TEST_ASSERT_EQUAL(0, s.at(newest));
    TEST_ASSERT_EQUAL(4, s.at(newest - 1));
    TEST_ASSERT_EQUAL(255, s.at(newest - 4));   // 1024, capped
    TEST_ASSERT_EQUAL(0, s.at(newest + 1));
    TEST_ASSERT_EQUAL(0, s.at(newest - 1000));
  }
}

static void test_contributions_today_counts() {
  ghscan::ContribScan s;
  feedChunks(s,
    "<td data-date=\"2026-03-01\"></td><tool-tip>2 contributions on March 1st.</tool-tip>"
    "<td data-date=\"2026-02-28\"></td><tool-tip>1 contribution on February 28th.</tool-tip>"
    "<td data-date=\"2026-02-27\"></td><tool-tip>No contributions on February 27th.</tool-tip>", 5);
  TEST_ASSERT_EQUAL(2, s.today());
  TEST_ASSERT_EQUAL(2, s.streak());       // across the month end
}

static void test_contributions_nothing_usable() {
  ghscan::ContribScan s;
  feedChunks(s, "<html><td data-date=\"not-a-date\"></td>rate limited</html>", 4);
  TEST_ASSERT_EQUAL(0, s.days());
  TEST_ASSERT_EQUAL(0, s.today());
  TEST_ASSERT_EQUAL(0, s.streak());
  TEST_ASSERT_EQUAL(0, s.at(0));
}

static void test_days_from_civil() {
  TEST_ASSERT_EQUAL(0, ghscan::daysFromCivil(1970, 1, 1));
  TEST_ASSERT_EQUAL(11016, ghscan::daysFromCivil(2000, 2, 29));
  TEST_ASSERT_EQUAL(1, ghscan::daysFromCivil(2026, 3, 1) - ghscan::daysFromCivil(2026, 2, 28));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_stars_sum_whatever_the_chunk_size);
  RUN_TEST(test_stars_empty_list);
  RUN_TEST(test_contributions_streak_survives_an_empty_today);
  RUN_TEST(test_contributions_today_counts);
  RUN_TEST(test_contributions_nothing_usable);
  RUN_TEST(test_days_from_civil);
  return UNITY_END();
}

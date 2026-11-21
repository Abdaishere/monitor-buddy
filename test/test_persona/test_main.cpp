#include <unity.h>
#include <string.h>
#include "persona.h"

void setUp() {}
void tearDown() {}

static PersonaPrefs prefs() {
  return PersonaPrefs{"Sam", "", "", "", 0, false, 9, 17, 12, WEEKEND_SAT_SUN};
}
// Wednesday 2026-11-18 unless said otherwise.
static PersonaNow at(uint8_t hour, uint8_t min, uint8_t sec = 0, uint8_t wday = 3) {
  return PersonaNow{2026, 11, 18, wday, hour, min, sec, true};
}
static const char *caption(PersonaStatus s, const PersonaPrefs &p, const PersonaNow &n) {
  static char text[PERSONA_CAPTION];
  personaCaption(s, p, n, text, sizeof(text));
  return text;
}
static const char *greeting(uint8_t style, const char *custom, const char *name, uint32_t pick) {
  static char text[PERSONA_CAPTION];
  personaGreeting(style, custom, name, pick, text, sizeof(text));
  return text;
}

static void test_work_days_follow_the_weekend() {
  TEST_ASSERT_TRUE(personaWorkDay(WEEKEND_SAT_SUN, 1));
  TEST_ASSERT_TRUE(personaWorkDay(WEEKEND_SAT_SUN, 5));
  TEST_ASSERT_FALSE(personaWorkDay(WEEKEND_SAT_SUN, 6));
  TEST_ASSERT_FALSE(personaWorkDay(WEEKEND_SAT_SUN, 0));
  TEST_ASSERT_TRUE(personaWorkDay(WEEKEND_FRI_SAT, 0));
  TEST_ASSERT_FALSE(personaWorkDay(WEEKEND_FRI_SAT, 5));
  TEST_ASSERT_FALSE(personaWorkDay(WEEKEND_FRI_SAT, 6));
  TEST_ASSERT_FALSE(personaWorkDay(WEEKEND_FRI, 5));
  TEST_ASSERT_TRUE(personaWorkDay(WEEKEND_FRI, 6));
  for (uint8_t d = 0; d < 7; d++) TEST_ASSERT_TRUE(personaWorkDay(WEEKEND_NONE, d));
  TEST_ASSERT_FALSE(personaWorkDay(99, 6));   // an unknown value behaves like Saturday and Sunday
}

static void test_birthday_is_one_day_a_year() {
  TEST_ASSERT_TRUE(personaIsBirthday("11-18", 2026, 11, 18));
  TEST_ASSERT_FALSE(personaIsBirthday("11-18", 2026, 11, 19));
  TEST_ASSERT_FALSE(personaIsBirthday("11-18", 2026, 10, 18));
  TEST_ASSERT_FALSE(personaIsBirthday("", 2026, 11, 18));
  TEST_ASSERT_FALSE(personaIsBirthday(nullptr, 2026, 11, 18));
}

static void test_birthday_refuses_what_is_not_a_date() {
  TEST_ASSERT_FALSE(personaIsBirthday("13-01", 2026, 1, 1));
  TEST_ASSERT_FALSE(personaIsBirthday("00-10", 2026, 1, 10));
  TEST_ASSERT_FALSE(personaIsBirthday("04-31", 2026, 4, 30));
  TEST_ASSERT_FALSE(personaIsBirthday("1-1", 2026, 1, 1));
  TEST_ASSERT_FALSE(personaIsBirthday("01/01", 2026, 1, 1));
  TEST_ASSERT_FALSE(personaIsBirthday("01-01x", 2026, 1, 1));
  TEST_ASSERT_FALSE(personaIsBirthday("a1-01", 2026, 1, 1));
}

static void test_a_leap_day_birthday_moves_to_the_28th() {
  TEST_ASSERT_TRUE(personaIsBirthday("02-29", 2028, 2, 29));
  TEST_ASSERT_FALSE(personaIsBirthday("02-29", 2028, 2, 28));
  TEST_ASSERT_TRUE(personaIsBirthday("02-29", 2027, 2, 28));
  TEST_ASSERT_FALSE(personaIsBirthday("02-29", 2027, 3, 1));
}

static void test_anniversary_counts_whole_years() {
  TEST_ASSERT_EQUAL(0, personaYearsSince("2026-11-18", 2026, 11, 18));   // the day it was set up
  TEST_ASSERT_EQUAL(1, personaYearsSince("2026-11-18", 2027, 11, 18));
  TEST_ASSERT_EQUAL(3, personaYearsSince("2026-11-18", 2029, 11, 18));
  TEST_ASSERT_EQUAL(0, personaYearsSince("2026-11-18", 2027, 11, 17));
  TEST_ASSERT_EQUAL(0, personaYearsSince("2026-11-18", 2025, 11, 18));   // a clock that runs behind
  TEST_ASSERT_EQUAL(1, personaYearsSince("2028-02-29", 2029, 2, 28));
  TEST_ASSERT_EQUAL(4, personaYearsSince("2028-02-29", 2032, 2, 29));
}

static void test_anniversary_refuses_what_is_not_a_date() {
  TEST_ASSERT_EQUAL(0, personaYearsSince("", 2027, 11, 18));
  TEST_ASSERT_EQUAL(0, personaYearsSince(nullptr, 2027, 11, 18));
  TEST_ASSERT_EQUAL(0, personaYearsSince("2026-13-18", 2027, 1, 18));
  TEST_ASSERT_EQUAL(0, personaYearsSince("2027-02-29", 2028, 2, 29));   // 2027 had no 29 February
  TEST_ASSERT_EQUAL(0, personaYearsSince("26-11-18", 2027, 11, 18));
  TEST_ASSERT_EQUAL(0, personaYearsSince("2026/11/18", 2027, 11, 18));
}

static void test_greetings_use_the_name_in_capitals() {
  TEST_ASSERT_EQUAL_STRING("HELLO SAM!", greeting(GREETING_HELLO, "", "Sam", 0));
  TEST_ASSERT_EQUAL_STRING("YO SAM!", greeting(GREETING_YO, "", "Sam", 0));
  TEST_ASSERT_EQUAL_STRING("SALAM SAM K.!", greeting(GREETING_SALAM, "", "Sam K.", 0));
  TEST_ASSERT_EQUAL_STRING("HELLO!", greeting(GREETING_HELLO, "", "", 0));
  TEST_ASSERT_EQUAL_STRING("SALAM!", greeting(GREETING_SALAM, "", nullptr, 0));
}

static void test_a_custom_greeting_is_shown_as_typed() {
  TEST_ASSERT_EQUAL_STRING("Ahoy, captain", greeting(GREETING_CUSTOM, "Ahoy, captain", "Sam", 0));
  TEST_ASSERT_EQUAL_STRING("HELLO!", greeting(GREETING_CUSTOM, "", "Sam", 1));   // empty: the mix
}

static void test_the_mix_keeps_the_classics_and_adds_the_name() {
  TEST_ASSERT_EQUAL_STRING("WHAT'S UP?", greeting(GREETING_MIX, "", "", 0));
  TEST_ASSERT_EQUAL_STRING("WHO'S THERE?", greeting(GREETING_MIX, "", "", 4));
  TEST_ASSERT_EQUAL_STRING("WHAT'S UP?", greeting(GREETING_MIX, "", "", 5));    // five lines without a name
  TEST_ASSERT_EQUAL_STRING("HEY SAM!", greeting(GREETING_MIX, "", "Sam", 5));   // six with one
  TEST_ASSERT_EQUAL_STRING("HELLO!", greeting(99, "", "", 1));                  // an unknown style is the mix
}

static void test_every_greeting_fits_the_caption() {
  const char *longest = "ABCDEFGHIJKL";                       // 12, the longest name
  const char *line = "abcdefghijklmnopqrstuvwxyz0123";        // longer than a caption
  for (uint8_t style = 0; style <= GREETING_CUSTOM; style++)
    for (uint32_t pick = 0; pick < 6; pick++)
      TEST_ASSERT_LESS_OR_EQUAL(26, strlen(greeting(style, line, longest, pick)));
  char tiny[8];
  personaGreeting(GREETING_SALAM, "", longest, 0, tiny, sizeof(tiny));
  TEST_ASSERT_EQUAL(7, strlen(tiny));                         // cut, never past the buffer
}

static void test_welcome_back() {
  char text[PERSONA_CAPTION];
  personaWelcome("Sam", text, sizeof(text));
  TEST_ASSERT_EQUAL_STRING("OH! HI SAM", text);
  personaWelcome("", text, sizeof(text));
  TEST_ASSERT_EQUAL_STRING("OH! HI", text);
}

static void test_meet_your_buddy() {
  char me[PERSONA_CAPTION], you[PERSONA_CAPTION];
  personaMeet("Pixel", "Sam", me, sizeof(me), you, sizeof(you));
  TEST_ASSERT_EQUAL_STRING("I'M PIXEL!", me);
  TEST_ASSERT_EQUAL_STRING("HI SAM!", you);
  personaMeet("", "", me, sizeof(me), you, sizeof(you));
  TEST_ASSERT_EQUAL_STRING("HELLO!", me);
  TEST_ASSERT_EQUAL_STRING("", you);
  personaMeet("ABCDEFGHIJKL", "ABCDEFGHIJKL", me, sizeof(me), you, sizeof(you));
  TEST_ASSERT_LESS_OR_EQUAL(26, strlen(me));
  TEST_ASSERT_LESS_OR_EQUAL(26, strlen(you));
}

static void test_stretch_comes_after_every_stretch_of_focus() {
  TEST_ASSERT_FALSE(personaStretchDue(0, 3000));        // off
  TEST_ASSERT_FALSE(personaStretchDue(25, 0));
  TEST_ASSERT_FALSE(personaStretchDue(25, 1499));
  TEST_ASSERT_TRUE(personaStretchDue(25, 1500));
  TEST_ASSERT_TRUE(personaStretchDue(25, 1559));
  TEST_ASSERT_FALSE(personaStretchDue(25, 1560));
  TEST_ASSERT_TRUE(personaStretchDue(25, 3000));        // and again after the next 25 minutes
  TEST_ASSERT_TRUE(personaStretchDue(120, 7200));
}

static void test_lunch_nudge_is_the_first_minute_of_lunch_on_a_work_day() {
  const PersonaPrefs p = prefs();
  TEST_ASSERT_EQUAL(PERSONA_LUNCH, personaNudge(p, at(12, 0, 0)));
  TEST_ASSERT_EQUAL(PERSONA_LUNCH, personaNudge(p, at(12, 0, 59)));
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaNudge(p, at(12, 1)));
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaNudge(p, at(11, 0)));
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaNudge(p, at(12, 0, 0, 6)));   // Saturday
  PersonaPrefs late = p;
  late.lunch = 18;                                                       // after work: never
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaNudge(late, at(18, 0)));
}

static void test_water_is_opt_in_and_hourly_in_work_hours() {
  PersonaPrefs p = prefs();
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaNudge(p, at(10, 0)));           // off by default
  p.water = true;
  TEST_ASSERT_EQUAL(PERSONA_WATER, personaNudge(p, at(10, 0, 0)));
  TEST_ASSERT_EQUAL(PERSONA_WATER, personaNudge(p, at(16, 0, 29)));
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaNudge(p, at(10, 0, 30)));
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaNudge(p, at(10, 30)));
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaNudge(p, at(9, 0)));            // not the moment work starts
  TEST_ASSERT_EQUAL(PERSONA_LUNCH, personaNudge(p, at(12, 0)));          // lunch wins its hour
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaNudge(p, at(17, 0)));           // work is over
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaNudge(p, at(8, 0)));
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaNudge(p, at(10, 0, 0, 0)));     // Sunday
}

static void test_other_weekends_move_the_work_days() {
  PersonaPrefs p = prefs();
  p.water = true;
  p.weekend = WEEKEND_FRI_SAT;
  TEST_ASSERT_EQUAL(PERSONA_WATER, personaNudge(p, at(10, 0, 0, 0)));    // Sunday is a work day
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaNudge(p, at(10, 0, 0, 5)));     // Friday is not
  TEST_ASSERT_EQUAL(PERSONA_WEEKEND, personaIdle(p, at(10, 0, 0, 5)));
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaIdle(p, at(10, 0, 0, 0)));
}

static void test_birthday_all_day_when_idle_and_in_bursts_otherwise() {
  PersonaPrefs p = prefs();
  p.birthday = "11-18";
  TEST_ASSERT_EQUAL(PERSONA_BIRTHDAY, personaIdle(p, at(15, 33)));
  TEST_ASSERT_EQUAL(PERSONA_BIRTHDAY, personaNudge(p, at(15, 30, 19)));
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaNudge(p, at(15, 30, 20)));
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaNudge(p, at(15, 33)));
  TEST_ASSERT_EQUAL(PERSONA_BIRTHDAY, personaIdle(p, at(15, 33, 0, 6)));   // also on a day off
  TEST_ASSERT_EQUAL_STRING("HAPPY BIRTHDAY!", caption(PERSONA_BIRTHDAY, p, at(15, 33)));
}

static void test_anniversary_caption_counts_the_years() {
  PersonaPrefs p = prefs();
  p.since = "2025-11-18";
  TEST_ASSERT_EQUAL(PERSONA_ANNIVERSARY, personaIdle(p, at(10, 5)));
  TEST_ASSERT_EQUAL_STRING("1 YEAR TOGETHER", caption(PERSONA_ANNIVERSARY, p, at(10, 5)));
  p.since = "2023-11-18";
  TEST_ASSERT_EQUAL_STRING("3 YEARS TOGETHER", caption(PERSONA_ANNIVERSARY, p, at(10, 5)));
  p.since = "2026-11-18";                                                   // the day itself: nothing to count yet
  TEST_ASSERT_EQUAL_STRING("", caption(PERSONA_ANNIVERSARY, p, at(10, 5)));
  p.since = "2023-11-18";
  p.birthday = "11-18";                                                     // both on one day: the birthday
  TEST_ASSERT_EQUAL(PERSONA_BIRTHDAY, personaIdle(p, at(10, 5)));
}

static void test_the_message_shows_now_and_then() {
  PersonaPrefs p = prefs();
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaIdle(p, at(10, 7)));               // no message, nothing to show
  p.message = "You've got this";
  TEST_ASSERT_EQUAL(PERSONA_MESSAGE, personaIdle(p, at(10, 7, 0)));
  TEST_ASSERT_EQUAL(PERSONA_MESSAGE, personaIdle(p, at(10, 52, 11)));
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaIdle(p, at(10, 7, 12)));
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaIdle(p, at(10, 8)));
  TEST_ASSERT_EQUAL_STRING("You've got this", caption(PERSONA_MESSAGE, p, at(10, 7)));
  TEST_ASSERT_EQUAL(PERSONA_MESSAGE, personaIdle(p, at(10, 7, 0, 6)));     // it interrupts the weekend nap
  TEST_ASSERT_EQUAL(PERSONA_WEEKEND, personaIdle(p, at(10, 8, 0, 6)));
}

static void test_nothing_personal_without_a_clock() {
  PersonaPrefs p = prefs();
  p.birthday = "11-18"; p.since = "2025-11-18"; p.message = "hi"; p.water = true;
  PersonaNow n = at(12, 0, 0, 6);
  n.trusted = false;
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaNudge(p, n));
  TEST_ASSERT_EQUAL(PERSONA_NONE, personaIdle(p, n));
}

static void test_defaults_change_nothing_on_a_work_day() {
  const PersonaPrefs p = PersonaPrefs{"", "", "", "", 0, false, 9, 17, 12, WEEKEND_SAT_SUN};
  for (uint8_t hour = 0; hour < 24; hour++)
    for (uint8_t min = 0; min < 60; min++) {
      TEST_ASSERT_EQUAL(PERSONA_NONE, personaIdle(p, at(hour, min)));
      if (!(hour == 12 && min == 0)) TEST_ASSERT_EQUAL(PERSONA_NONE, personaNudge(p, at(hour, min)));
    }
}

static void test_captions_fit() {
  PersonaPrefs p = prefs();
  p.since = "1990-11-18";
  p.message = "abcdefghijklmnopqrstuvwxyz0123";
  for (uint8_t s = PERSONA_NONE; s <= PERSONA_WEEKEND; s++)
    TEST_ASSERT_LESS_OR_EQUAL(26, strlen(caption((PersonaStatus)s, p, at(10, 0))));
  TEST_ASSERT_EQUAL_STRING("", caption(PERSONA_NONE, p, at(10, 0)));
  TEST_ASSERT_EQUAL_STRING("36 YEARS TOGETHER", caption(PERSONA_ANNIVERSARY, p, at(10, 0)));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_work_days_follow_the_weekend);
  RUN_TEST(test_birthday_is_one_day_a_year);
  RUN_TEST(test_birthday_refuses_what_is_not_a_date);
  RUN_TEST(test_a_leap_day_birthday_moves_to_the_28th);
  RUN_TEST(test_anniversary_counts_whole_years);
  RUN_TEST(test_anniversary_refuses_what_is_not_a_date);
  RUN_TEST(test_greetings_use_the_name_in_capitals);
  RUN_TEST(test_a_custom_greeting_is_shown_as_typed);
  RUN_TEST(test_the_mix_keeps_the_classics_and_adds_the_name);
  RUN_TEST(test_every_greeting_fits_the_caption);
  RUN_TEST(test_welcome_back);
  RUN_TEST(test_meet_your_buddy);
  RUN_TEST(test_stretch_comes_after_every_stretch_of_focus);
  RUN_TEST(test_lunch_nudge_is_the_first_minute_of_lunch_on_a_work_day);
  RUN_TEST(test_water_is_opt_in_and_hourly_in_work_hours);
  RUN_TEST(test_other_weekends_move_the_work_days);
  RUN_TEST(test_birthday_all_day_when_idle_and_in_bursts_otherwise);
  RUN_TEST(test_anniversary_caption_counts_the_years);
  RUN_TEST(test_the_message_shows_now_and_then);
  RUN_TEST(test_nothing_personal_without_a_clock);
  RUN_TEST(test_defaults_change_nothing_on_a_work_day);
  RUN_TEST(test_captions_fit);
  return UNITY_END();
}

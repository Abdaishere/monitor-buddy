#include <unity.h>
#include "button.h"

void setUp() {}
void tearDown() {}

void test_a_press_counts_once() {
  ButtonEdge button{300};
  TEST_ASSERT_FALSE(button.press(false, 1000));
  TEST_ASSERT_TRUE(button.press(true, 2000));
  TEST_ASSERT_FALSE(button.press(true, 2500));    // held
  TEST_ASSERT_FALSE(button.press(false, 3000));   // released
  TEST_ASSERT_TRUE(button.press(true, 4000));
}

void test_changes_within_the_debounce_are_ignored() {
  ButtonEdge button{300};
  button.press(false, 1000);
  TEST_ASSERT_TRUE(button.press(true, 2000));
  TEST_ASSERT_FALSE(button.press(false, 2010));   // contact bounce
  TEST_ASSERT_FALSE(button.press(true, 2020));
  TEST_ASSERT_FALSE(button.press(false, 2250));   // a quick release is taken once the debounce is over
  TEST_ASSERT_FALSE(button.press(false, 2400));
  TEST_ASSERT_TRUE(button.press(true, 2800));
}

void test_down_at_the_first_read_counts_after_a_release() {
  ButtonEdge button{300};
  TEST_ASSERT_FALSE(button.press(true, 1000));
  TEST_ASSERT_FALSE(button.press(true, 60000));
  TEST_ASSERT_FALSE(button.press(false, 61000));
  TEST_ASSERT_TRUE(button.press(true, 62000));
}

void test_the_millis_wrap() {
  ButtonEdge button{300};
  button.press(false, 0xFFFFFF00u);
  TEST_ASSERT_TRUE(button.press(true, 0x100u));   // 512 ms later
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_press_counts_once);
  RUN_TEST(test_changes_within_the_debounce_are_ignored);
  RUN_TEST(test_down_at_the_first_read_counts_after_a_release);
  RUN_TEST(test_the_millis_wrap);
  return UNITY_END();
}

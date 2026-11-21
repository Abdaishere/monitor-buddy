#include <unity.h>
#include <string>
#include "keys.h"

void setUp() {}
void tearDown() {}

static const char PEM[] =
  "-----BEGIN PUBLIC KEY-----\n"
  "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
  "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==\n"
  "-----END PUBLIC KEY-----\n";

static void test_link_key_is_64_hex_digits() {
  TEST_ASSERT_TRUE(keysLinkValid("0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789abcdef"));
  TEST_ASSERT_FALSE(keysLinkValid(nullptr));
  TEST_ASSERT_FALSE(keysLinkValid(""));
  TEST_ASSERT_FALSE(keysLinkValid("0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789abcde"));     // 63
  TEST_ASSERT_FALSE(keysLinkValid("0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789abcdef0"));   // 65
  TEST_ASSERT_FALSE(keysLinkValid("g123456789abcdef0123456789ABCDEF0123456789abcdef0123456789abcdef"));
  TEST_ASSERT_FALSE(keysLinkValid("0123456789abcdef0123456789ABCDEF 123456789abcdef0123456789abcdef"));
}

static void test_update_key_is_a_pem_public_key() {
  TEST_ASSERT_TRUE(keysUpdateValid(PEM));
  std::string noNewline(PEM, sizeof(PEM) - 2);
  TEST_ASSERT_TRUE(keysUpdateValid(noNewline.c_str()));
}

static void test_update_key_refuses_other_text() {
  TEST_ASSERT_FALSE(keysUpdateValid(nullptr));
  TEST_ASSERT_FALSE(keysUpdateValid(""));
  TEST_ASSERT_FALSE(keysUpdateValid("-----BEGIN PUBLIC KEY-----\n-----END PUBLIC KEY-----\n"));
  std::string priv(PEM);
  priv.replace(priv.find("PUBLIC"), 6, "PRIVATE");
  TEST_ASSERT_FALSE(keysUpdateValid(priv.c_str()));
  std::string odd(PEM);
  odd[40] = '"';                                   // nothing that could break out of a JSON or C string
  TEST_ASSERT_FALSE(keysUpdateValid(odd.c_str()));
  std::string cr(PEM);
  cr.insert(26, "\r");
  TEST_ASSERT_FALSE(keysUpdateValid(cr.c_str()));
  std::string tail = std::string(PEM) + "x";
  TEST_ASSERT_FALSE(keysUpdateValid(tail.c_str()));
}

static void test_update_key_has_a_size_limit() {
  std::string big = "-----BEGIN PUBLIC KEY-----\n" + std::string(KEYS_UPDATE_MAX, 'A') + "\n-----END PUBLIC KEY-----\n";
  TEST_ASSERT_FALSE(keysUpdateValid(big.c_str()));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_link_key_is_64_hex_digits);
  RUN_TEST(test_update_key_is_a_pem_public_key);
  RUN_TEST(test_update_key_refuses_other_text);
  RUN_TEST(test_update_key_has_a_size_limit);
  return UNITY_END();
}

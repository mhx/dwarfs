#include <gtest/gtest.h>

#include <string>

TEST(u8string, compare) {
  EXPECT_EQ(u8"我", u8"我");
}

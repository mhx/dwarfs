#include <cstdio>

#include "gtest/gtest.h"

#if 0 && defined(__linux__)

#include <csignal>
#include <cstdlib>
#include <cstring>

namespace {

void ignore_sigpipe() {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SIG_IGN;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGPIPE, &sa, nullptr);
}

} // namespace

#endif

GTEST_API_ int main(int argc, char** argv) {
  printf("Running main() from %s\n", __FILE__);
#if 0 && defined(__linux__)
  ignore_sigpipe();
#endif
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

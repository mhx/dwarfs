#include <iostream>

#include <dwarfs/config.h>

#ifdef DWARFS_STACKTRACE_ENABLED
#include <cpptrace/from_current.hpp>
#endif

#include <gtest/gtest.h>

GTEST_API_ int main(int argc, char** argv) {
  int result{1};
#ifdef DWARFS_STACKTRACE_ENABLED
  CPPTRACE_TRY {
#endif
    std::cout << "Running main() from " << __FILE__ << "\n";
    testing::InitGoogleTest(&argc, argv);
#ifdef DWARFS_STACKTRACE_ENABLED
    GTEST_FLAG_SET(catch_exceptions, false);
#endif
    result = RUN_ALL_TESTS();
#ifdef DWARFS_STACKTRACE_ENABLED
  }
  CPPTRACE_CATCH(std::exception const& e) {
    std::cout << "Exception: " << e.what() << std::endl;
    cpptrace::from_current_exception().print();
  }
#endif
  return result;
}

#include <iostream>
#include <parallel_hashmap/phmap_config.h>

int main() {
  std::cout << PHMAP_VERSION_MAJOR << '.' << PHMAP_VERSION_MINOR << '.' << PHMAP_VERSION_PATCH << '\n';
  return 0;
}

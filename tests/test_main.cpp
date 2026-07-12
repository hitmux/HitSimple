#include "support/TestRunner.h"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  std::vector<std::string> filters;
  for (int index = 1; index < argc; ++index) {
    filters.emplace_back(argv[index]);
  }
  return hitsimple::testing::Registry::instance().runAll(std::cout, filters);
}

#include <string>

extern "C" int cpp_add_suffix(int value) {
  return value + std::stoi("24");
}

#include <string>

extern "C" int hsc_increment(int value);

int main() {
  const std::string value = "41";
  return hsc_increment(std::stoi(value)) == 42 ? 0 : 1;
}

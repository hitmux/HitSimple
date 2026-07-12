#pragma once

#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace hitsimple::testing {

struct TestCase {
  std::string name;
  std::function<void()> run;
};

class Registry {
public:
  static Registry& instance();

  void add(std::string name, std::function<void()> run);
  int runAll(std::ostream& out, const std::vector<std::string>& filters = {}) const;

private:
  std::vector<TestCase> tests_;
};

class Failure {
public:
  explicit Failure(std::string message);

  const std::string& message() const;

private:
  std::string message_;
};

struct Registrar {
  Registrar(std::string name, std::function<void()> run);
};

void fail(std::string_view file, int line, std::string message);

template <typename Lhs, typename Rhs>
void expectEqual(const Lhs& lhs,
                 const Rhs& rhs,
                 std::string_view lhsText,
                 std::string_view rhsText,
                 std::string_view file,
                 int line) {
  if (!(lhs == rhs)) {
    fail(file, line, std::string(lhsText) + " != " + std::string(rhsText));
  }
}

void expectTrue(bool value,
                std::string_view exprText,
                std::string_view file,
                int line);

} // namespace hitsimple::testing

#define HS_TEST(name)                                                         \
  static void name();                                                         \
  static ::hitsimple::testing::Registrar name##_registrar(#name, name);       \
  static void name()

#define HS_EXPECT_EQ(lhs, rhs)                                                \
  ::hitsimple::testing::expectEqual(                                          \
      (lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__)

#define HS_EXPECT_TRUE(expr)                                                  \
  ::hitsimple::testing::expectTrue((expr), #expr, __FILE__, __LINE__)

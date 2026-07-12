#include "support/TestRunner.h"

#include <iostream>
#include <utility>

namespace hitsimple::testing {

Registry& Registry::instance() {
  static Registry registry;
  return registry;
}

void Registry::add(std::string name, std::function<void()> run) {
  tests_.push_back(TestCase{std::move(name), std::move(run)});
}

int Registry::runAll(std::ostream& out,
                     const std::vector<std::string>& filters) const {
  int failures = 0;
  int selected = 0;

  for (const auto& test : tests_) {
    bool matches = filters.empty();
    for (const auto& filter : filters) {
      if (test.name.find(filter) != std::string::npos) {
        matches = true;
        break;
      }
    }
    if (!matches) {
      continue;
    }
    ++selected;
    out << "[RUN] " << test.name << '\n' << std::flush;
    try {
      test.run();
      out << "[PASS] " << test.name << '\n' << std::flush;
    } catch (const Failure& failure) {
      ++failures;
      out << "[FAIL] " << test.name << ": " << failure.message() << '\n'
          << std::flush;
    }
  }

  out << selected << " tests, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}

Failure::Failure(std::string message) : message_(std::move(message)) {}

const std::string& Failure::message() const {
  return message_;
}

Registrar::Registrar(std::string name, std::function<void()> run) {
  Registry::instance().add(std::move(name), std::move(run));
}

void fail(std::string_view file, int line, std::string message) {
  throw Failure(std::string(file) + ":" + std::to_string(line) + ": " +
                std::move(message));
}

void expectTrue(bool value,
                std::string_view exprText,
                std::string_view file,
                int line) {
  if (!value) {
    fail(file, line, std::string(exprText) + " is false");
  }
}

} // namespace hitsimple::testing

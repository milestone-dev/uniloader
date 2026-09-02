#include "harness.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ult {
namespace {

struct Test {
  std::string group;
  std::string name;
  TestFunction function;
};

/// A function-local static rather than a namespace-scope vector: registrations
/// run during static initialisation, and a plain global might not be
/// constructed yet when the first one arrives.
std::vector<Test>& Tests() {
  static std::vector<Test> tests;
  return tests;
}

std::string& RootSlot() {
  static std::string root(".");
  return root;
}

struct Failure : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct Skipped : std::runtime_error {
  using std::runtime_error::runtime_error;
};

}  // namespace

Registration::Registration(const char* group, const char* name, TestFunction function) {
  Tests().push_back(Test{group, name, function});
}

const std::string& Root() { return RootSlot(); }

std::string ReadFixture(const std::string& relative_path) {
  std::ifstream file(RootSlot() + "/" + relative_path, std::ios::binary);
  if (!file) return {};
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

void Skip(const std::string& why) { throw Skipped(why); }

void Check(bool condition, const char* expression, const char* file, int line,
           const std::string& detail) {
  if (condition) return;
  std::ostringstream message;
  message << file << ":" << line << ": " << expression;
  if (!detail.empty()) message << "\n      " << detail;
  throw Failure(message.str());
}

std::string Describe(const std::string& value) { return "\"" + value + "\""; }
std::string Describe(const char* value) {
  return value ? "\"" + std::string(value) + "\"" : "null";
}
std::string Describe(int value) { return std::to_string(value); }
std::string Describe(long long value) { return std::to_string(value); }
std::string Describe(bool value) { return value ? "true" : "false"; }

int Run(int argc, char** argv) {
  std::string group;
  std::string filter;
  bool list = false;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--group" && i + 1 < argc) {
      group = argv[++i];
    } else if (argument == "--filter" && i + 1 < argc) {
      filter = argv[++i];
    } else if (argument == "--list") {
      list = true;
    } else if (!argument.empty() && argument[0] != '-') {
      RootSlot() = argument;
    }
  }

  int passed = 0, failed = 0, skipped = 0;
  for (const Test& test : Tests()) {
    if (!group.empty() && test.group != group) continue;
    if (!filter.empty() && test.name.find(filter) == std::string::npos) continue;
    if (list) {
      std::printf("%s.%s\n", test.group.c_str(), test.name.c_str());
      continue;
    }
    try {
      test.function();
      ++passed;
    } catch (const Skipped& why) {
      ++skipped;
      std::printf("SKIP %s.%s — %s\n", test.group.c_str(), test.name.c_str(), why.what());
    } catch (const Failure& failure) {
      ++failed;
      std::printf("FAIL %s.%s\n  %s\n", test.group.c_str(), test.name.c_str(),
                  failure.what());
    } catch (const std::exception& error) {
      ++failed;
      std::printf("FAIL %s.%s\n  threw: %s\n", test.group.c_str(), test.name.c_str(),
                  error.what());
    }
  }
  if (list) return 0;
  std::printf("%d passed, %d failed, %d skipped\n", passed, failed, skipped);
  return failed == 0 ? 0 : 1;
}

}  // namespace ult

int main(int argc, char** argv) { return ult::Run(argc, argv); }

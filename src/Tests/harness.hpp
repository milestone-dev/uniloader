// A test harness with no dependencies, because the core has none either.
//
//   ul_tests <repo-root> [--group <name>] [--filter <substring>] [--list]
//
// Tests register themselves at static-initialisation time through TEST(), so
// adding one is adding a function. A test that needs something the machine may
// not have — a real package, a network — calls Skip() and says why, which is
// counted and reported separately from a pass. A suite that quietly passes
// because it tested nothing is worse than one that fails.

#pragma once

#include <string>
#include <vector>

namespace ult {

using TestFunction = void (*)();

struct Registration {
  Registration(const char* group, const char* name, TestFunction function);
};

/// The repository root, as given on the command line. Fixtures hang off it.
const std::string& Root();

/// Reads a file below the repository root. Empty when it is not there, which is
/// how a test decides to skip rather than fail.
std::string ReadFixture(const std::string& relative_path);

/// Abandons the running test as "not applicable here", with a reason that is
/// printed. Not a failure and not a pass.
void Skip(const std::string& why);

void Check(bool condition, const char* expression, const char* file, int line,
           const std::string& detail);

int Run(int argc, char** argv);

}  // namespace ult

#define TEST(group, name)                                                    \
  static void ul_test_##group##_##name();                                    \
  static const ::ult::Registration ul_reg_##group##_##name(                  \
      #group, #name, &ul_test_##group##_##name);                             \
  static void ul_test_##group##_##name()

#define CHECK(condition) \
  ::ult::Check((condition), #condition, __FILE__, __LINE__, {})

/// The same, with the values printed on failure — which is the difference
/// between "the comparison was false" and knowing what it compared.
#define CHECK_EQ(actual, expected)                                          \
  do {                                                                      \
    const auto& ul_actual = (actual);                                       \
    const auto& ul_expected = (expected);                                   \
    ::ult::Check(ul_actual == ul_expected, #actual " == " #expected,        \
                 __FILE__, __LINE__,                                        \
                 ::ult::Describe(ul_actual) + " vs " +                      \
                     ::ult::Describe(ul_expected));                         \
  } while (0)

namespace ult {
std::string Describe(const std::string& value);
std::string Describe(const char* value);
std::string Describe(int value);
std::string Describe(long long value);
std::string Describe(bool value);
}  // namespace ult

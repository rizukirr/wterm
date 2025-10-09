# Contributing to wterm

Thank you for your interest in contributing to wterm! This document provides guidelines and instructions for contributing to the project.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Setup](#development-setup)
- [Making Changes](#making-changes)
- [Testing Requirements](#testing-requirements)
- [Security Guidelines](#security-guidelines)
- [Submitting Changes](#submitting-changes)
- [Code Style Guidelines](#code-style-guidelines)
- [Review Process](#review-process)

## Code of Conduct

By participating in this project, you agree to:
- Be respectful and inclusive
- Focus on what is best for the community
- Show empathy towards other contributors
- Accept constructive criticism gracefully

## Getting Started

### Prerequisites

Before contributing, ensure you have:
- A GitHub account
- Git installed locally
- CMake (>= 3.12)
- GCC or Clang compiler
- NetworkManager (for testing)
- Basic knowledge of C programming

### Fork and Clone

1. Fork the repository on GitHub
2. Clone your fork locally:
   ```bash
   git clone https://github.com/YOUR_USERNAME/wterm.git
   cd wterm
   ```
3. Add the upstream repository:
   ```bash
   git remote add upstream https://github.com/ORIGINAL_OWNER/wterm.git
   ```

## Development Setup

### Building the Project

```bash
# Quick build
./scripts/build.sh release

# Debug build with sanitizers
./scripts/build.sh debug --sanitize

# Build and run tests
./scripts/build.sh all
```

### Manual Build

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON ..
cmake --build . --parallel $(nproc)
```

## Making Changes

### Before You Start

1. Check existing issues to avoid duplicate work
2. Create an issue to discuss major changes
3. Keep changes focused and atomic
4. Write clear commit messages

### Branch Naming

Use descriptive branch names:
- `feature/add-5ghz-support`
- `fix/ssid-parsing-bug`
- `refactor/improve-string-utils`
- `security/fix-command-injection`
- `docs/update-installation-guide`

### Commit Messages

Follow conventional commit format (recommended):

```
type(scope): brief description

Detailed explanation if necessary

Fixes #123
```

**Types:**
- `feat`: New feature
- `fix`: Bug fix
- `security`: Security fix
- `refactor`: Code refactoring
- `perf`: Performance improvement
- `test`: Test updates
- `docs`: Documentation
- `style`: Code style/formatting
- `ci`: CI/CD changes
- `chore`: Maintenance tasks

**Examples:**
```
feat(scanner): add 5GHz band detection
fix(security): properly escape SSID in commands
docs(readme): update installation instructions
```

## Testing Requirements

### Test Locally Before Submitting

**All tests must pass before submitting a PR:**

```bash
# RECOMMENDED: Run comprehensive CI checks locally
cmake --build build --target ci-check

# Or run quick pre-commit check
cmake --build build --target ci-quick

# Run all tests (using build script)
./scripts/build.sh test

# Run specific test suites
./build/tests/test_string_utils
./build/tests/test_network_scanner
./build/tests/test_integration
./build/tests/test_security
```

### Available CMake CI Targets

The project includes CMake targets that mirror GitHub Actions CI checks:

```bash
# Comprehensive CI check (recommended before submitting PR)
cmake --build build --target ci-check
# Runs: build, all tests, security tests, static analysis,
#       format check, security audit, memory leak detection

# Quick pre-commit check (fast)
cmake --build build --target ci-quick
# Runs: build + all tests

# Security-specific checks
cmake --build build --target security-check      # 80 security tests
cmake --build build --target security-audit      # Audit for unsafe patterns
cmake --build build --target memory-check        # Valgrind leak detection

# Code quality checks
cmake --build build --target static-analysis     # cppcheck
cmake --build build --target format-check        # Formatting validation
```

### Security Test Requirements

The project has **80 security tests** that must pass:

```bash
# Run security tests
./build/tests/test_security

# Expected output: "80 tests passed"
```

### Testing with Sanitizers

**Critical:** Always test with sanitizers before submitting:

```bash
# Build with sanitizers
./scripts/build.sh debug --sanitize

# Run tests with sanitizers
cd build
ctest --output-on-failure
```

This detects:
- Memory leaks
- Buffer overflows
- Use-after-free bugs
- Undefined behavior

### Memory Leak Testing

```bash
# Test with Valgrind (if available)
valgrind --leak-check=full --show-leak-kinds=all \
  ./build/tests/test_security
```

## Security Guidelines

### Critical: Security-First Development

wterm handles network credentials and system commands. **Security is paramount.**

### Input Validation Rules

**All user inputs MUST be validated and sanitized:**

```c
#include "wterm/input_sanitizer.h"

// 1. Validate input
if (!validate_ssid(ssid)) {
    return WTERM_ERROR_INVALID_INPUT;
}

// 2. Escape for shell
char escaped_ssid[MAX_ESCAPED_LENGTH];
if (!shell_escape(ssid, escaped_ssid, sizeof(escaped_ssid))) {
    return WTERM_ERROR_BUFFER_OVERFLOW;
}

// 3. Use escaped value in commands
snprintf(command, sizeof(command), "nmcli device wifi connect %s", escaped_ssid);
```

### Security-Critical Files

**Extra scrutiny required when modifying:**
- `src/utils/input_sanitizer.c/h` - Input validation and sanitization
- `src/core/network_backends/nmcli_backend.c` - Command execution
- `src/core/connection.c` - Network connection handling
- `src/tui/tui_interface.c` - User input handling and TUI interactions
- `src/core/hotspot_ui.c` - Password input and hotspot configuration

### Command Injection Prevention

**Never do this:**
```c
// ❌ VULNERABLE - Direct user input in command
snprintf(cmd, sizeof(cmd), "nmcli device wifi connect \"%s\"", user_ssid);
```

**Always do this:**
```c
// ✅ SECURE - Validated and escaped
if (!validate_ssid(user_ssid)) return ERROR;
shell_escape(user_ssid, escaped, sizeof(escaped));
snprintf(cmd, sizeof(cmd), "nmcli device wifi connect %s", escaped);
```

### Temporary File Security

**Always set secure permissions:**
```c
int fd = mkstemp(temp_file);
if (fd < 0) return ERROR;

// Set owner-only permissions immediately
if (fchmod(fd, 0600) != 0) {
    close(fd);
    return ERROR;
}
```

### Adding New Security Tests

If you add new security features, add corresponding tests to `tests/test_security.c`:

```c
void test_new_validation_function(void) {
    TEST_ASSERT(validate_new_input("valid"), "Valid input should pass");
    TEST_ASSERT(!validate_new_input("'; rm -rf"), "Injection should fail");
    TEST_ASSERT(!validate_new_input("$(whoami)"), "Command sub should fail");
}
```

### Security Checklist

Before submitting security-related changes:

- [ ] All user inputs validated
- [ ] All inputs escaped before shell commands
- [ ] No `system()` calls (use `popen()` with sanitization)
- [ ] Temporary files use 0600 permissions
- [ ] No hardcoded credentials
- [ ] No format string vulnerabilities
- [ ] Memory safety verified (no overflows)
- [ ] Security tests updated
- [ ] SECURITY_FIXES.md updated (if fixing vulnerability)

## Submitting Changes

### Pre-Submission Checklist

Before creating a pull request:

- [ ] Run comprehensive CI checks: `cmake --build build --target ci-check`
- [ ] All CI checks pass locally
- [ ] Code compiles without warnings
- [ ] All tests pass locally
- [ ] Security tests pass (80/80)
- [ ] Sanitizers run clean (no leaks/errors)
- [ ] Code follows project style
- [ ] Documentation updated
- [ ] Commit messages are clear
- [ ] Branch is up to date with main

**Quick checklist command:**
```bash
# This runs everything CI will check
cmake --build build --target ci-check
```

### Creating a Pull Request

1. Push your changes to your fork:
   ```bash
   git push origin your-branch-name
   ```

2. Create a PR on GitHub

3. Fill out the PR template completely

4. Ensure all CI checks pass

5. Respond to review feedback

### What Happens Next

1. **Automated Checks:** CI runs automatically
   - Build matrix (Ubuntu 22.04/24.04, GCC/Clang)
   - All test suites
   - Security tests with sanitizers
   - Static analysis (cppcheck)
   - Code quality checks

2. **Review Process:**
   - Maintainer reviews code
   - Security implications assessed
   - Tests verified
   - Feedback provided

3. **Approval:**
   - All checks must pass
   - At least one maintainer approval required
   - Security-critical changes may require additional review

4. **Merge:**
   - PR merged to main
   - Thank you for contributing!

## Code Style Guidelines

### General Principles

- **Clarity over cleverness**
- **Safety over performance** (unless proven necessary)
- **Explicit over implicit**
- **Consistent with existing code**

### C Code Style

#### Naming Conventions

```c
// Functions: snake_case
wterm_result_t scan_networks(void);

// Types: snake_case_t suffix
typedef struct network_info_t network_info_t;

// Constants: UPPER_SNAKE_CASE
#define MAX_SSID_LENGTH 32

// Variables: snake_case
int network_count = 0;
```

#### Indentation

- Use **4 spaces** (no tabs)
- Indent braces consistently

```c
// Function braces on new line
void function_name(void)
{
    if (condition) {
        // Code
    }
}

// Struct braces inline
typedef struct {
    int member;
} type_t;
```

#### Comments

```c
/**
 * Brief function description
 *
 * Detailed explanation if necessary
 *
 * @param ssid The network SSID to connect to
 * @return WTERM_SUCCESS on success, error code otherwise
 */
wterm_result_t connect_to_network(const char *ssid);
```

#### Error Handling

```c
// Always check return values
wterm_result_t result = perform_operation();
if (result != WTERM_SUCCESS) {
    // Handle error
    return result;
}

// Use early returns for error cases
if (input == NULL) {
    return WTERM_ERROR_NULL_POINTER;
}
```

#### Memory Safety

```c
// Use stack allocation when possible
char buffer[MAX_BUFFER_SIZE];

// Always use safe string functions
safe_string_copy(dest, src, sizeof(dest));

// Never use unsafe functions
// strcpy()  -> use safe_string_copy()
// strcat()  -> use strncat() with size
// sprintf() -> use snprintf() with size
```

### File Organization

```
src/
├── core/              # Core functionality
│   ├── network_scanner.c
│   └── connection.c
├── utils/             # Utility functions
│   ├── string_utils.c
│   └── input_sanitizer.c
└── main.c             # Entry point

include/wterm/         # Public headers
├── common.h
├── network_scanner.h
└── input_sanitizer.h

tests/                 # Test suite
├── test_string_utils.c
├── test_network_scanner.c
├── test_integration.c
└── test_security.c
```

## Review Process

### What Reviewers Look For

1. **Correctness:** Does the code do what it's supposed to?
2. **Security:** Are there any security implications?
3. **Tests:** Are tests adequate and passing?
4. **Style:** Does code follow project conventions?
5. **Documentation:** Is functionality documented?
6. **Performance:** Any obvious performance issues?

### Addressing Review Feedback

- Respond to all comments
- Make requested changes or explain why not
- Push updates to the same branch
- Request re-review when ready

### Review Timeline

- Initial review: Usually within 3-5 days
- Security-critical changes: May take longer
- Simple fixes: May be merged quickly

## Additional Resources

### Documentation

- **README.md:** User documentation and features
- **CLAUDE.md:** Development context and architecture
- **SECURITY_FIXES.md:** Security vulnerability documentation

### Getting Help

- **Issues:** Open an issue for bugs or questions
- **Discussions:** For general questions and ideas
- **Security:** Report security issues privately to maintainers

### Development Tips

1. **Read existing code** to understand patterns
2. **Start small** with bug fixes or documentation
3. **Ask questions** if something is unclear
4. **Be patient** with the review process
5. **Learn from feedback** to improve future contributions

## Thank You!

Your contributions make wterm better for everyone. Whether it's:
- Fixing a bug
- Adding a feature
- Improving documentation
- Reporting issues
- Reviewing code

**Every contribution matters!** 🚀

---

**Questions?** Open an issue or reach out to the maintainers.

**Ready to contribute?** Fork the repo and start coding!

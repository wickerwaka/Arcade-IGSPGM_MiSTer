# C++ Naming Conventions

This document describes the C++ naming conventions used in the sim/ directory and how to enforce them using clang-tidy.

## Naming Rules

### Types (Classes, Structs, Enums, Typedefs, Type Aliases)
- **Convention**: UpperCamelCase
- **Examples**: `SimCore`, `SimVideo`, `MemoryRegion`, `GameT`

### Functions and Methods
- **Convention**: UpperCamelCase
- **Examples**: `Init()`, `Tick()`, `SendIOCTLData()`, `GetGameName()`

### Member Variables
- **Convention**: mLowerCamelCase (prefix with 'm')
- **Examples**: `mTop`, `mVideo`, `mTotalTicks`, `mSimulationRun`
- **Rationale**: The 'm' prefix clearly distinguishes member variables from local variables and parameters

### Global Variables
- **Convention**: gLowerCamelCase (prefix with 'g')
- **Examples**: `gSimCore`
- **Rationale**: The 'g' prefix makes global scope immediately visible

### Static Variables
- **Convention**: sLowerCamelCase (prefix with 's')
- **Examples**: `sDummyValue`, `sInitialized`
- **Rationale**: The 's' prefix indicates static storage duration

### Local Variables and Parameters
- **Convention**: lowerCamelCase
- **Examples**: `timeout`, `dataSize`, `byteCount`

### Constants
- **Convention**:
  - Class/local constants: kCamelCase (prefix with 'k')
  - Global constants: UPPER_CASE
- **Examples**: `kMaxRetries`, `CPU_ROM_SDR_BASE`, `SCN0_ROM_SDR_BASE`

### Macros
- **Convention**: UPPER_CASE
- **Examples**: `PGM_SIGNAL`, `UNIQUE_MEMORY_16B`

### Namespaces
- **Convention**: lower_case
- **Examples**: `sim_utils`, `memory_management`

### Enum Constants
- **Convention**: UPPER_CASE
- **Examples**: `GAME_INVALID`, `MEMORY_READ`, `TRACE_ACTIVE`

## Using clang-tidy to Enforce Conventions

A `.clang-tidy` configuration file is provided in this directory that automatically checks for naming convention violations.

### Prerequisites

1. Install LLVM/clang-tidy (available via Homebrew on macOS):
   ```bash
   brew install llvm
   ```

2. Ensure you have a compilation database (`compile_commands.json`) in the sim/ directory. This is generated automatically when building with the Makefile.

### Running clang-tidy

To check a single file:
```bash
/opt/homebrew/Cellar/llvm/*/bin/clang-tidy -p . filename.cpp
```

To check all C++ files:
```bash
find . -name "*.cpp" -not -path "*/verilated/*" -exec /opt/homebrew/Cellar/llvm/*/bin/clang-tidy -p . {} \;
```

To automatically fix violations (use with caution):
```bash
/opt/homebrew/Cellar/llvm/*/bin/clang-tidy -p . --fix filename.cpp
```

### Known Limitations

1. **System Headers**: clang-tidy may report errors about system headers not being found. This is usually harmless and can be ignored.

2. **False Positives**: The tool may occasionally misidentify code constructs. Common false positives include:
   - Member functions being flagged as global variables
   - Template parameters in complex contexts

3. **Verilator Code**: Generated Verilator code is excluded from checks via the `HeaderFilterRegex` in `.clang-tidy`.

4. **Existing Code**: The configuration enforces conventions for new code. Legacy code should be refactored incrementally.

## Refactoring Guide

When refactoring existing code to follow these conventions:

1. **Start with headers**: Rename in header files first to catch all usages
2. **Update implementation**: Modify .cpp files to match header changes
3. **Use find/replace carefully**: sed and Edit tools can help with bulk renames
4. **Test after each change**: Run `make` to ensure code still compiles
5. **Commit incrementally**: Make separate commits for each class/module refactored

## History

- **2025-10-30**: Created naming conventions document
- **2025-10-30**: Refactored FileSearch class (methods and members)
- **2025-10-30**: Refactored SimCore class (all 20 member variables)
- **2025-10-30**: Added clang-tidy configuration for automated checking

## References

- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
- [clang-tidy readability-identifier-naming check](https://clang.llvm.org/extra/clang-tidy/checks/readability/identifier-naming.html)
- [Hungarian Notation variants](https://en.wikipedia.org/wiki/Hungarian_notation)

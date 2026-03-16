#!/bin/bash
# Helper script to check C++ naming conventions using clang-tidy

# Find clang-tidy
CLANG_TIDY=$(find /opt/homebrew/Cellar/llvm/*/bin/clang-tidy 2>/dev/null | head -1)

if [ -z "$CLANG_TIDY" ]; then
    echo "Error: clang-tidy not found. Install with: brew install llvm"
    exit 1
fi

echo "Using clang-tidy: $CLANG_TIDY"

# Change to script directory
cd "$(dirname "$0")"

# Check if compile_commands.json exists
if [ ! -f "compile_commands.json" ]; then
    echo "Warning: compile_commands.json not found. Building to generate it..."
    make -j8
fi

# Default to checking all .cpp files if no arguments
if [ $# -eq 0 ]; then
    echo "Checking all C++ source files..."
    find . -name "*.cpp" -not -path "*/verilated/*" -not -path "*/obj/*" -print0 | while IFS= read -r -d '' file; do
        echo ""
        echo "=== Checking $file ==="
        "$CLANG_TIDY" -p . "$file" 2>&1 | grep -A 5 "warning:"
    done
else
    # Check specified files
    for file in "$@"; do
        echo ""
        echo "=== Checking $file ==="
        "$CLANG_TIDY" -p . "$file"
    done
fi

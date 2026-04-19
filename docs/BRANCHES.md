# Feature Branches for Build Fixes

This document describes the feature branches created to fix build issues in the Havel window manager.

## Current State

- **main**: Up to date with origin/main, contains latest features
- **Binary**: `build-debug/havel` builds successfully from commit 7a9724f

## Fix Branches

The following branches contain isolated fixes that can be cherry-picked or merged as needed:

### 1. fix/vm-error-formatting (0f89bd7)
**Purpose**: Add formatErrorWithContext() definition for VMExecutionContext

**Changes**:
- `src/havel-lang/compiler/vm/VM.cpp`: Added formatErrorWithContext() method definition
- `src/havel-lang/compiler/vm/VM.hpp`: Declaration already exists

**Status**: Standalone fix, can be cherry-picked

### 2. fix/lexer-signatures (d6bd218)
**Purpose**: Fix scanString and scanMultilineString function signatures

**Changes**:
- `src/havel-lang/lexer/Lexer.cpp`: Updated function signatures to accept isFString parameter
- `src/havel-lang/lexer/Lexer.hpp`: Updated declarations

**Status**: Depends on VM fix for full build

### 3. fix/parser-expressions (e34d23d)
**Purpose**: Fix expression parsing scope issues

**Changes**:
- `src/havel-lang/parser/Parser.cpp`: Added using directives for TokenType
- `src/havel-lang/parser/Parser.h`: Fixed parseExpressionFromString scope

**Status**: Depends on lexer fix

### 4. fix/object-literals (f1f1a28)
**Purpose**: Fix parsing of standalone object literals

**Changes**:
- `src/havel-lang/parser/Parser.cpp`: Added isObjectLiteral() function
- `src/havel-lang/parser/Parser.h`: Added declaration

**Status**: Depends on parser expressions fix

## Usage

To apply fixes individually:

```bash
# Cherry-pick a specific fix
git cherry-pick <commit-hash>

# Or merge a branch
git merge fix/<branch-name>
```

## Recommended Merge Order

1. fix/vm-error-formatting
2. fix/lexer-signatures
3. fix/parser-expressions
4. fix/object-literals

## Testing

After applying fixes, rebuild:

```bash
cd build-debug
cmake --build . --target havel -j4
./havel --run test.hv
```

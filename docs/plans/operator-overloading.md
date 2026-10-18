---
plan name: operator-overloading
plan description: Add special methods
plan status: done
---

## Idea
Add support for special method syntax in Havel: constructor @(), destructor @-(), index [](i)/[]=(i,v), operators (+, -, *, /, **, ==, !=, <, >, <=, >=, |>, |, &, ^, ~, >>, <<, =), length #(), toString ""(), repr(), code(), and callable ()(args...).

## Implementation
- Add destructor @-() syntax support in parser (parseStructMembers and parseClassMembers) - map to op_destructor
- Add length #() syntax support - map to op_length
- Add toString ""() syntax support - map to op_toString
- Add repr() method support - map to op_repr
- Add code() method support - map to op_code
- Add missing operator tokens to parser: ** (Power), |> (PipeRight), | (BitwiseOr), & (BitwiseAnd), ^ (BitwiseXor), ~ (Tilde/unary), >> (ShiftRight), << (ShiftLeft), = (copy/Assign)
- Update VM.cpp with new operator dispatch for all new op_* methods
- Build and test with sample class using new syntax
- Run existing tests to verify no regressions

## Required Specs
<!-- SPECS_START -->
- remove-call-host
- operator-syntax
<!-- SPECS_END -->
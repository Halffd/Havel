# Evaluator Extraction Status

## Current State

### Completed ✅
- **ExprEvaluator framework** - Created with key methods:
  - `visitBinaryExpression()` - Full implementation (104 lines)
  - `visitUnaryExpression()` - Full implementation
  - All other expression visitors delegate to Interpreter (placeholder pattern)

- **StatementEvaluator framework** - Created with key methods:
  - `visitProgram()` - Full implementation
  - All other statement visitors delegate to Interpreter (placeholder pattern)

- **Helper methods** - Properly delegated:
  - `Evaluate()`
  - `isError()`
  - `unwrap()`
  - `ValueToString()`
  - `ValueToNumber()`
  - `ValueToBool()`

### Remaining Work

**Expression visitors to extract (25 methods):**
```
visitUpdateExpression      (line 1300)
visitCallExpression        (line 1366)
visitMemberExpression      (line 1460)
visitLambdaExpression      (line 1602)
visitSetExpression         (line 1665)
visitPipelineExpression    (line 1685)
visitInterpolatedStringExpression (line 1942)
visitHotkeyLiteral         (line 1967)
visitAsyncExpression       (line 1971)
visitAwaitExpression       (line 1981)
visitArrayLiteral          (line 1999)
visitSpreadExpression      (line 2036)
visitObjectLiteral         (line 2047)
visitIndexExpression       (line 2285)
visitTernaryExpression     (line 2339)
visitRangeExpression       (line 2541)
visitAssignmentExpression  (line 2587)
visitTryExpression         (line 2746)
visitBlockExpression       (line 693)
visitIfExpression          (line 722)
visitBacktickExpression    (line 905)
visitShellCommandExpression (line 920)
```

**Statement visitors to extract (48 methods):**
```
visitLetDeclaration        (line 551)
visitFunctionDeclaration   (line 625)
visitReturnStatement       (line 644)
visitIfStatement           (line 657)
visitBlockStatement        (line 673)
visitHotkeyBinding         (line 740)
visitSleepStatement        (line 835)
visitRepeatStatement       (line 887)
visitShellCommandStatement (line 955)
visitInputStatement        (line 1036)
... (38 more)
```

## Extraction Process

For each visitor method:

1. **Copy** method from `Interpreter.cpp` to evaluator file
2. **Replace** `this->` with `interpreter->`
3. **Replace** `lastResult` with `interpreter->lastResult`
4. **Replace** `environment` with `interpreter->environment`
5. **Remove** from `Interpreter.cpp`

## Expected Result

| File | Current | After Extraction |
|------|---------|------------------|
| Interpreter.cpp | 3,219 lines | ~863 lines |
| ExprEvaluator.cpp | 230 lines | ~1,300 lines |
| StatementEvaluator.cpp | 200 lines | ~1,400 lines |

## Benefits

- **Clear separation** - Orchestration vs evaluation
- **Testable** - Each evaluator independently testable
- **Maintainable** - No more 3k line files
- **Follows pattern** - Matches Lua/Python architecture

## Next Steps

1. Extract remaining expression visitors (mechanical)
2. Extract remaining statement visitors (mechanical)
3. Update Interpreter to use evaluators
4. Remove duplicated code
5. Test thoroughly

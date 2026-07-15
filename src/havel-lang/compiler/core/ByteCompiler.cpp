void ByteCompiler::compileAssignExpression(const ast::AssignmentExpression &expression) {
  // Collect upvalues from both sides of assignment
  collectUpvaluesFromExpr(expression.left);
  collectUpvaluesFromExpr(expression.right);

  // Compile both expressions
  compileExpression(*expression.left);
  compileExpression(*expression.right);

  // Emit assignment operation
  emit(OpCode::ASSIGN);

  // Ensure assignment doesn't leak upvalues across scopes
  scope.exitOutermostContext();
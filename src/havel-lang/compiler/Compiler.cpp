#include "Compiler.h"

#ifdef HAVEL_ENABLE_LLVM
#include <cmath>
#include <stdexcept>

namespace {
template <typename Container, typename Key>
bool contains(const Container &container, const Key &key) {
  return container.find(key) != container.end();
}
} // namespace

namespace havel::compiler {
Compiler::Compiler() : builder(context) { Initialize(); }

void Compiler::Initialize() {
  // Initialize LLVM targets
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  // Create module and keep raw pointer
  auto modulePtr = std::make_unique<llvm::Module>("HavelJIT", context);
  module = modulePtr.get(); // Keep raw pointer for later use

  // Create execution engine with error handling
  std::string error;
  auto engineBuilder =
      llvm::EngineBuilder(std::move(modulePtr)); // Move ownership
  engineBuilder.setErrorStr(&error);

  // Configure the engine
  executionEngine.reset(
      engineBuilder.setEngineKind(llvm::EngineKind::JIT).create());

  if (!executionEngine) {
    throw std::runtime_error("Failed to create execution engine: " + error);
  }

  CreateStandardLibrary();
}

llvm::Value *Compiler::GenerateBinary(const ast::BinaryExpression &binary) {
  llvm::Value *left = GenerateExpression(*binary.left);
  llvm::Value *right = GenerateExpression(*binary.right);

  if (!left || !right) {
    throw std::runtime_error("Null operand in binary expression");
  }

  // Check types for arithmetic operations
  bool isFloat = left->getType()->isFloatingPointTy() ||
                 right->getType()->isFloatingPointTy();
  bool isInt =
      left->getType()->isIntegerTy() && right->getType()->isIntegerTy();

  if (isFloat && isInt) {
    throw std::runtime_error("Type mismatch: mixed integer and float operands");
  }

  switch (binary.operator_) {
  // Arithmetic Operations
  case ast::BinaryOperator::Add:
    if (isFloat)
      return builder.CreateFAdd(left, right, "faddtmp");
    return builder.CreateAdd(left, right, "addtmp");
  case ast::BinaryOperator::Sub:
    if (isFloat)
      return builder.CreateFSub(left, right, "fsubtmp");
    return builder.CreateSub(left, right, "subtmp");
  case ast::BinaryOperator::Mul:
    if (isFloat)
      return builder.CreateFMul(left, right, "fmultmp");
    return builder.CreateMul(left, right, "multmp");
  case ast::BinaryOperator::Div:
    if (isFloat)
      return builder.CreateFDiv(left, right, "fdivtmp");
    return builder.CreateSDiv(left, right, "sdivtmp");
  case ast::BinaryOperator::Mod:
    if (isFloat)
      return builder.CreateFRem(left, right, "fremtmp");
    return builder.CreateSRem(left, right, "sremtmp");
  case ast::BinaryOperator::Pow: {
    if (!isFloat) {
      // Convert integers to doubles for pow
      left = builder.CreateSIToFP(
          left, llvm::Type::getDoubleTy(builder.getContext()), "int2double");
      right = builder.CreateSIToFP(
          right, llvm::Type::getDoubleTy(builder.getContext()), "int2double");
    }
    auto powFunc = llvm::Intrinsic::getOrInsertDeclaration(
        module, llvm::Intrinsic::pow, {left->getType()});
    return builder.CreateCall(powFunc, {left, right}, "powtmp");
  }

  // Assignment Operators (assumes left is an lvalue, e.g., a variable)
  case ast::BinaryOperator::AddAssign:
  case ast::BinaryOperator::SubAssign:
  case ast::BinaryOperator::MulAssign:
  case ast::BinaryOperator::DivAssign:
  case ast::BinaryOperator::ModAssign:
  case ast::BinaryOperator::PowAssign: {
    if (!left->getType()->isPointerTy()) {
      throw std::runtime_error("Left operand of assignment must be an lvalue");
    }
    llvm::Value *result;
    switch (binary.operator_) {
    case ast::BinaryOperator::AddAssign:
      result = isFloat ? builder.CreateFAdd(left, right, "faddtmp")
                       : builder.CreateAdd(left, right, "addtmp");
      break;
    case ast::BinaryOperator::SubAssign:
      result = isFloat ? builder.CreateFSub(left, right, "fsubtmp")
                       : builder.CreateSub(left, right, "subtmp");
      break;
    case ast::BinaryOperator::MulAssign:
      result = isFloat ? builder.CreateFMul(left, right, "fmultmp")
                       : builder.CreateMul(left, right, "multmp");
      break;
    case ast::BinaryOperator::DivAssign:
      result = isFloat ? builder.CreateFDiv(left, right, "fdivtmp")
                       : builder.CreateSDiv(left, right, "sdivtmp");
      break;
    case ast::BinaryOperator::ModAssign:
      result = isFloat ? builder.CreateFRem(left, right, "fremtmp")
                       : builder.CreateSRem(left, right, "sremtmp");
      break;
    case ast::BinaryOperator::PowAssign: {
      if (!isFloat) {
        left = builder.CreateSIToFP(
            left, llvm::Type::getDoubleTy(builder.getContext()), "int2double");
        right = builder.CreateSIToFP(
            right, llvm::Type::getDoubleTy(builder.getContext()), "int2double");
      }
      auto powFunc = llvm::Intrinsic::getOrInsertDeclaration(
          module, llvm::Intrinsic::pow, {left->getType()});
      result = builder.CreateCall(powFunc, {left, right}, "powtmp");
      break;
    }
    default:
      throw std::runtime_error("Unknown assignment operator");
    }
    return builder.CreateStore(result, left);
  }

  // Comparison Operators
  case ast::BinaryOperator::Equal:
    if (isFloat)
      return builder.CreateFCmpOEQ(left, right, "eqtmp");
    return builder.CreateICmpEQ(left, right, "eqtmp");
  case ast::BinaryOperator::NotEqual:
    if (isFloat)
      return builder.CreateFCmpONE(left, right, "netmp");
    return builder.CreateICmpNE(left, right, "netmp");
  case ast::BinaryOperator::Less:
    if (isFloat)
      return builder.CreateFCmpOLT(left, right, "lttmp");
    return builder.CreateICmpSLT(left, right, "lttmp");
  case ast::BinaryOperator::Greater:
    if (isFloat)
      return builder.CreateFCmpOGT(left, right, "gttmp");
    return builder.CreateICmpSGT(left, right, "gttmp");
  case ast::BinaryOperator::LessEqual:
    if (isFloat)
      return builder.CreateFCmpOLE(left, right, "letmp");
    return builder.CreateICmpSLE(left, right, "letmp");
  case ast::BinaryOperator::GreaterEqual:
    if (isFloat)
      return builder.CreateFCmpOGE(left, right, "getmp");
    return builder.CreateICmpSGE(left, right, "getmp");

  // Logical Operators (short-circuit evaluation)
  case ast::BinaryOperator::And: {
    auto *currentBB = builder.GetInsertBlock();
    auto *rhsBB = llvm::BasicBlock::Create(builder.getContext(), "and.rhs",
                                           currentBB->getParent());
    auto *endBB = llvm::BasicBlock::Create(builder.getContext(), "and.end",
                                           currentBB->getParent());

    // Evaluate left operand
    builder.CreateCondBr(left, rhsBB, endBB);
    builder.SetInsertPoint(rhsBB);

    // Evaluate right operand
    builder.CreateBr(endBB);
    builder.SetInsertPoint(endBB);

    // PHI node to combine results
    auto *phi = builder.CreatePHI(llvm::Type::getInt1Ty(builder.getContext()),
                                  2, "andtmp");
    phi->addIncoming(llvm::ConstantInt::getFalse(builder.getContext()),
                     currentBB);
    phi->addIncoming(right, rhsBB);
    return phi;
  }
  case ast::BinaryOperator::Or: {
    auto *currentBB = builder.GetInsertBlock();
    auto *rhsBB = llvm::BasicBlock::Create(builder.getContext(), "or.rhs",
                                           currentBB->getParent());
    auto *endBB = llvm::BasicBlock::Create(builder.getContext(), "or.end",
                                           currentBB->getParent());

    // Evaluate left operand
    builder.CreateCondBr(left, endBB, rhsBB);
    builder.SetInsertPoint(rhsBB);

    // Evaluate right operand
    builder.CreateBr(endBB);
    builder.SetInsertPoint(endBB);

    // PHI node to combine results
    auto *phi = builder.CreatePHI(llvm::Type::getInt1Ty(builder.getContext()),
                                  2, "ortmp");
    phi->addIncoming(llvm::ConstantInt::getTrue(builder.getContext()),
                     currentBB);
    phi->addIncoming(right, rhsBB);
    return phi;
  }

  default:
    throw std::runtime_error("Unknown binary operator: " +
                             toString(binary.operator_));
  }
}
// Generate LLVM IR for expressions - THIS IS WHERE THE MAGIC HAPPENS! 🔥
llvm::Value *Compiler::GenerateExpression(const ast::Expression &expr) {
  switch (expr.kind) {
  case ast::NodeType::NumberLiteral:
    return GenerateNumberLiteral(static_cast<const ast::NumberLiteral &>(expr));

  case ast::NodeType::StringLiteral:
    return GenerateStringLiteral(static_cast<const ast::StringLiteral &>(expr));

  case ast::NodeType::Identifier:
    return GenerateIdentifier(static_cast<const ast::Identifier &>(expr));

  case ast::NodeType::PipelineExpression:
    return GeneratePipeline(static_cast<const ast::PipelineExpression &>(expr));

  case ast::NodeType::CallExpression:
    return GenerateCall(static_cast<const ast::CallExpression &>(expr));

  case ast::NodeType::MemberExpression:
    return GenerateMember(static_cast<const ast::MemberExpression &>(expr));

  case ast::NodeType::BinaryExpression:
    return GenerateBinary(static_cast<const ast::BinaryExpression &>(expr));

  default:
    throw std::runtime_error("Unknown expression type in LLVM generation");
  }
}

llvm::Value *Compiler::GenerateIdentifier(const ast::Identifier &id) {
  // First check variables (like function parameters, let bindings)
  if (contains(namedValues, id.symbol)) {
    return namedValues[id.symbol]; // Return variable value
  }

  // Then check functions (for function references)
  if (contains(functions, id.symbol)) {
    return functions[id.symbol]; // Return function pointer
  }

  // Unknown identifier - throw clear error
  throw std::runtime_error("Unknown identifier: " + id.symbol);
}

llvm::Function *Compiler::CompileProgram(const ast::Program &program) {
  llvm::Type *voidTy = llvm::Type::getVoidTy(context);
  llvm::FunctionType *fnTy = llvm::FunctionType::get(voidTy, false);
  llvm::Function *mainFunc = llvm::Function::Create(
      fnTy, llvm::Function::ExternalLinkage, "main", module);

  llvm::BasicBlock *entry =
      llvm::BasicBlock::Create(context, "entry", mainFunc);
  builder.SetInsertPoint(entry);

  for (const auto &stmt : program.body) {
    GenerateStatement(*stmt);
  }

  builder.CreateRetVoid();
  return mainFunc;
}

llvm::Value *Compiler::GenerateCall(const ast::CallExpression &call) {
  // Get the function being called
  llvm::Value *calleeValue = GenerateExpression(*call.callee);

  // Must be a function
  llvm::Function *calleeFunc = llvm::dyn_cast<llvm::Function>(calleeValue);
  if (!calleeFunc) {
    throw std::runtime_error("Called value is not a function");
  }

  // Generate arguments
  std::vector<llvm::Value *> args;
  for (const auto &arg : call.args) {
    args.push_back(GenerateExpression(*arg));
  }

  // Verify argument count
  if (args.size() != calleeFunc->arg_size()) {
    throw std::runtime_error("Incorrect number of arguments for function call");
  }

  return builder.CreateCall(calleeFunc, args, "calltmp");
}

llvm::Value *
Compiler::GeneratePipeline(const ast::PipelineExpression &pipeline) {
  if (pipeline.stages.empty()) {
    return llvm::ConstantPointerNull::get(llvm::PointerType::get(context, 0));
  }

  // Start with first stage: clipboard.out
  llvm::Value *result = GenerateExpression(*pipeline.stages[0]);

  // Chain through each subsequent stage
  for (size_t i = 1; i < pipeline.stages.size(); ++i) {
    const auto &stageExpr = pipeline.stages[i];
    llvm::Function *func = nullptr;
    std::vector<llvm::Value *> args = {result};
    // Previous result as first arg

    if (stageExpr->kind == ast::NodeType::CallExpression) {
      // Handle: text.upper("extra", "args")
      const auto &call = static_cast<const ast::CallExpression &>(*stageExpr);

      llvm::Value *calleeValue = GenerateExpression(*call.callee);
      func = llvm::dyn_cast<llvm::Function>(calleeValue);

      if (!func) {
        throw std::runtime_error("Pipeline stage is not a callable function");
      }

      // Add additional arguments from the call
      for (const auto &arg : call.args) {
        args.push_back(GenerateExpression(*arg));
      }
    } else if (stageExpr->kind == ast::NodeType::Identifier) {
      // Handle: send (simple function call)
      const auto &id = static_cast<const ast::Identifier &>(*stageExpr);

      if (!contains(functions, id.symbol)) {
        throw std::runtime_error("Unknown pipeline function: " + id.symbol);
      }

      func = functions[id.symbol];
    } else if (stageExpr->kind == ast::NodeType::MemberExpression) {
      // Handle: text.upper
      const auto &member =
          static_cast<const ast::MemberExpression &>(*stageExpr);
      llvm::Value *memberValue = GenerateMember(member);
      func = llvm::dyn_cast<llvm::Function>(memberValue);

      if (!func) {
        throw std::runtime_error("Pipeline member is not a function");
      }
    } else {
      throw std::runtime_error("Invalid pipeline stage - must be function "
                               "call, identifier, or member access");
    }

    if (!func) {
      throw std::runtime_error("Failed to resolve function in pipeline stage");
    }

    // Verify argument count matches function signature
    if (args.size() != func->arg_size()) {
      throw std::runtime_error("Pipeline function argument count mismatch");
    }

    // Execute the pipeline stage: result = func(previous_result, ...args)
    result = builder.CreateCall(func, args, "pipeline_stage");
  }

  return result;
}

// for clipboard.get, text.upper, etc.
llvm::Value *Compiler::GenerateMember(const ast::MemberExpression &member) {
  // Get the object (clipboard, text, window, etc.)
  const auto *objectId =
      dynamic_cast<const ast::Identifier *>(member.object.get());
  const auto *propertyId =
      dynamic_cast<const ast::Identifier *>(member.property.get());

  if (!objectId || !propertyId) {
    throw std::runtime_error("Complex member expressions not yet supported");
  }

  // Construct fully qualified name: "clipboard.out", "text.upper", etc.
  std::string memberName = objectId->symbol + "." + propertyId->symbol;

  // Look up in functions table
  if (contains(functions, memberName)) {
    return functions[memberName];
  }

  throw std::runtime_error("Unknown member function: " + memberName);
}

// Generate Variable management - for let bindings and function params!
void Compiler::SetVariable(const std::string &name, llvm::Value *value) {
  namedValues[name] = value;
}

llvm::Value *Compiler::GetVariable(const std::string &name) {
  if (contains(namedValues, name)) {
    return namedValues[name];
  }
  throw std::runtime_error("Unknown variable: " + name);
}
void Compiler::RegisterHotkey(const ast::HotkeyLiteral &hotkey,
                              llvm::Function *handler) {
  // Store the LLVM function
  hotkeyHandlers[hotkey.toString()] = handler;

  // Register with the system
  RegisterSystemHotkey(hotkey);
}

void Compiler::RegisterSystemHotkey(const ast::HotkeyLiteral &hotkey) {
  // Interface with your existing HotkeyManager or OS-specific registration
  info("Registered hotkey: " + hotkey.toString());
}
void Compiler::CreateStandardLibrary() {
  // Create function types
  llvm::Type *int8Type = llvm::Type::getInt8Ty(context);
  llvm::Type *stringType = llvm::PointerType::get(context, 0);
  // i8* for strings
  llvm::Type *voidType = llvm::Type::getVoidTy(context);

  // clipboard.get() -> string
  llvm::FunctionType *clipboardGetType =
      llvm::FunctionType::get(stringType, {}, false);
  functions["clipboard.get"] =
      llvm::Function::Create(clipboardGetType, llvm::Function::ExternalLinkage,
                             "clipboard_get", module);

  // text.upper(string) -> string
  llvm::FunctionType *textUpperType =
      llvm::FunctionType::get(stringType, {stringType}, false);
  functions["text.upper"] = llvm::Function::Create(
      textUpperType, llvm::Function::ExternalLinkage, "text_upper", module);

  // send(string) -> void
  llvm::FunctionType *sendType =
      llvm::FunctionType::get(voidType, {stringType}, false);
  functions["send"] = llvm::Function::Create(
      sendType, llvm::Function::ExternalLinkage, "send", module);

  // window.next() -> void
  llvm::FunctionType *windowNextType =
      llvm::FunctionType::get(voidType, {}, false);
  functions["window.next"] = llvm::Function::Create(
      windowNextType, llvm::Function::ExternalLinkage, "window_next", module);
}
llvm::Value *Compiler::GenerateStringLiteral(const ast::StringLiteral &str) {
  // Create constant string data
  llvm::Constant *strConstant =
      llvm::ConstantDataArray::getString(context, str.value, true);

  // Create global variable to hold the string
  llvm::GlobalVariable *globalStr =
      new llvm::GlobalVariable(*module, strConstant->getType(),
                               true,                              // isConstant
                               llvm::GlobalValue::PrivateLinkage, // linkage
                               strConstant,                       // initializer
                               "strlit"                           // name
      );

  globalStr->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  // Return pointer to the string data
  return builder.CreateInBoundsGEP(strConstant->getType(), globalStr,
                                   {builder.getInt32(0), builder.getInt32(0)},
                                   "strptr");
}
llvm::Value *Compiler::GenerateNumberLiteral(const ast::NumberLiteral &num) {
  // Check if it's actually an integer
  if (num.value == std::floor(num.value)) {
    return llvm::ConstantInt::get(llvm::Type::getInt64Ty(context),
                                  static_cast<int64_t>(num.value));
  }
  return llvm::ConstantFP::get(llvm::Type::getDoubleTy(context), num.value);
}
void Compiler::RegisterType(const std::string &name,
                            const ast::TypeDefinition &definition) {
  // Simply store the type definition for later use
  typeRegistry[name] = definition;

  // Optional: Log the registration
  info("Registered type: " + name);
}
void Compiler::LoadModule(const std::string &moduleName) {
  // Check if already loaded
  if (loadedModules.find(moduleName) != loadedModules.end()) {
    return; // Already loaded
  }

  // Mark as loaded to prevent cycles
  loadedModules[moduleName] = true;

  // For now, just log the module loading
  info("Loading module: " + moduleName);

  // TODO: Actual module loading logic
  // This is where you'd:
  // 1. Find the module file (e.g., "std/collections.havel")
  // 2. Parse it into an AST
  // 3. Generate LLVM IR for it
  // 4. Link it into the current module

  // Example of what it might look like:
  /*
  std::string modulePath = FindModulePath(moduleName);
  if (modulePath.empty()) {
      throw std::runtime_error("Module not found: " + moduleName);
  }

  // Read and parse the module
  std::string moduleCode = ReadFile(modulePath);
  parser::Parser parser;
  auto moduleAST = parser.produceAST(moduleCode);

  // Generate LLVM IR for the module
  for (const auto& stmt : moduleAST->body) {
      GenerateStatement(*stmt);
  }
  */
}
llvm::Value *Compiler::GenerateStatement(const ast::Statement &stmt) {
  switch (stmt.kind) {
  case ast::NodeType::ExpressionStatement: {
    const auto &exprStmt = static_cast<const ast::ExpressionStatement &>(stmt);
    return GenerateExpression(*exprStmt.expression);
  }

  case ast::NodeType::LetDeclaration: {
    const auto &letStmt = static_cast<const ast::LetDeclaration &>(stmt);

    // Check if there's a value to assign
    if (!letStmt.value) {
      throw std::runtime_error(
          "LetDeclaration without value not supported in LLVM codegen");
    }

    // Generate the value expression
    llvm::Value *value = GenerateExpression(*letStmt.value);

    // Get the identifier name from the pattern
    std::string identifierName;
    if (auto *ident =
            dynamic_cast<const ast::Identifier *>(letStmt.pattern.get())) {
      identifierName = ident->symbol;
    } else {
      // For destructuring patterns, we'd need more complex handling
      // For now, skip compilation of destructuring
      return nullptr;
    }

    // Create alloca for the binding
    llvm::AllocaInst *alloca = builder.CreateAlloca(
        value->getType(), nullptr,
        identifierName // Now passing string, not unique_ptr
    );

    // Store the value
    builder.CreateStore(value, alloca);

    // Add to the symbol table
    symbolTable[identifierName] = alloca;

    return value;
  }

  case ast::NodeType::FunctionDeclaration: {
    const auto &funcDecl = static_cast<const ast::FunctionDeclaration &>(stmt);

    // Create function type
    std::vector<llvm::Type *> paramTypes;
    for (const auto &param : funcDecl.parameters) {
      // Default to double for now (you might want type inference)
      paramTypes.push_back(llvm::Type::getDoubleTy(context));
      info("Parameter: " + param->symbol);
    }

    llvm::FunctionType *funcType =
        llvm::FunctionType::get(llvm::Type::getDoubleTy(context), // Return type
                                paramTypes,
                                false // Not variadic
        );

    // Create function
    llvm::Function *function =
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                               funcDecl.name->symbol, module);

    // Set parameter names
    auto paramIt = funcDecl.parameters.begin();
    for (auto &arg : function->args()) {
      arg.setName((*paramIt)->symbol);
      ++paramIt;
    }

    // Create function body
    llvm::BasicBlock *funcBlock =
        llvm::BasicBlock::Create(context, "entry", function);

    // Save the current state
    llvm::BasicBlock *prevBlock = builder.GetInsertBlock();
    std::unordered_map<std::string, llvm::Value *> prevSymbolTable =
        symbolTable;

    // Switch to function block
    builder.SetInsertPoint(funcBlock);

    // Add parameters to symbol table
    for (auto &arg : function->args()) {
      llvm::AllocaInst *alloca =
          builder.CreateAlloca(arg.getType(), nullptr, arg.getName());
      builder.CreateStore(&arg, alloca);
      symbolTable[arg.getName().str()] = alloca;
    }

    // Generate function body
    llvm::Value *bodyValue = nullptr;
    for (const auto &stmt : funcDecl.body->body) {
      bodyValue = GenerateStatement(*stmt);
    }

    // Create return
    builder.CreateRet(bodyValue);

    // Restore previous state
    builder.SetInsertPoint(prevBlock);
    symbolTable = prevSymbolTable;

    // Add function to symbol table
    symbolTable[funcDecl.name->symbol] =
        function; // Now passing string, not unique_ptr [funcDecl.name] =
                  // function;

    return function;
  }

  case ast::NodeType::HotkeyBinding: {
    const auto &hotkeyStmt = static_cast<const ast::HotkeyBinding &>(stmt);

    // Create a function for the hotkey handler
    llvm::FunctionType *hotkeyFuncType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(context), {}, // No parameters
        false);

    // Create a unique name for the handler function.
    // Using the first hotkey for the name is fine for debugging.
    std::string hotkeyFuncName = "hotkey_handler";
    if (!hotkeyStmt.hotkeys.empty()) {
      hotkeyFuncName = "hotkey_" + hotkeyStmt.hotkeys[0]->toString();
    }

    llvm::Function *hotkeyFunc =
        llvm::Function::Create(hotkeyFuncType, llvm::Function::ExternalLinkage,
                               hotkeyFuncName, module);

    // Create a function body
    llvm::BasicBlock *hotkeyBlock =
        llvm::BasicBlock::Create(context, "entry", hotkeyFunc);

    // Save current state
    llvm::BasicBlock *prevBlock = builder.GetInsertBlock();
    std::unordered_map<std::string, llvm::Value *> prevSymbolTable =
        symbolTable;

    // Switch to the hotkey block
    builder.SetInsertPoint(hotkeyBlock);

    // Generate hotkey body
    GenerateStatement(*hotkeyStmt.action);

    // Create return
    builder.CreateRetVoid();

    // Restore previous state
    builder.SetInsertPoint(prevBlock);
    symbolTable = prevSymbolTable;

    // Register the hotkey for each hotkey literal in the binding
    for (const auto &hotkeyExpr : hotkeyStmt.hotkeys) {
      // Evaluate expression at compile-time if constant
      if (auto *hotkeyLit =
              dynamic_cast<const ast::HotkeyLiteral *>(hotkeyExpr.get())) {
        RegisterHotkey(*hotkeyLit, hotkeyFunc);
      } else if (auto *strLit = dynamic_cast<const ast::StringLiteral *>(
                     hotkeyExpr.get())) {
        // Allow string literals too: "^+F1" => action()
        ast::HotkeyLiteral literal(strLit->value);
        RegisterHotkey(literal, hotkeyFunc);
      } else {
        // For variables/expressions, register at runtime
        warn("Hotkey expression is not constant, will register at runtime");
        // Emit code to call IO::Hotkey() at runtime
      }
    }

    return hotkeyFunc;
  }

  case ast::NodeType::ImportStatement: {
    const auto &importStmt = static_cast<const ast::ImportStatement &>(stmt);

    // Load the module (you'll need to implement module loading)
    LoadModule(importStmt.modulePath);

    // Return null/void for import statements
    return llvm::UndefValue::get(llvm::Type::getVoidTy(context));
  }

  case ast::NodeType::TypeDeclaration: {
    const auto &typeDecl = static_cast<const ast::TypeDeclaration &>(stmt);

    RegisterType(typeDecl.name, *typeDecl.definition);

    return llvm::UndefValue::get(llvm::Type::getVoidTy(context));
  }

  case ast::NodeType::TryExpression: {
    const auto &tryExpr = static_cast<const ast::TryExpression &>(stmt);

    // Create basic blocks
    llvm::BasicBlock *tryBlock = llvm::BasicBlock::Create(
        context, "try", builder.GetInsertBlock()->getParent());
    llvm::BasicBlock *catchBlock = llvm::BasicBlock::Create(
        context, "catch", builder.GetInsertBlock()->getParent());
    llvm::BasicBlock *continueBlock = llvm::BasicBlock::Create(
        context, "continue", builder.GetInsertBlock()->getParent());

    // Jump to try block
    builder.CreateBr(tryBlock);

    // Generate try block
    builder.SetInsertPoint(tryBlock);
    llvm::Value *tryValue = GenerateExpression(*tryExpr.tryBody);
    builder.CreateBr(continueBlock);

    // Generate catch block
    builder.SetInsertPoint(catchBlock);
    llvm::Value *catchValue = GenerateExpression(*tryExpr.catchBody);
    builder.CreateBr(continueBlock);

    // Generate continue block with PHI
    builder.SetInsertPoint(continueBlock);
    llvm::PHINode *phi =
        builder.CreatePHI(tryValue->getType(), 2, "try_result");
    phi->addIncoming(tryValue, tryBlock);
    phi->addIncoming(catchValue, catchBlock);

    return phi;
  }

  default:
    throw std::runtime_error("Unsupported statement kind in LLVM codegen: " +
                             std::to_string(static_cast<int>(stmt.kind)));
  }
}

bool Compiler::VerifyModule() const {
  return !llvm::verifyModule(*module, &llvm::errs());
}
void Compiler::DumpModule() const { module->print(llvm::outs(), nullptr); }

llvm::Function *Compiler::CompileHotkeyAction(const ast::Expression &expr) {
  // This is a stub implementation - will be filled in later
  return nullptr; // Return null for now
}

Compiler::HotkeyActionFunc
Compiler::GetCompiledFunction(const std::string &name) {
  // This is a stub implementation - will be filled in later
  return nullptr; // Return null function pointer for now
}
} // namespace havel::compiler
#endif // HAVEL_ENABLE_LLVM

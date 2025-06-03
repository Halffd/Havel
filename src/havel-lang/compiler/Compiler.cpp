#include "Compiler.h"
#include <llvm/Support/raw_ostream.h>
#include <iostream>

namespace havel::compiler {
    Compiler::Compiler() : builder(context) {
        Initialize();
    }

    void Compiler::Initialize() {
        // Initialize LLVM
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();

        // Create module
        auto modulePtr = std::make_unique<llvm::Module>("HavelJIT", context);
        module = modulePtr.get(); // Store raw pointer BEFORE the move

        // Create execution engine - it takes ownership
        std::string error;
        executionEngine.reset(llvm::EngineBuilder(std::move(modulePtr))
            .setErrorStr(&error)
            .setEngineKind(llvm::EngineKind::JIT)
            .create());

        if (!executionEngine) {
            throw std::runtime_error(
                "Failed to create execution engine: " + error);
        }

        CreateStandardLibrary();
    }

    // Generate LLVM IR for expressions - THIS IS WHERE THE MAGIC HAPPENS! 🔥
    llvm::Value *Compiler::GenerateExpression(const ast::Expression &expr) {
        switch (expr.kind) {
            case ast::NodeType::NumberLiteral:
                return GenerateNumberLiteral(
                    static_cast<const ast::NumberLiteral &>(expr));

            case ast::NodeType::StringLiteral:
                return GenerateStringLiteral(
                    static_cast<const ast::StringLiteral &>(expr));

            case ast::NodeType::Identifier:
                return GenerateIdentifier(
                    static_cast<const ast::Identifier &>(expr));

            case ast::NodeType::PipelineExpression:
                return GeneratePipeline(
                    static_cast<const ast::PipelineExpression &>(expr));

            case ast::NodeType::CallExpression:
                return GenerateCall(
                    static_cast<const ast::CallExpression &>(expr));

            case ast::NodeType::MemberExpression:
                return GenerateMember(
                    static_cast<const ast::MemberExpression &>(expr));

            case ast::NodeType::BinaryExpression:
                return GenerateBinary(
                    static_cast<const ast::BinaryExpression &>(expr));

            default:
                throw std::runtime_error(
                    "Unknown expression type in LLVM generation");
        }
    }

    llvm::Value *Compiler::GenerateIdentifier(const ast::Identifier &id) {
        // First check variables (like function parameters, let bindings)
        if (namedValues.contains(id.symbol)) {
            return namedValues[id.symbol]; // Return variable value
        }

        // Then check functions (for function references)
        if (functions.contains(id.symbol)) {
            return functions[id.symbol]; // Return function pointer
        }

        // Unknown identifier - throw clear error
        throw std::runtime_error("Unknown identifier: " + id.symbol);
    }

    // ✅ FIXED GenerateCall - handles YOUR CallExpression AST! ⚡
    llvm::Value *Compiler::GenerateCall(const ast::CallExpression &call) {
        // Get the function being called
        llvm::Value *calleeValue = GenerateExpression(*call.callee);

        // Must be a function
        llvm::Function *calleeFunc = llvm::dyn_cast<
            llvm::Function>(calleeValue);
        if (!calleeFunc) {
            throw std::runtime_error("Called value is not a function");
        }

        // Generate arguments
        std::vector<llvm::Value *> args;
        for (const auto &arg: call.args) {
            args.push_back(GenerateExpression(*arg));
        }

        // Verify argument count
        if (args.size() != calleeFunc->arg_size()) {
            throw std::runtime_error(
                "Incorrect number of arguments for function call");
        }

        return builder.CreateCall(calleeFunc, args, "calltmp");
    }

    llvm::Value *Compiler::GeneratePipeline(
        const ast::PipelineExpression &pipeline) {
        if (pipeline.stages.empty()) {
            return llvm::ConstantPointerNull::get(
                llvm::Type::getInt8PtrTy(context));
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
                const auto &call = static_cast<const ast::CallExpression &>(*
                    stageExpr);

                llvm::Value *calleeValue = GenerateExpression(*call.callee);
                func = llvm::dyn_cast<llvm::Function>(calleeValue);

                if (!func) {
                    throw std::runtime_error(
                        "Pipeline stage is not a callable function");
                }

                // Add additional arguments from the call
                for (const auto &arg: call.args) {
                    args.push_back(GenerateExpression(*arg));
                }
            } else if (stageExpr->kind == ast::NodeType::Identifier) {
                // Handle: send (simple function call)
                const auto &id = static_cast<const ast::Identifier &>(*
                    stageExpr);

                if (!functions.contains(id.symbol)) {
                    throw std::runtime_error(
                        "Unknown pipeline function: " + id.symbol);
                }

                func = functions[id.symbol];
            } else if (stageExpr->kind == ast::NodeType::MemberExpression) {
                // Handle: text.upper
                const auto &member = static_cast<const ast::MemberExpression &>(
                    *stageExpr);
                llvm::Value *memberValue = GenerateMember(member);
                func = llvm::dyn_cast<llvm::Function>(memberValue);

                if (!func) {
                    throw std::runtime_error(
                        "Pipeline member is not a function");
                }
            } else {
                throw std::runtime_error(
                    "Invalid pipeline stage - must be function call, identifier, or member access");
            }

            if (!func) {
                throw std::runtime_error(
                    "Failed to resolve function in pipeline stage");
            }

            // Verify argument count matches function signature
            if (args.size() != func->arg_size()) {
                throw std::runtime_error(
                    "Pipeline function argument count mismatch");
            }

            // Execute the pipeline stage: result = func(previous_result, ...args)
            result = builder.CreateCall(func, args, "pipeline_stage");
        }

        return result;
    }

    // for clipboard.get, text.upper, etc.
    llvm::Value *Compiler::GenerateMember(const ast::MemberExpression &member) {
        // Get the object (clipboard, text, window, etc.)
        const auto *objectId = dynamic_cast<const ast::Identifier *>(member.
            object.get());
        const auto *propertyId = dynamic_cast<const ast::Identifier *>(member.
            property.get());

        if (!objectId || !propertyId) {
            throw std::runtime_error(
                "Complex member expressions not yet supported");
        }

        // Construct fully qualified name: "clipboard.out", "text.upper", etc.
        std::string memberName = objectId->symbol + "." + propertyId->symbol;

        // Look up in functions table
        if (functions.contains(memberName)) {
            return functions[memberName];
        }

        throw std::runtime_error("Unknown member function: " + memberName);
    }

    // Generate Variable management - for let bindings and function params!
    void Compiler::SetVariable(const std::string &name, llvm::Value *value) {
        namedValues[name] = value;
    }

    llvm::Value *Compiler::GetVariable(const std::string &name) {
        if (namedValues.contains(name)) {
            return namedValues[name];
        }
        throw std::runtime_error("Unknown variable: " + name);
    }

    void Compiler::CreateStandardLibrary() {
        // Create function types
        llvm::Type *int8Type = llvm::Type::getInt8Ty(context);
        llvm::Type *stringType = llvm::PointerType::get(int8Type, 0);
        // i8* for strings
        llvm::Type *voidType = llvm::Type::getVoidTy(context);

        // clipboard.get() -> string
        llvm::FunctionType *clipboardGetType =
                llvm::FunctionType::get(stringType, {}, false);
        functions["clipboard.get"] =
                llvm::Function::Create(clipboardGetType,
                                       llvm::Function::ExternalLinkage,
                                       "clipboard_get", module);

        // text.upper(string) -> string
        llvm::FunctionType *textUpperType =
                llvm::FunctionType::get(stringType, {stringType}, false);
        functions["text.upper"] =
                llvm::Function::Create(textUpperType,
                                       llvm::Function::ExternalLinkage,
                                       "text_upper", module);

        // send(string) -> void
        llvm::FunctionType *sendType =
                llvm::FunctionType::get(voidType, {stringType}, false);
        functions["send"] =
                llvm::Function::Create(sendType,
                                       llvm::Function::ExternalLinkage,
                                       "send", module);

        // window.next() -> void
        llvm::FunctionType *windowNextType =
                llvm::FunctionType::get(voidType, {}, false);
        functions["window.next"] =
                llvm::Function::Create(windowNextType,
                                       llvm::Function::ExternalLinkage,
                                       "window_next", module);
    }
} // namespace havel::compiler

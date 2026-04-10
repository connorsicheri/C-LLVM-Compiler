//add more header files if your want
#include "IRGenerator.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "Declarations.h"
#include "Program.h"
#include "Exprs.h"
#include "Statements.h"
#include "Terms.h"

namespace minicc {

    //=== Helper functions ===

    llvm::Type* IRGenerator::getLLVMType(Type type) {
        llvm::Type* baseType;
        switch (type.primitiveType()) {
            case Type::Void: baseType = llvm::Type::getVoidTy(*TheContext); break;
            case Type::Int:  baseType = llvm::Type::getInt32Ty(*TheContext); break;
            case Type::Bool: baseType = llvm::Type::getInt1Ty(*TheContext); break;
            case Type::Char: baseType = llvm::Type::getInt8Ty(*TheContext); break;
        }
        if (type.arrayBound() > 0)
            return llvm::ArrayType::get(baseType, type.arrayBound());
        return baseType;
    }

    //=== Visitor implementations ===

    void IRGenerator::visitProgram(Program *prog) {
        // Initialize llvm module and builder
        TheModule = std::make_unique<llvm::Module>(ModuleName, *TheContext);
        TheBuilder = std::make_unique<llvm::IRBuilder<>>(*TheContext);

        // Declare syslib (minicio) functions if #include "minicio.h" was used
        if (prog->syslibFlag()) {
            // int getint()
            auto *getintTy = llvm::FunctionType::get(
                llvm::Type::getInt32Ty(*TheContext), {}, false);
            llvm::Function::Create(getintTy, llvm::Function::ExternalLinkage,
                "getint", TheModule.get());

            // void putint(int)
            auto *putintTy = llvm::FunctionType::get(
                llvm::Type::getVoidTy(*TheContext),
                {llvm::Type::getInt32Ty(*TheContext)}, false);
            llvm::Function::Create(putintTy, llvm::Function::ExternalLinkage,
                "putint", TheModule.get());

            // void putcharacter(char)
            auto *putcharTy = llvm::FunctionType::get(
                llvm::Type::getVoidTy(*TheContext),
                {llvm::Type::getInt8Ty(*TheContext)}, false);
            llvm::Function::Create(putcharTy, llvm::Function::ExternalLinkage,
                "putcharacter", TheModule.get());

            // void putnewline()
            auto *putnewlineTy = llvm::FunctionType::get(
                llvm::Type::getVoidTy(*TheContext), {}, false);
            llvm::Function::Create(putnewlineTy, llvm::Function::ExternalLinkage,
                "putnewline", TheModule.get());
        }

        // Pre-declare all user functions so forward references work
        for (size_t i = 0; i < prog->numChildren(); i++) {
            ASTNode* child = prog->getChild(i);
            if (child && child->isFuncDecl()) {
                FuncDeclaration* func = (FuncDeclaration*)child;
                std::string funcName = func->name();
                // Skip if already declared (e.g. syslib or duplicate declaration)
                if (!TheModule->getFunction(funcName)) {
                    std::vector<llvm::Type*> paramTypes;
                    for (size_t j = 0; j < func->numParameters(); j++)
                        paramTypes.push_back(getLLVMType(func->parameter(j)->type()));
                    llvm::Type* retType = getLLVMType(func->returnType());
                    llvm::FunctionType* funcType = llvm::FunctionType::get(
                        retType, paramTypes, false);
                    // false means not variable number of params
                    llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                        funcName, TheModule.get());
                }
            }
        }

        // Now visit all children: VarDecls create globals, FuncDecls fill bodies
        for (size_t i = 0; i < prog->numChildren(); i++) {
            ASTNode* child = prog->getChild(i);
            if (child)
                child->accept(this);
        }
    }

    void IRGenerator::visitVarDecl(VarDeclaration *decl) {
        // Get the scope's symbol table (verifier already inserted entries)
        ASTNode* scope = decl->getParentScope();
        VarSymbolTable* table = scope->scopeVarTable();
        bool isGlobal = scope->isProgram();

        for (size_t i = 0; i < decl->numVarReferences(); i++) {
            VarReference* ref = decl->varReference(i);
            std::string name = ref->identifier()->name();
            VarSymbolEntry* entry = table->lookup(name);
            Type varType = entry->VarType;
            llvm::Type* llvmType = getLLVMType(varType);

            if (isGlobal) {
                // Global variable: create a GlobalVariable with zero initializer
                llvm::Constant* initializer;
                if (varType.arrayBound() > 0)
                    initializer = llvm::ConstantAggregateZero::get(
                        (llvm::ArrayType*)llvmType);
                else
                    initializer = llvm::ConstantInt::get(llvmType, 0, true);

                auto* gv = new llvm::GlobalVariable(
                    *TheModule, llvmType, false,
                    llvm::GlobalVariable::CommonLinkage,
                    initializer, name);
                entry->LLVMValue = gv;
            } else {
                // Local variable: create a stack alloca
                llvm::Value* alloca = TheBuilder->CreateAlloca(
                    llvmType, nullptr, name);
                entry->LLVMValue = alloca;
            }
        }
    }

    void IRGenerator::visitFuncDecl(FuncDeclaration *func) {
        if (!func->hasBody())
            return;

        // Get the llvm::Function we pre-declared in visitProgram
        llvm::Function* llvmFunc = TheModule->getFunction(func->name());

        // Create the entry basic block
        llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(
            *TheContext, "entry", llvmFunc);
        TheBuilder->SetInsertPoint(entryBB);

        // Allocate local stack space for each parameter and store the arg value
        VarSymbolTable* bodyTable = func->body()->scopeVarTable();
        for (size_t i = 0; i < func->numParameters(); i++) {
            Parameter* param = func->parameter(i);
            std::string paramName = param->name();
            llvm::Type* paramLLVMType = getLLVMType(param->type());

            // Create alloca for the param
            llvm::Value* alloca = TheBuilder->CreateAlloca(
                paramLLVMType, nullptr, paramName);
            // Store the incoming argument into the alloca
            TheBuilder->CreateStore(llvmFunc->getArg(i), alloca);
            // Record in symbol table so visitVarExpr can find it
            bodyTable->lookup(paramName)->LLVMValue = alloca;
        }

        // Visit the function body
        func->body()->accept(this);

        // If void function and current block has no terminator, add implicit return void
        if (!TheBuilder->GetInsertBlock()->getTerminator()) {
            if (func->returnType().isVoid())
                TheBuilder->CreateRetVoid();
        }
    }

    void IRGenerator::visitIfStmt(IfStatement *stmt) {
        // Evaluate the condition
        stmt->condExpr()->accept(this);
        llvm::Value* condVal = ExprValues[stmt->condExpr()];

        llvm::Function* func = TheBuilder->GetInsertBlock()->getParent();

        if (stmt->hasElse()) {
            // if-then-else: 3 blocks
            llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*TheContext, "if.then", func);
            llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(*TheContext, "if.else", func);
            llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(*TheContext, "if.after", func);

            TheBuilder->CreateCondBr(condVal, thenBB, elseBB);

            // Then block
            TheBuilder->SetInsertPoint(thenBB);
            stmt->thenStmt()->accept(this);
            if (!TheBuilder->GetInsertBlock()->getTerminator())
                TheBuilder->CreateBr(afterBB);

            // Else block
            TheBuilder->SetInsertPoint(elseBB);
            stmt->elseStmt()->accept(this);
            if (!TheBuilder->GetInsertBlock()->getTerminator())
                TheBuilder->CreateBr(afterBB);

            // Continue in after block
            TheBuilder->SetInsertPoint(afterBB);
        } else {
            // if-then (no else): 2 blocks
            llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*TheContext, "if.then", func);
            llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(*TheContext, "if.after", func);

            TheBuilder->CreateCondBr(condVal, thenBB, afterBB);

            // Then block
            TheBuilder->SetInsertPoint(thenBB);
            stmt->thenStmt()->accept(this);
            if (!TheBuilder->GetInsertBlock()->getTerminator())
                TheBuilder->CreateBr(afterBB);

            // Continue in after block
            TheBuilder->SetInsertPoint(afterBB);
        }
    }

    void IRGenerator::visitForStmt(ForStatement *stmt) {
        llvm::Function* func = TheBuilder->GetInsertBlock()->getParent();
        llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*TheContext, "for.cond", func);
        llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(*TheContext, "for.body", func);
        llvm::BasicBlock* iterBB = llvm::BasicBlock::Create(*TheContext, "for.iter", func);
        llvm::BasicBlock* exitBB = llvm::BasicBlock::Create(*TheContext, "for.exit", func);

        // Push break/continue targets for nested break/continue statements
        BreakTargets.push_back(exitBB);
        ContinueTargets.push_back(iterBB);

        // Evaluate init expression (e.g. i = 0)
        if (stmt->initExpr())
            stmt->initExpr()->accept(this);
        TheBuilder->CreateBr(condBB);

        // Condition block
        TheBuilder->SetInsertPoint(condBB);
        if (stmt->condExpr()) {
            stmt->condExpr()->accept(this);
            llvm::Value* condVal = ExprValues[stmt->condExpr()];
            TheBuilder->CreateCondBr(condVal, bodyBB, exitBB);
        } else {
            // No condition means infinite loop (condition always true)
            TheBuilder->CreateBr(bodyBB);
        }

        // Body block
        TheBuilder->SetInsertPoint(bodyBB);
        stmt->body()->accept(this);
        if (!TheBuilder->GetInsertBlock()->getTerminator())
            TheBuilder->CreateBr(iterBB);

        // Iter block (e.g. i = i + 1)
        TheBuilder->SetInsertPoint(iterBB);
        if (stmt->iterExpr())
            stmt->iterExpr()->accept(this);
        TheBuilder->CreateBr(condBB);

        // Pop break/continue targets
        BreakTargets.pop_back();
        ContinueTargets.pop_back();

        // Continue generating code in exit block
        TheBuilder->SetInsertPoint(exitBB);
    }

    void IRGenerator::visitWhileStmt(WhileStatement *stmt) {
        llvm::Function* func = TheBuilder->GetInsertBlock()->getParent();
        llvm::BasicBlock* condBB = llvm::BasicBlock::Create(*TheContext, "while.cond", func);
        llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(*TheContext, "while.body", func);
        llvm::BasicBlock* exitBB = llvm::BasicBlock::Create(*TheContext, "while.exit", func);

        // Push break/continue targets
        BreakTargets.push_back(exitBB);
        ContinueTargets.push_back(condBB);

        // Jump to condition
        TheBuilder->CreateBr(condBB);

        // Condition block
        TheBuilder->SetInsertPoint(condBB);
        stmt->condExpr()->accept(this);
        llvm::Value* condVal = ExprValues[stmt->condExpr()];
        TheBuilder->CreateCondBr(condVal, bodyBB, exitBB);

        // Body block
        TheBuilder->SetInsertPoint(bodyBB);
        stmt->body()->accept(this);
        if (!TheBuilder->GetInsertBlock()->getTerminator())
            TheBuilder->CreateBr(condBB);

        // Pop break/continue targets
        BreakTargets.pop_back();
        ContinueTargets.pop_back();

        // Continue generating code in exit block
        TheBuilder->SetInsertPoint(exitBB);
    }

    void IRGenerator::visitContinueStmt(ContinueStatement *stmt) {
        TheBuilder->CreateBr(ContinueTargets.back());
    }

    void IRGenerator::visitReturnStmt(ReturnStatement *stmt) {
        if (stmt->hasReturnExpr()) {
            stmt->returnExpr()->accept(this);
            llvm::Value* retVal = ExprValues[stmt->returnExpr()];
            TheBuilder->CreateRet(retVal);
        } else {
            TheBuilder->CreateRetVoid();
        }
    }

    void IRGenerator::visitBreakStmt(BreakStatement *stmt) {
        TheBuilder->CreateBr(BreakTargets.back());
    }


    void IRGenerator::visitUnaryExpr(UnaryExpr *expr) {
        // Visit the operand (child 0)
        Expr* operand = (Expr*)expr->getChild(0);
        operand->accept(this);
        llvm::Value* val = ExprValues[operand];

        if (expr->opcode() == Expr::Sub) {
            // Unary minus: -val
            ExprValues[expr] = TheBuilder->CreateNeg(val);
        } else if (expr->opcode() == Expr::Not) {
            // Boolean not: !val
            ExprValues[expr] = TheBuilder->CreateNot(val);
        }
    }

    void IRGenerator::visitBinaryExpr(BinaryExpr *expr) {
        Expr* left = (Expr*)expr->getChild(0);
        Expr* right = (Expr*)expr->getChild(1);
        Expr::ExprOpcode op = expr->opcode();

        // Short-circuit AND/OR need special control flow
        if (op == Expr::And) {
            // A && B: if A is false, skip B entirely (result = false)
            left->accept(this);
            llvm::Value* lVal = ExprValues[left];

            llvm::Function* func = TheBuilder->GetInsertBlock()->getParent();
            llvm::BasicBlock* slowBB = llvm::BasicBlock::Create(*TheContext, "and.rhs", func);
            llvm::BasicBlock* outBB = llvm::BasicBlock::Create(*TheContext, "and.out", func);
            llvm::BasicBlock* currentBB = TheBuilder->GetInsertBlock();

            // If A is true, evaluate B; if false, skip to out
            TheBuilder->CreateCondBr(lVal, slowBB, outBB);

            // Evaluate B
            TheBuilder->SetInsertPoint(slowBB);
            right->accept(this);
            llvm::Value* rVal = ExprValues[right];
            // B's evaluation may have changed the current block (e.g. nested &&)
            llvm::BasicBlock* slowEndBB = TheBuilder->GetInsertBlock();
            TheBuilder->CreateBr(outBB);

            // PHI node merges: from currentBB → false(0), from slowBB → B's value
            TheBuilder->SetInsertPoint(outBB);
            llvm::PHINode* phi = TheBuilder->CreatePHI(
                llvm::Type::getInt1Ty(*TheContext), 2);
            phi->addIncoming(llvm::ConstantInt::get(
                llvm::Type::getInt1Ty(*TheContext), 0), currentBB);
            phi->addIncoming(rVal, slowEndBB);
            ExprValues[expr] = phi;
            return;
        }

        if (op == Expr::Or) {
            // A || B: if A is true, skip B entirely (result = true)
            left->accept(this);
            llvm::Value* lVal = ExprValues[left];

            llvm::Function* func = TheBuilder->GetInsertBlock()->getParent();
            llvm::BasicBlock* slowBB = llvm::BasicBlock::Create(*TheContext, "or.rhs", func);
            llvm::BasicBlock* outBB = llvm::BasicBlock::Create(*TheContext, "or.out", func);
            llvm::BasicBlock* currentBB = TheBuilder->GetInsertBlock();

            // If A is true, skip to out; if false, evaluate B
            TheBuilder->CreateCondBr(lVal, outBB, slowBB);

            // Evaluate B
            TheBuilder->SetInsertPoint(slowBB);
            right->accept(this);
            llvm::Value* rVal = ExprValues[right];
            llvm::BasicBlock* slowEndBB = TheBuilder->GetInsertBlock();
            TheBuilder->CreateBr(outBB);

            // PHI node merges: from currentBB → true(1), from slowBB → B's value
            TheBuilder->SetInsertPoint(outBB);
            llvm::PHINode* phi = TheBuilder->CreatePHI(
                llvm::Type::getInt1Ty(*TheContext), 2);
            phi->addIncoming(llvm::ConstantInt::get(
                llvm::Type::getInt1Ty(*TheContext), 1), currentBB);
            phi->addIncoming(rVal, slowEndBB);
            ExprValues[expr] = phi;
            return;
        }

        // All other binary ops: evaluate both sides, then create instruction
        left->accept(this);
        right->accept(this);
        llvm::Value* lVal = ExprValues[left];
        llvm::Value* rVal = ExprValues[right];

        switch (op) {
            case Expr::Add:
                ExprValues[expr] = TheBuilder->CreateAdd(lVal, rVal); break;
            case Expr::Sub:
                ExprValues[expr] = TheBuilder->CreateSub(lVal, rVal); break;
            case Expr::Mul:
                ExprValues[expr] = TheBuilder->CreateMul(lVal, rVal); break;
            case Expr::Div:
                ExprValues[expr] = TheBuilder->CreateSDiv(lVal, rVal); break;
            case Expr::Equal:
                ExprValues[expr] = TheBuilder->CreateICmpEQ(lVal, rVal); break;
            case Expr::NotEqual:
                ExprValues[expr] = TheBuilder->CreateICmpNE(lVal, rVal); break;
            case Expr::Less:
                ExprValues[expr] = TheBuilder->CreateICmpSLT(lVal, rVal); break;
            case Expr::LessEqual:
                ExprValues[expr] = TheBuilder->CreateICmpSLE(lVal, rVal); break;
            case Expr::Greater:
                ExprValues[expr] = TheBuilder->CreateICmpSGT(lVal, rVal); break;
            case Expr::GreaterEqual:
                ExprValues[expr] = TheBuilder->CreateICmpSGE(lVal, rVal); break;
            default:
                break;
        }
    }

    void IRGenerator::visitCallExpr(CallExpr *expr) {
        // Get the function from the module
        std::string funcName = expr->callee()->name();
        llvm::Function* callee = TheModule->getFunction(funcName);

        // Visit each argument expression
        std::vector<llvm::Value*> args;
        for (size_t i = 0; i < expr->numArgs(); i++) {
            expr->arg(i)->accept(this);
            args.push_back(ExprValues[expr->arg(i)]);
        }

        // Create the call
        llvm::Value* callVal = TheBuilder->CreateCall(callee, args);

        // If non-void, store the return value for the parent to use
        if (!callee->getReturnType()->isVoidTy())
            ExprValues[expr] = callVal;
    }

    void IRGenerator::visitVarExpr(VarExpr *expr) {
        // Child 0 is the VarReference
        VarReference* ref = (VarReference*)expr->getChild(0);
        std::string name = ref->identifier()->name();

        // Find the variable's LLVMValue (a pointer) from the symbol table
        VarSymbolTable* table = expr->locateDeclaringTableForVar(name);
        VarSymbolEntry* entry = table->lookup(name);
        llvm::Value* ptr = entry->LLVMValue;

        if (ref->isArray()) {
            // Array indexing: a[i]
            // Visit the index expression to get its value
            ref->indexExpr()->accept(this);
            llvm::Value* index = ExprValues[(Expr*)ref->indexExpr()];

            // GEP (get element pointer) into the array: ptr points to [N x T], we need element pointer
            llvm::Value* zero = llvm::ConstantInt::get(
                llvm::Type::getInt32Ty(*TheContext), 0);
            ptr = TheBuilder->CreateGEP(
                getLLVMType(entry->VarType), ptr, {zero, index});

            // Load the element
            llvm::Type* elemType = getLLVMType(entry->VarType.getIndexedType());
            ExprValues[expr] = TheBuilder->CreateLoad(elemType, ptr);
        } else {
            // Simple variable: load from pointer
            llvm::Type* varType = getLLVMType(entry->VarType);
            ExprValues[expr] = TheBuilder->CreateLoad(varType, ptr);
        }
    }

    void IRGenerator::visitAssignmentExpr(AssignmentExpr *expr) {
        // Child 0 is VarReference (LHS), Child 1 is the value expression (RHS)
        VarReference* ref = (VarReference*)expr->getChild(0);
        Expr* valueExpr = (Expr*)expr->getChild(1);

        // Visit the RHS to compute the value
        valueExpr->accept(this);
        llvm::Value* val = ExprValues[valueExpr];

        // Find the variable's LLVMValue (a pointer)
        std::string name = ref->identifier()->name();
        VarSymbolTable* table = expr->locateDeclaringTableForVar(name);
        VarSymbolEntry* entry = table->lookup(name);
        llvm::Value* ptr = entry->LLVMValue;

        if (ref->isArray()) {
            // Array indexing: a[i] = val
            ref->indexExpr()->accept(this);
            llvm::Value* index = ExprValues[(Expr*)ref->indexExpr()];

            llvm::Value* zero = llvm::ConstantInt::get(
                llvm::Type::getInt32Ty(*TheContext), 0);
            ptr = TheBuilder->CreateGEP(
                getLLVMType(entry->VarType), ptr, {zero, index});
        }

        TheBuilder->CreateStore(val, ptr);

        // Assignment expression itself evaluates to the assigned value
        ExprValues[expr] = val;
    }

    void IRGenerator::visitIntLiteralExpr(IntLiteralExpr *literal) {
        ExprValues[literal] = llvm::ConstantInt::get(
            llvm::Type::getInt32Ty(*TheContext), literal->value(), true);
    }

    void IRGenerator::visitBoolLiteralExpr(BoolLiteralExpr *literal) {
        ExprValues[literal] = llvm::ConstantInt::get(
            llvm::Type::getInt1Ty(*TheContext), literal->value() ? 1 : 0, false);
    }

    void IRGenerator::visitCharLiteralExpr(CharLiteralExpr *literal) {
        ExprValues[literal] = llvm::ConstantInt::get(
            llvm::Type::getInt8Ty(*TheContext), (uint8_t)literal->value(), false);
    }

    void IRGenerator::visitScope(ScopeStatement *stmt) {
        for (size_t i = 0; i < stmt->numChildren(); i++) {
            ASTNode* child = stmt->getChild(i);
            if (child)
                child->accept(this);
            // If a terminator was emitted (return/break/continue), stop — rest is dead code
            if (TheBuilder->GetInsertBlock()->getTerminator())
                break;
        }
    }

}

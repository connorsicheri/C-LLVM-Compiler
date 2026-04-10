//Add necessary headers you want
#include "VerifyAndBuildSymbols.h"
#include "Declarations.h"
#include "Terms.h"
#include "Types.h"
#include "Exprs.h"
#include "Statements.h"
#include "Program.h"
#include <string>
#include <sstream>

namespace minicc {

    void VerifyAndBuildSymbols::visitASTNode(ASTNode *node) {
        //Hint: set root of the node before visiting children
        // Set root of the node before visiting children
        node->setRoot(VisitingProgram);
        // Visit all children
        ASTVisitor::visitASTNode(node);
    }

    void VerifyAndBuildSymbols::visitProgram(Program *prog) {
        // Store the program pointer for later use
        VisitingProgram = prog;
        prog->setRoot(prog);
        
        if (prog->syslibFlag()) {
            // Manually populate the function symbol table with syslib functions
            FuncSymbolTable* funcTable = prog->funcTable();
            // int getint()
            funcTable->insert("getint", Type(Type::Int), std::vector<Type>(), true);
            // void putint(int v)
            funcTable->insert("putint", Type(Type::Void), std::vector<Type>{Type(Type::Int)}, true);
            // void putcharacter(char c)
            funcTable->insert("putcharacter", Type(Type::Void), std::vector<Type>{Type(Type::Char)}, true);
            // void putnewline()
            funcTable->insert("putnewline", Type(Type::Void), std::vector<Type>(), true);
        }
        
        // Visit all children (declarations)
        ASTVisitor::visitProgram(prog);
    }

    void VerifyAndBuildSymbols::visitVarDecl(VarDeclaration *decl) {
        //Hint: Check that same variable cannot be declared twice in the same scope
        // First visit children to process any expressions (e.g., array sizes)
        ASTVisitor::visitVarDecl(decl);
        
        // Get the enclosing scope's variable table
        ASTNode* scope = decl->getParentScope();
        VarSymbolTable* table = scope->scopeVarTable();
        
        // Get the declared type
        Type declType(decl->declType());
        
        // Process each variable reference in the declaration
        for (size_t i = 0; i < decl->numVarReferences(); i++) {
            VarReference* ref = decl->varReference(i);
            std::string name = ref->identifier()->name();
            
            // Build the full type (including array bounds if applicable)
            Type varType = declType;
            if (ref->isArray()) {
                // Get array size from the index expression (it's an IntLiteralExpr)
                IntLiteralExpr* sizeExpr = (IntLiteralExpr*)ref->indexExpr();
                varType.setIsArray(sizeExpr->value());
            }
            
            // Try to insert into the symbol table
            if (!table->insert(name, varType)) {
                // Variable already exists in this scope
                Errors.push_back(ErrorMessage(
                    "Redefinition of variable/parameter \"" + name + "\" in the same scope!",
                    decl->srcLoc()));
            }
        }
    }

    void VerifyAndBuildSymbols::visitFuncDecl(FuncDeclaration *func) {
        //Hint: Check return type of the function does not match with each other
        //      Check number of parameters should match with each other
        //      Check each parameter type should match with each other
        //      Check there should be only one definition of the function
        //      Check parameters cannot have the same name
        //      Check the last statement a function body must be return if the return type is not void
        std::string funcName = func->name();
        Type returnType = func->returnType();
        FuncSymbolTable* funcTable = VisitingProgram->funcTable();
        
        // Build parameter type list
        std::vector<Type> paramTypes;
        for (size_t i = 0; i < func->numParameters(); i++) {
            paramTypes.push_back(func->parameter(i)->type());
        }
        
        // Check if function already exists
        FuncSymbolEntry* existing = funcTable->lookup(funcName);
        if (existing) {
            // Check return type matches
            if (existing->ReturnType != returnType) {
                Errors.push_back(ErrorMessage(
                    "Definition of function \"" + funcName + "()\" with different return type!",
                    func->srcLoc()));
            }
            // Check parameter count matches
            else if (existing->ParameterTypes.size() != paramTypes.size()) {
                Errors.push_back(ErrorMessage(
                    "Definition of function \"" + funcName + "()\" with different number of parameters!",
                    func->srcLoc()));
            }
            // Check each parameter type matches
            else {
                for (size_t i = 0; i < paramTypes.size(); i++) {
                    if (existing->ParameterTypes[i] != paramTypes[i]) {
                        Errors.push_back(ErrorMessage(
                            "Definition of function \"" + funcName + "()\" with different parameter type at position " + std::to_string(i + 1) + "!",
                            func->srcLoc()));
                        break;
                    }
                }
            }
            // Check for redefinition (multiple bodies)
            if (func->hasBody() && existing->HasBody) {
                Errors.push_back(ErrorMessage(
                    "Redefinition of function \"" + funcName + "()\"!",
                    func->srcLoc()));
            }
            // Update HasBody if this is a definition
            if (func->hasBody()) {
                funcTable->setHasBody(funcName, true);
            }
        } else {
            // New function - add to symbol table
            funcTable->insert(funcName, returnType, paramTypes, func->hasBody());
        }
        
        // If function has a body, add parameters to the body's scope
        if (func->hasBody()) {
            VarSymbolTable* bodyTable = func->body()->scopeVarTable();
            
            // Check for duplicate parameter names and add to scope
            for (size_t i = 0; i < func->numParameters(); i++) {
                Parameter* param = func->parameter(i);
                std::string paramName = param->name();
                
                if (!bodyTable->insert(paramName, param->type())) {
                    Errors.push_back(ErrorMessage(
                        "Redefinition of variable/parameter \"" + paramName + "\" in the same scope!",
                        param->srcLoc()));
                }
            }
            
            // Check that non-void function ends with return
            if (!returnType.isVoid()) {
                ScopeStatement* body = func->body();
                size_t numChildren = body->numChildren();
                if (numChildren == 0) {
                    Errors.push_back(ErrorMessage(
                        "The function \"" + funcName + "()\" need to return a value at its end!",
                        func->srcLoc()));
                } else {
                    ASTNode* lastStmt = body->getChild(numChildren - 1);
                    if (!lastStmt->isReturn()) {
                        Errors.push_back(ErrorMessage(
                            "The function \"" + funcName + "()\" need to return a value at its end!",
                            func->srcLoc()));
                    }
                }
            }
        }
        
        // Visit children (parameters and body)
        ASTVisitor::visitFuncDecl(func);
    }

    void VerifyAndBuildSymbols::visitIfStmt(IfStatement *stmt) {
        //Hint: Check the conditional expression must have bool type
        // Visit children first to get expression types
        ASTVisitor::visitIfStmt(stmt);
        
        // Check the conditional expression must have bool type
        Type condType = stmt->condExpr()->exprType();
        if (!condType.isBool()) {
            Errors.push_back(ErrorMessage(
                "Conditional expression in if statement has non-bool type!",
                stmt->srcLoc()));
        }
    }

    void VerifyAndBuildSymbols::visitForStmt(ForStatement *stmt) {
        //Hint: Check the second expression in for must be either null or bool type
        // Visit children first to get expression types
        ASTVisitor::visitForStmt(stmt);
        
        // Check the conditional expression must have bool type (if present)
        Expr* condExpr = stmt->condExpr();
        if (condExpr != nullptr) {
            Type condType = condExpr->exprType();
            if (!condType.isBool()) {
                Errors.push_back(ErrorMessage(
                    "Conditional expression in for statement has non-bool type!",
                    stmt->srcLoc()));
            }
        }
    }

    void VerifyAndBuildSymbols::visitWhileStmt(WhileStatement *stmt) {
        //Hint: Check the conditional expression must have bool type
        // Visit children first to get expression types
        ASTVisitor::visitWhileStmt(stmt);
        
        // Check the conditional expression must have bool type
        Type condType = stmt->condExpr()->exprType();
        if (!condType.isBool()) {
            Errors.push_back(ErrorMessage(
                "Conditional expression in while statement has non-bool type!",
                stmt->srcLoc()));
        }
    }

    void VerifyAndBuildSymbols::visitContinueStmt(ContinueStatement *stmt) {
        //Hint: Check Continue statement must appear inside a loop (for/while)
        // Check Continue statement must appear inside a loop (for/while)
        if (stmt->getParentForStatement() == nullptr && stmt->getParentWhileStatement() == nullptr) {
            Errors.push_back(ErrorMessage(
                "Continue statement must appear inside a for/while statement!",
                stmt->srcLoc()));
        }
        
        ASTVisitor::visitContinueStmt(stmt);
    }

    void VerifyAndBuildSymbols::visitReturnStmt(ReturnStatement *stmt) {
        //Hint: Check void function must have no expression to return
        //      Check Non-Void function must have an expression to return
        //      Check the return type and the returned expression type must match
        // Visit children first to get expression types
        ASTVisitor::visitReturnStmt(stmt);
        
        // Get the enclosing function
        FuncDeclaration* func = stmt->getParentFunction();
        Type returnType = func->returnType();
        
        if (returnType.isVoid()) {
            // Void function must have no expression to return
            if (stmt->hasReturnExpr()) {
                Errors.push_back(ErrorMessage(
                    "Function has void return type, but the return statement has a returned expression!",
                    stmt->srcLoc()));
            }
        } else {
            // Non-void function must have an expression to return
            if (!stmt->hasReturnExpr()) {
                Errors.push_back(ErrorMessage(
                    "Function has non-void return type, but the return statement has no returned expression!",
                    stmt->srcLoc()));
            } else {
                // Check the return type matches
                Type exprType = stmt->returnExpr()->exprType();
                if (exprType != returnType) {
                    Errors.push_back(ErrorMessage(
                        "Function has return type \"" + returnType.toString() + 
                        "\", but the returned expression has type \"" + exprType.toString() + "\"!",
                        stmt->srcLoc()));
                }
            }
        }
    }

    void VerifyAndBuildSymbols::visitBreakStmt(BreakStatement *stmt) {
        //Hint: Check Break statement must appear inside a loop (for/while)
        // Check Break statement must appear inside a loop (for/while)
        if (stmt->getParentForStatement() == nullptr && stmt->getParentWhileStatement() == nullptr) {
            Errors.push_back(ErrorMessage(
                "Break statement must appear inside a for/while statement!",
                stmt->srcLoc()));
        }
        
        ASTVisitor::visitBreakStmt(stmt);
    }

    void VerifyAndBuildSymbols::visitUnaryExpr(UnaryExpr *expr) {
        //Hint: Check Negate opcode must have int operand!
        //      Check Not opcode must have bool operand
        // Visit children first to get expression types
        ASTVisitor::visitUnaryExpr(expr);
        
        Expr* operand = (Expr*)expr->getChild(0);
        Type operandType = operand->exprType();
        Expr::ExprOpcode opcode = expr->opcode();
        
        if (opcode == Expr::Sub) {
            // Negate "-" must have int operand
            if (!operandType.isInt()) {
                Errors.push_back(ErrorMessage(
                    "Negate \"-\" opcode must have int operand!",
                    expr->srcLoc()));
            }
            // Unary ops inherit type of argument
            expr->setExprType(operandType);
        } else if (opcode == Expr::Not) {
            // Not "!" must have bool operand
            if (!operandType.isBool()) {
                Errors.push_back(ErrorMessage(
                    "Not \"!\" opcode must have bool operand!",
                    expr->srcLoc()));
            }
            // Unary ops inherit type of argument
            expr->setExprType(operandType);
        }
    }

    void VerifyAndBuildSymbols::visitBinaryExpr(BinaryExpr *expr) {
        //Hint: Check that for logical opcode, both operand need to be bool
        //      Check that for equal and not equal opcode, both operand need to be the same primitive types
        //      Check that for arithmetic and other comparison operand, both operand need to be int
        // Visit children first to get expression types
        ASTVisitor::visitBinaryExpr(expr);
        
        Expr* left = (Expr*)expr->getChild(0);
        Expr* right = (Expr*)expr->getChild(1);
        Type leftType = left->exprType();
        Type rightType = right->exprType();
        Expr::ExprOpcode opcode = expr->opcode();
        
        if (opcode == Expr::And || opcode == Expr::Or) {
            // Logical operators need bool operands
            if (!leftType.isBool() || !rightType.isBool()) {
                Errors.push_back(ErrorMessage(
                    "\"&&\"/\"||\" opcode must have bool operand!",
                    expr->srcLoc()));
            }
            // Binary ops with &&/|| inherit type of first argument
            expr->setExprType(leftType);
        } else if (opcode == Expr::Equal || opcode == Expr::NotEqual) {
            // Equality operators need same primitive type
            if (leftType != rightType || leftType.arrayBound() > 0) {
                Errors.push_back(ErrorMessage(
                    "\"==\"/\"!=\" opcode must have same primitive type operand!",
                    expr->srcLoc()));
            }
            expr->setExprType(Type(Type::Bool));
        } else if (opcode == Expr::Less) {
            if (!leftType.isInt() || !rightType.isInt()) {
                Errors.push_back(ErrorMessage(
                    "\"<\" opcode must have int type operand!",
                    expr->srcLoc()));
            }
            expr->setExprType(Type(Type::Bool));
        } else if (opcode == Expr::Greater) {
            if (!leftType.isInt() || !rightType.isInt()) {
                Errors.push_back(ErrorMessage(
                    "\">\" opcode must have int type operand!",
                    expr->srcLoc()));
            }
            expr->setExprType(Type(Type::Bool));
        } else if (opcode == Expr::LessEqual) {
            if (!leftType.isInt() || !rightType.isInt()) {
                Errors.push_back(ErrorMessage(
                    "\"<=\" opcode must have int type operand!",
                    expr->srcLoc()));
            }
            expr->setExprType(Type(Type::Bool));
        } else if (opcode == Expr::GreaterEqual) {
            if (!leftType.isInt() || !rightType.isInt()) {
                Errors.push_back(ErrorMessage(
                    "\">=\" opcode must have int type operand!",
                    expr->srcLoc()));
            }
            expr->setExprType(Type(Type::Bool));
        } else if (opcode == Expr::Add) {
            if (!leftType.isInt() || !rightType.isInt()) {
                Errors.push_back(ErrorMessage(
                    "\"+\" opcode must have int type operand!",
                    expr->srcLoc()));
            }
            // Arithmetic ops inherit type of first argument
            expr->setExprType(leftType);
        } else if (opcode == Expr::Sub) {
            if (!leftType.isInt() || !rightType.isInt()) {
                Errors.push_back(ErrorMessage(
                    "\"-\" opcode must have int type operand!",
                    expr->srcLoc()));
            }
            // Arithmetic ops inherit type of first argument
            expr->setExprType(leftType);
        } else if (opcode == Expr::Mul) {
            if (!leftType.isInt() || !rightType.isInt()) {
                Errors.push_back(ErrorMessage(
                    "\"*\" opcode must have int type operand!",
                    expr->srcLoc()));
            }
            // Arithmetic ops inherit type of first argument
            expr->setExprType(leftType);
        } else if (opcode == Expr::Div) {
            if (!leftType.isInt() || !rightType.isInt()) {
                Errors.push_back(ErrorMessage(
                    "\"/\" opcode must have int type operand!",
                    expr->srcLoc()));
            }
            // Arithmetic ops inherit type of first argument
            expr->setExprType(leftType);
        }
    }

    void VerifyAndBuildSymbols::visitCallExpr(CallExpr *expr) {
        //Hint: Check Call undeclared function
        //      Check the number of arguments must match the number of parameters
        //      Check the type of each parameter must match the argument
        // Visit children first to get expression types
        ASTVisitor::visitCallExpr(expr);
        
        std::string funcName = expr->callee()->name();
        FuncSymbolTable* funcTable = expr->root()->funcTable();
        FuncSymbolEntry* funcEntry = funcTable->lookup(funcName);
        
        if (funcEntry == nullptr) {
            // Function not declared
            Errors.push_back(ErrorMessage(
                "Function " + funcName + "() is not declared before use!",
                expr->srcLoc()));
            expr->setExprType(Type(Type::Void));
        } else {
            // Check argument count
            size_t numArgs = expr->numArgs();
            size_t numParams = funcEntry->ParameterTypes.size();
            
            if (numArgs != numParams) {
                Errors.push_back(ErrorMessage(
                    "Function " + funcName + "() is declared with " + std::to_string(numParams) +
                    " parameters but called with " + std::to_string(numArgs) + " arguments!",
                    expr->srcLoc()));
            } else {
                // Check each argument type
                for (size_t i = 0; i < numArgs; i++) {
                    Type argType = expr->arg(i)->exprType();
                    Type paramType = funcEntry->ParameterTypes[i];
                    if (argType != paramType) {
                        Errors.push_back(ErrorMessage(
                            "Function " + funcName + "() does not match the type of the call argument at position " +
                            std::to_string(i + 1) + "!",
                            expr->srcLoc()));
                    }
                }
            }
            expr->setExprType(funcEntry->ReturnType);
        }
    }

    static Type verifyVarReference(std::vector<ErrorMessage> & Errors, Expr* expr, VarReference *ref) {
        //Hint: Check the vairable which is reference must be declared before
        //      Check index expression must have int type
        //      Check variable must be declared as an array for indexing
        //return ref Type
        std::string varName = ref->identifier()->name();
        
        // Check variable is declared
        VarSymbolTable* table = expr->locateDeclaringTableForVar(varName);
        if (table == nullptr) {
            Errors.push_back(ErrorMessage(
                "Variable " + varName + " is not declared before use!",
                expr->srcLoc()));
            return Type(Type::Void);
        }
        
        VarSymbolEntry* entry = table->lookup(varName);
        Type varType = entry->VarType;
        
        // If indexing
        if (ref->isArray()) {
            // Check index expression is int
            Expr* indexExpr = ref->indexExpr();
            if (!indexExpr->exprType().isInt()) {
                Errors.push_back(ErrorMessage(
                    "Array index expressions must have int operand!",
                    expr->srcLoc()));
            }
            // Check variable is actually an array
            if (varType.arrayBound() == 0) {
                Errors.push_back(ErrorMessage(
                    "Indexing an non-array variable!",
                    expr->srcLoc()));
                // If indexing non-array, give same type as variable
                return varType;
            }
            // Return the indexed type (element type)
            return varType.getIndexedType();
        }
        
        return varType;
    }

    void VerifyAndBuildSymbols::visitVarExpr(VarExpr *expr) {
        //Hint: invoke verifyVarReference to verify
        // Visit children first
        ASTVisitor::visitVarExpr(expr);
        
        // Get the VarReference (first child)
        VarReference* ref = (VarReference*)expr->getChild(0);
        Type varType = verifyVarReference(Errors, expr, ref);
        expr->setExprType(varType);
    }

    void VerifyAndBuildSymbols::visitAssignmentExpr(AssignmentExpr *expr) {
        //Hint: invoke verifyVarReference to verify
        //      Also, check var and assigned expression must have the same type
        // Visit children first
        ASTVisitor::visitAssignmentExpr(expr);
        
        // Get the VarReference (first child) and assigned expression (second child)
        VarReference* ref = (VarReference*)expr->getChild(0);
        Expr* assignedExpr = (Expr*)expr->getChild(1);
        
        Type varType = verifyVarReference(Errors, expr, ref);
        Type assignedType = assignedExpr->exprType();
        
        // Check types match
        if (varType != assignedType) {
            Errors.push_back(ErrorMessage(
                "Variable and the assignment expression do not have the same type!",
                expr->srcLoc()));
        }
        
        expr->setExprType(varType);
    }

    void VerifyAndBuildSymbols::visitIntLiteralExpr(IntLiteralExpr *literal) {
        //Hint: Check Integer literal must be inside the range of int
        // If fromString detected overflow, type will be Void - report error and fix to Int
        if (literal->exprType().isVoid()) {
            Errors.push_back(ErrorMessage(
                "Integer literal must be inside the range of int!",
                literal->srcLoc()));
            // Per ambiguity rules: overflowed literals should still have Int type
            literal->setExprType(Type(Type::Int));
        }
        ASTVisitor::visitIntLiteralExpr(literal);
    }

    void VerifyAndBuildSymbols::visitBoolLiteralExpr(BoolLiteralExpr *literal) {
        // Type is already set to Bool in the constructor
        ASTVisitor::visitBoolLiteralExpr(literal);
    }

    void VerifyAndBuildSymbols::visitCharLiteralExpr(CharLiteralExpr *literal) {
        // Type is already set to Char in the constructor
        ASTVisitor::visitCharLiteralExpr(literal);
    }

    //print collected error messages
    std::string VerifyAndBuildSymbols::genErrorMessages() {
        std::stringbuf buf;
        std::ostream os(&buf);

        for (size_t i = 0; i < Errors.size(); i++) {
            os << Errors[i].Msg << " (" << Errors[i].SrcLoc.Line << ":" << Errors[i].SrcLoc.Row << ")\n";
        }

        return buf.str();
    }

}

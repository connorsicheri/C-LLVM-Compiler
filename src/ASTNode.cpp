#include <typeinfo>

#include "ASTNode.h"
#include "Program.h"
#include "Statements.h"
#include "Declarations.h"

namespace minicc {
    bool ASTNode::isScope() {
        return typeid(*this) == typeid(Program) || typeid(*this) == typeid(ScopeStatement);
    }

    bool ASTNode::isForStatement() {
        return typeid(*this) == typeid(ForStatement);
    }

    bool ASTNode::isWhileStatement() {
        return typeid(*this) == typeid(WhileStatement);
    }

    bool ASTNode::isContinueStatement() {
        return typeid(*this) == typeid(ContinueStatement);
    }

    bool ASTNode::isFuncDecl() {
        return typeid(*this) == typeid(FuncDeclaration);
    }

    bool ASTNode::isProgram() {
        return typeid(*this) == typeid(Program);
    }

    bool ASTNode::isReturn() {
        return typeid(*this) == typeid(ReturnStatement);
    }

    VarSymbolTable* ASTNode::locateDeclaringTableForVar(const std::string &name) {
        // Start from the first enclosing scope
        ASTNode* scope = getParentScope();
        
        // Walk up the scope chain
        while (scope != nullptr) {
            VarSymbolTable* table = scope->scopeVarTable();
            
            // Check if this scope's table has the variable
            if (table != nullptr && table->exists(name)) {
                return table;
            }
            
            // Move to the parent scope
            scope = scope->getParentScope();
        }
        
        // Variable not found in any scope
        return nullptr;
    }
}

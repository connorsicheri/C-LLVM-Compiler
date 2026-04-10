#ifndef MINICC_SYMBOLTABLE_H
#define MINICC_SYMBOLTABLE_H

//add more header files if your want
//You may need assert function
#include <cassert>
#include "Types.h"
#include <map>

namespace llvm {
    class Value;
}

namespace minicc {


    struct VarSymbolEntry {
        Type VarType;
        llvm::Value *LLVMValue;

        explicit VarSymbolEntry(Type varType) : VarType(varType), LLVMValue(nullptr) { }
    };

    class VarSymbolTable {

        std::map<std::string, VarSymbolEntry> Table;

    public:
        // Check if a variable exists in this table
        bool exists(const std::string &name) const {
            return Table.find(name) != Table.end();
        }

        // Insert a new variable into the table
        // Returns false if variable already exists
        bool insert(const std::string &name, Type varType) {
            if (exists(name))
                return false;
            Table.emplace(name, VarSymbolEntry(varType));
            return true;
        }

        // Lookup a variable by name
        // Returns nullptr if not found
        VarSymbolEntry* lookup(const std::string &name) {
            auto it = Table.find(name);
            if (it == Table.end())
                return nullptr;
            return &(it->second);
        }

        // Const version of lookup
        const VarSymbolEntry* lookup(const std::string &name) const {
            auto it = Table.find(name);
            if (it == Table.end())
                return nullptr;
            return &(it->second);
        }
    };

    struct FuncSymbolEntry {
        Type ReturnType;
        std::vector<Type> ParameterTypes;
        bool HasBody;

        FuncSymbolEntry(Type retType, const std::vector<Type> &paraTypes, bool hasBody) : ReturnType(retType), ParameterTypes(paraTypes), HasBody(hasBody) { }
    };

    class FuncSymbolTable {
        std::map<std::string, FuncSymbolEntry> Table;
    public:
        // Check if a function exists in this table
        bool exists(const std::string &name) const {
            return Table.find(name) != Table.end();
        }

        // Insert a new function into the table
        // Returns false if function already exists
        bool insert(const std::string &name, Type retType, const std::vector<Type> &paramTypes, bool hasBody) {
            if (exists(name))
                return false;
            Table.emplace(name, FuncSymbolEntry(retType, paramTypes, hasBody));
            return true;
        }

        // Lookup a function by name
        // Returns nullptr if not found
        FuncSymbolEntry* lookup(const std::string &name) {
            auto it = Table.find(name);
            if (it == Table.end())
                return nullptr;
            return &(it->second);
        }

        // Const version of lookup
        const FuncSymbolEntry* lookup(const std::string &name) const {
            auto it = Table.find(name);
            if (it == Table.end())
                return nullptr;
            return &(it->second);
        }

        // Update HasBody flag for an existing function
        void setHasBody(const std::string &name, bool hasBody) {
            auto entry = lookup(name);
            if (entry)
                entry->HasBody = hasBody;
        }
    };
}

#endif //MINICC_SYMBOLTABLE_H

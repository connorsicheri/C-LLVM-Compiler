# MiniC to LLVM IR Compiler

A compiler for **MiniC** (a subset of C99) that generates LLVM IR bitcode. Built with ANTLR4 for parsing and LLVM 15 for code generation.

## Pipeline

1. **Parsing** — ANTLR4 grammar (`MiniC.g4`) parses source into a parse tree
2. **AST Construction** — Embedded actions build a typed Abstract Syntax Tree
3. **Semantic Analysis** — Symbol table construction and type/scope verification (`VerifyAndBuildSymbols`)
4. **IR Generation** — LLVM IR emission from the AST (`IRGenerator`)
5. **Optimization** — Custom LLVM function pass with inlining, constant folding, algebraic simplification, CSE, DCE, and LICM (`Alloca2Reg.cpp`)
6. **Output** — LLVM bitcode file (default `output.bc`)

## Building

### Prerequisites

- CMake 3.14+
- LLVM 15
- ANTLR4 C++ runtime (4.11.1)
- Java (for ANTLR4 grammar compilation)

### Build

```bash
mkdir build && cd build
cmake ..
make
```

## Usage

```bash
# Compile a MiniC source file to LLVM bitcode
./build/src/minicc input.c -o output.bc

# Print the AST
./build/src/minicc input.c --print-ast

# Print the generated LLVM IR
./build/src/minicc input.c --print-ir

# Check the ANTLR parse tree
./build/src/minicc input.c --check-parse-tree
```

### Running compiled programs

```bash
# Compile bitcode to native executable (link with minicio runtime)
llc-15 output.bc -o output.s
clang output.s minicio/minicio.c -o program
./program
```

### Running optimizations

```bash
opt-15 -O0 -load build/src/liballoca2reg.so output.bc -o output_opt.bc -enable-new-pm=0
```

## Project Structure

```
├── grammars/         # ANTLR4 grammar (MiniC.g4)
├── src/              # Compiler source (AST, semantic analysis, IR generation, optimizations)
├── minicio/          # Small I/O runtime library (getint, putint, putcharacter, putnewline)
├── sample/           # Example MiniC programs
├── cmake/            # CMake modules for ANTLR4
└── thirdparty/       # ANTLR4 jar
```

## The MiniC Language

MiniC is a subset of C99. A valid MiniC program is always a valid C99 program. Features include:

- Integer, boolean, and character types
- Arrays
- Functions with pass-by-value parameters
- Control flow: `if`/`else`, `for`, `while`, `break`, `continue`, `return`
- Standard arithmetic and logical operators
- I/O via `getint()`, `putint()`, `putcharacter()`, `putnewline()`

See `language.txt` for the full grammar specification.

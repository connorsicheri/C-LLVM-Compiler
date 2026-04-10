# MiniC to LLVM IR Compiler

A from-scratch compiler that takes **MiniC** (a strict subset of C99) and produces LLVM IR bitcode. The front end uses ANTLR4 to parse source into a concrete syntax tree, builds a typed AST with embedded grammar actions, runs semantic verification (type checking, scope resolution, symbol table construction), and then lowers the AST to LLVM IR. A custom LLVM `FunctionPass` provides an optimization pipeline — function inlining, constant folding, algebraic simplification, local CSE, DCE, and loop-invariant code motion — all driven by a fixpoint loop so each pass can feed opportunities to the others.

## Tested Versions

This compiler has been built and tested with the following toolchain:

| Dependency | Version |
|---|---|
| **LLVM** | 15.0.7 |
| **ANTLR4 JAR** (grammar tool) | 4.11.1 (`antlr-4.11.1-complete.jar`) |
| **ANTLR4 C++ Runtime** | 4.11.1 |
| **Java** (required by ANTLR4 tool) | 11.0.29 |
| **GCC** (C / C++) | 13.3.0 |
| **CMake** | 3.14+ |
| **C++ Standard** | C++17 |

> Other ANTLR 4.x releases may work but have not been verified. The bundled jar in `thirdparty/` is 4.11.1 and the CMake config looks for `antlr4-runtime 4.11.1`.

## Building

```bash
mkdir build && cd build
cmake ..
make
```

The build produces three targets:
- `minicc` — the compiler executable
- `liballoca2reg.so` — the optimization pass (loaded by `opt`)
- `libminicio.a` — small I/O runtime linked into compiled programs

## Usage

```bash
# Compile MiniC source to LLVM bitcode
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
llc-15 output.bc -o output.s
clang output.s minicio/minicio.c -o program
./program
```

### Running the optimization pass

```bash
opt-15 -O0 -load build/src/liballoca2reg.so output.bc -o output_opt.bc -enable-new-pm=0
```

## Project Structure

```
├── grammars/         # ANTLR4 grammar (MiniC.g4)
├── src/              # Compiler source — AST, semantic analysis, IR generation, optimizations
├── minicio/          # Runtime I/O library (getint, putint, putcharacter, putnewline)
├── sample/           # Example MiniC programs (factorial, fibonacci, n-queens)
├── cmake/            # CMake module for ANTLR4 C++ integration
└── thirdparty/       # Bundled ANTLR4 jar
```

## The MiniC Language

MiniC is a strict subset of C99 — every valid MiniC program is a valid C99 program with identical semantics. It supports:

- `int`, `bool`, and `char` types
- One-dimensional arrays
- Functions with pass-by-value parameters
- Control flow: `if`/`else`, `for`, `while`, `break`, `continue`, `return`
- Arithmetic, relational, and logical operators with C-style precedence
- I/O via `getint()`, `putint()`, `putcharacter()`, `putnewline()`

See `language.txt` for the full reference grammar.

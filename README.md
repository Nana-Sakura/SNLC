# SNLC — Small Nested Language Compiler

A compiler for SNL (Small Nested Language), written from scratch in C++17, targeting MIPS32 assembly. It implements a complete compilation pipeline from lexical analysis through register allocation.

## Features

- **Lexical analysis**: hand-written lexer with line/column-aware error reporting
- **Syntax analysis**: two interchangeable parsers
  - Recursive-descent parser (default)
  - Table-driven LL(1) parser (`--ll1`)
- **Semantic analysis**: scope/symbol table management, type checking, and support for SNL language features such as arrays, records, procedures, and VAR (by-reference) parameters
- **Optimization** (AST-level): constant folding, algebraic simplification, loop-invariant code motion (LICM)
- **Code generation**: two paths
  - Direct code generation (`--no-regalloc`)
  - TAC (three-address code) + graph-coloring register allocation (default, `--opt-regalloc`), based on the Chaitin-Briggs algorithm, mapping virtual registers onto `$t0`–`$t7` with spill handling
- **Target**: MIPS32 assembly, runnable/verifiable via MARS or the bundled Python simulator

## Pipeline

```
Source (.snl)
   │
   ▼
Lexer                       ──▶ --lex-only  to inspect the token stream
   │
   ▼
Parser / LL1Parser          ──▶ --parse-only  to inspect the AST
   │
   ▼
SemanticAnalyzer            ──▶ --sem-only  checking only, no codegen
   │
   ▼
AST-level Optimizer            constant folding / algebraic simplification / LICM
   │
   ▼
CodeGen / TACCodeGen + RegAlloc
   │
   ▼
MIPS32 assembly output (.asm)
```

## Build

```bash
make            # debug build (with ASan/UBSan)
make release    # release build (-O2, no sanitizers)
```

## Usage

```bash
./snlc [options] <source.snl>

  -o <outfile>    output file name (default: out.asm)
  --lex-only      lex only, print tokens
  --parse-only    lex + parse only, print the AST
  --sem-only      lex + parse + semantic analysis, no codegen
  --opt-regalloc  enable TAC + register allocation (default)
  --no-regalloc   disable register allocation, use direct codegen
  --ll1           use the table-driven LL(1) parser (default: recursive descent)
  -v              verbose mode (print info for each stage)
```

Examples:

```bash
./snlc tests/simple.snl -o out.asm
./snlc --opt-regalloc --ll1 -v tests/fibsum.snl -o out.asm
```

## Testing

```bash
make test          # regression tests: compile + run via the built-in MIPS simulator, diff results
make compiletest    # CompileTest.sh: batch-compile every case under tests/
make outputtest      # OutputTest.sh: run the assembly through MARS and diff output
```

`make test` covers basic expressions, arrays, records, procedure calls (including recursive fib), stack balance, right-associative operators, record/array aliasing, variadic parameter passing, LICM, and undefined-variable error detection — and runs the same suite again through the graph-coloring register allocation path (`--opt-regalloc`).

## Layout

```
include/       headers for each stage (lexer / parser / ll1_parser / semantic /
               optimizer / codegen / taccodegen / regalloc / ast / token)
src/
  lexer/       lexer
  parser/      recursive-descent parser, table-driven LL(1) parser, AST definitions
  semantic/    semantic analysis (symbol table, scoping, type checking)
  optimizer/   AST-level optimizations + TAC graph-coloring register allocator
  codegen/     direct code generation + two-pass TAC-based code generation
  main.cpp     CLI entry point
```

## Background

This project was built from scratch during a compiler-construction course, implementing a full SNL-to-MIPS32 pipeline — including a custom-designed TAC intermediate representation and a graph-coloring register allocator based on the Chaitin-Briggs algorithm.

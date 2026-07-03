# SNLC — Small Nested Language Compiler

一个从零手写的 SNL（Small Nested Language）编译器，用 C++17 实现，将 SNL 源码编译为 MIPS32 汇编。涵盖从词法分析到寄存器分配的完整编译流程。

## 特性

- **词法分析**：手写 Lexer，支持行列号定位的错误提示
- **语法分析**：两套可切换的解析器
  - 递归下降解析器（默认）
  - 表驱动 LL(1) 解析器（`--ll1`）
- **语义分析**：作用域/符号表管理，类型检查，数组、记录（record）、过程（procedure）、变参（VAR 参数）等 SNL 语言特性
- **优化**（AST 级）：常量折叠、代数化简、循环不变式外提（LICM）
- **代码生成**：两条路径
  - 直接生成路径（`--no-regalloc`）
  - TAC（三地址码）+ 图着色寄存器分配路径（默认，`--opt-regalloc`），基于 Chaitin-Briggs 算法，将虚拟寄存器分配到 `$t0`–`$t7`，处理 spill
- **目标代码**：MIPS32 汇编，可用 MARS 或仓库自带的 Python 模拟器运行验证

## 编译流程

```
源文件(.snl)
   │
   ▼
词法分析 (Lexer)          ──▶ --lex-only 查看 Token 流
   │
   ▼
语法分析 (Parser / LL1Parser) ──▶ --parse-only 查看 AST
   │
   ▼
语义分析 (SemanticAnalyzer)  ──▶ --sem-only 仅做检查
   │
   ▼
AST 级优化 (Optimizer)       常量折叠 / 代数化简 / LICM
   │
   ▼
代码生成 (CodeGen / TACCodeGen + RegAlloc)
   │
   ▼
MIPS32 汇编输出 (.asm)
```

## 构建

```bash
make            # 调试构建（开启 ASan/UBSan）
make release    # 发布构建（-O2，无 sanitizer）
```

## 使用

```bash
./snlc [选项] <源文件.snl>

  -o <输出文件>   指定输出文件名（默认 out.asm）
  --lex-only      只做词法分析并打印 Token
  --parse-only    只做词法+语法分析并打印 AST
  --sem-only      词法+语法+语义分析（不生成代码）
  --opt-regalloc  开启 TAC + 寄存器分配（默认开启）
  --no-regalloc   关闭寄存器分配，使用原始代码生成
  --ll1           使用表驱动 LL(1) 解析器（默认递归下降）
  -v              详细模式（打印各阶段信息）
```

示例：

```bash
./snlc tests/simple.snl -o out.asm
./snlc --opt-regalloc --ll1 -v tests/fibsum.snl -o out.asm
```

## 测试

```bash
make test          # 回归测试：编译 + 用内置 MIPS 模拟器跑结果比对
make compiletest    # CompileTest.sh：批量编译 tests/ 下所有用例
make outputtest      # OutputTest.sh：借助 MARS 运行汇编并比对输出
```

`make test` 覆盖了基础表达式、数组、记录、过程调用（含递归 fib）、栈平衡、右结合运算、记录数组别名、变参传递、LICM 外提，以及未定义变量的错误检测；并对图着色寄存器分配路径（`--opt-regalloc`）单独跑了一遍同样的用例。

## 目录结构

```
include/       各阶段的头文件（lexer / parser / ll1_parser / semantic /
               optimizer / codegen / taccodegen / regalloc / ast / token）
src/
  lexer/       词法分析器
  parser/      递归下降解析器、表驱动 LL(1) 解析器、AST 定义
  semantic/    语义分析（符号表、作用域、类型检查）
  optimizer/   AST 级优化 + TAC 图着色寄存器分配
  codegen/     直接代码生成 + 基于 TAC 的两阶段代码生成
  main.cpp     命令行入口
```

## 背景

这是在学习编译原理课程期间实现的项目，从零构造了 SNL 到 MIPS32 的完整编译链路，包括自己设计的 TAC 中间表示和基于 Chaitin-Briggs 算法的图着色寄存器分配器。

#include <codegen.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <lexer.h>
#include <ll1_parser.h> // ← 新增
#include <optimizer.h>
#include <parser.h>
#include <semantic.h>
#include <sstream>
#include <string>
#include <taccodegen.h>

static void
printUsage(const char* prog)
{
  std::cerr << "用法：" << prog << " [选项] <源文件.snl>\n"
            << "  -o <输出文件>  指定输出文件名（默认 out.asm）\n"
            << "  --lex-only     只做词法分析并打印 Token\n"
            << "  --parse-only   只做词法+语法分析并打印 AST\n"
            << "  --sem-only     词法+语法+语义分析（不生成代码）\n"
            << "  --opt-regalloc 开启 TAC + 寄存器分配（默认开启）\n"
            << "  --no-regalloc  关闭寄存器分配，使用原始代码生成\n"
            << "  --ll1          使用表驱动 LL(1) 解析器（默认递归下降）\n"
            << "  -v             详细模式（打印各阶段信息）\n";
}

static std::string
readFile(const std::string& path)
{
  std::ifstream f(path);
  if(!f)
    {
      std::cerr << "无法打开文件：" << path << "\n";
      std::exit(1);
    }
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

int
main(int argc, char* argv[])
{
  if(argc < 2)
    {
      printUsage(argv[0]);
      return 1;
    }

  std::string srcFile, outFile = "out.asm";
  bool lexOnly = false, parseOnly = false, semOnly = false, verbose = false;
  bool useRegAlloc = true;
  bool useLL1 = true; // ← 新增

  for(int i = 1; i < argc; ++i)
    {
      if(std::strcmp(argv[i], "-o") == 0 && i + 1 < argc)
        outFile = argv[++i];
      else if(std::strcmp(argv[i], "--lex-only") == 0)
        lexOnly = true;
      else if(std::strcmp(argv[i], "--parse-only") == 0)
        parseOnly = true;
      else if(std::strcmp(argv[i], "--sem-only") == 0)
        semOnly = true;
      else if(std::strcmp(argv[i], "-v") == 0)
        verbose = true;
      else if(std::strcmp(argv[i], "--opt-regalloc") == 0)
        useRegAlloc = true;
      else if(std::strcmp(argv[i], "--no-regalloc") == 0)
        useRegAlloc = false;
      else if(std::strcmp(argv[i], "--ll1") == 0) // ← 新增
        useLL1 = false;
      else
        srcFile = argv[i];
    }
  if(srcFile.empty())
    {
      printUsage(argv[0]);
      return 1;
    }

  std::string source = readFile(srcFile);

  // ── 阶段一：词法分析 ────────────────────────────────────────────────────
  if(verbose)
    std::cout << "=== 词法分析 ===\n";
  Lexer lexer(source);
  auto tokens = lexer.tokenize();

  if(lexer.hasError())
    {
      for(auto& e : lexer.errors())
        std::cerr << e << "\n";
      return 1;
    }

  if(verbose || lexOnly)
    {
      for(auto& tok : tokens)
        {
          std::cout << "  [" << tokenTypeName(tok.type) << "] " << tok.value
                    << "  (行" << tok.line << " 列" << tok.col << ")\n";
        }
    }
  if(lexOnly)
    return 0;

  // ── 阶段二：语法分析 ────────────────────────────────────────────────────
  if(verbose)
    std::cout << "\n=== 语法分析（" << (useLL1 ? "表驱动 LL(1)" : "递归下降")
              << "）===\n";

  ASTPtr ast;
  bool parseHasError = false;
  std::vector<std::string> parseErrors;

  if(useLL1)
    {
      // 表驱动 LL(1)：tokens 不 move，LL1Parser 内部持有副本
      LL1Parser parser(tokens);
      ast = parser.parseProgram();
      parseHasError = parser.hasError();
      parseErrors = parser.errors();
    }
  else
    {
      // 递归下降（原有路径）
      Parser parser(std::move(tokens));
      ast = parser.parseProgram();
      parseHasError = parser.hasError();
      parseErrors = parser.errors();
    }

  if(parseHasError)
    {
      for(auto& e : parseErrors)
        std::cerr << e << "\n";
      return 1;
    }

  if(verbose || parseOnly)
    {
      std::cout << "\nAST：\n";
      printAST(ast.get());
    }
  if(parseOnly)
    return 0;

  // ── 阶段三：语义分析 ────────────────────────────────────────────────────
  if(verbose)
    std::cout << "\n=== 语义分析 ===\n";
  SemanticAnalyzer sem;
  sem.analyze(ast.get());

  if(sem.hasError())
    {
      for(auto& e : sem.errors())
        std::cerr << e << "\n";
      if(!verbose)
        return 1;
    }
  if(semOnly)
    return 0;

  // ── 阶段三·五：优化 ─────────────────────────────────────────────────────
  Optimizer opt;
  opt.optimize(ast.get(), sem.globalScope());
  if(verbose)
    {
      std::cout << "\n=== 优化 ===\n";
      std::cout << "  常量折叠:     " << opt.foldCount() << " 次\n";
      std::cout << "  代数化简:     " << opt.simplifyCount() << " 次\n";
      std::cout << "  LICM 外提:    " << opt.licmCount() << " 次\n";
    }

  // ── 阶段四：代码生成 ────────────────────────────────────────────────────
  if(verbose)
    std::cout << "\n=== 代码生成 ===\n";
  std::string asmCode;
  if(useRegAlloc)
    {
      TACCodeGen cg(sem.globalScope());
      for(auto& [name, scope] : sem.procScopes())
        cg.registerProcScope(name, scope);
      for(auto& [name, scope] : sem.procScopes())
        {
          std::vector<Symbol*> order;
          for(auto& [n, s] : scope->table)
            if(s.kind == SymKind::PARAM || s.kind == SymKind::VAR_PARAM)
              order.push_back(&s);
          std::sort(order.begin(), order.end(), [](Symbol* a, Symbol* b)
                      { return a->offset > b->offset; });
          cg.registerProcParamOrder(name, order);
        }
      asmCode = cg.generate(ast.get());
      if(verbose)
        std::cout << "  spill 次数: " << cg.spillCount() << "\n";
    }
  else
    {
      CodeGen cg(sem.globalScope());
      for(auto& [name, scope] : sem.procScopes())
        cg.registerProcScope(name, scope);
      asmCode = cg.generate(ast.get());
    }

  std::ofstream out(outFile);
  if(!out)
    {
      std::cerr << "无法写入文件：" << outFile << "\n";
      return 1;
    }
  out << asmCode;
  out.close();

  if(verbose)
    std::cout << "\n生成完成：" << outFile << "\n";
  else
    std::cout << outFile << "\n";

  return 0;
}

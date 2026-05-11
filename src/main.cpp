#include "../include/lexer.h"
#include "../include/parser.h"
#include "../include/semantic.h"
#include "../include/codegen.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>

static void printUsage(const char* prog) {
    std::cerr << "用法：" << prog << " [选项] <源文件.snl>\n"
              << "  -o <输出文件>  指定输出文件名（默认 out.asm）\n"
              << "  --lex-only     只做词法分析并打印 Token\n"
              << "  --parse-only   只做词法+语法分析并打印 AST\n"
              << "  --sem-only     词法+语法+语义分析（不生成代码）\n"
              << "  -v             详细模式（打印各阶段信息）\n";
}

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) { std::cerr << "无法打开文件：" << path << "\n"; std::exit(1); }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char* argv[]) {
    if (argc < 2) { printUsage(argv[0]); return 1; }

    std::string srcFile, outFile = "out.asm";
    bool lexOnly = false, parseOnly = false, semOnly = false, verbose = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-o") == 0 && i + 1 < argc) outFile = argv[++i];
        else if (std::strcmp(argv[i], "--lex-only") == 0)   lexOnly = true;
        else if (std::strcmp(argv[i], "--parse-only") == 0) parseOnly = true;
        else if (std::strcmp(argv[i], "--sem-only") == 0)   semOnly = true;
        else if (std::strcmp(argv[i], "-v") == 0)           verbose = true;
        else srcFile = argv[i];
    }
    if (srcFile.empty()) { printUsage(argv[0]); return 1; }

    std::string source = readFile(srcFile);

    // ── 阶段一：词法分析 ────────────────────────────────────────────────────
    if (verbose) std::cout << "=== 词法分析 ===\n";
    Lexer lexer(source);
    auto tokens = lexer.tokenize();

    if (lexer.hasError()) {
        for (auto& e : lexer.errors()) std::cerr << e << "\n";
        return 1;
    }

    if (verbose || lexOnly) {
        for (auto& tok : tokens) {
            std::cout << "  [" << tokenTypeName(tok.type) << "] "
                      << tok.value << "  (行" << tok.line << " 列" << tok.col << ")\n";
        }
    }
    if (lexOnly) return 0;

    // ── 阶段二：语法分析 ────────────────────────────────────────────────────
    if (verbose) std::cout << "\n=== 语法分析 ===\n";
    Parser parser(std::move(tokens));
    auto ast = parser.parseProgram();

    if (parser.hasError()) {
        for (auto& e : parser.errors()) std::cerr << e << "\n";
        return 1;
    }

    if (verbose || parseOnly) {
        std::cout << "\nAST：\n";
        printAST(ast.get());
    }
    if (parseOnly) return 0;

    // ── 阶段三：语义分析 ────────────────────────────────────────────────────
    if (verbose) std::cout << "\n=== 语义分析 ===\n";
    SemanticAnalyzer sem;
    sem.analyze(ast.get());

    if (sem.hasError()) {
        for (auto& e : sem.errors()) std::cerr << e << "\n";
        // 非致命：继续生成代码（调试模式）
        if (!verbose) return 1;
    }
    if (semOnly) return 0;

    // ── 阶段四：代码生成 ────────────────────────────────────────────────────
    if (verbose) std::cout << "\n=== 代码生成 ===\n";
    CodeGen cg(sem.globalScope());
    std::string asmCode = cg.generate(ast.get());

    // 写输出文件
    std::ofstream out(outFile);
    if (!out) { std::cerr << "无法写入文件：" << outFile << "\n"; return 1; }
    out << asmCode;
    out.close();

    if (verbose) std::cout << "\n生成完成：" << outFile << "\n";
    else         std::cout << outFile << "\n";

    return 0;
}

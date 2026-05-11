#pragma once
#include "ast.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <optional>

// ─── 符号条目 ──────────────────────────────────────────────────────────────────
enum class SymKind { VAR, PARAM, VAR_PARAM, PROC, TYPE };

struct Symbol {
    std::string name;
    SymKind kind;
    std::shared_ptr<TypeDesc> type;

    // 运行时信息（代码生成阶段填充）
    int offset = 0;       // 相对于 $fp 的字节偏移（负值 = 局部变量）
    int paramIndex = 0;   // 参数位置（用于 $a0-$a3 分配）
    std::string label;     // 过程的全局标签
    bool isVarParam = false; // 是否为 VAR 参数（按引用传递）
};

// ─── 作用域（嵌套符号表）─────────────────────────────────────────────────────
struct Scope {
    std::unordered_map<std::string, Symbol> table;
    Scope* parent = nullptr;
    int localSize = 0;    // 该作用域局部变量总字节数

    explicit Scope(Scope* p = nullptr) : parent(p) {}

    // 在当前作用域查找（不向上）
    Symbol* lookupLocal(const std::string& name);
    // 向上逐层查找
    Symbol* lookup(const std::string& name);
    // 插入，返回 false 表示重复定义
    bool insert(Symbol sym);
};

// ─── 语义分析器 ───────────────────────────────────────────────────────────────
class SemanticAnalyzer {
public:
    SemanticAnalyzer();

    // 对 AST 做两遍分析：
    //   第一遍：收集所有类型别名和过程签名
    //   第二遍：类型检查 + 填充 typeInfo + 计算偏移
    void analyze(ASTNode* root);

    bool hasError() const { return !errors_.empty(); }
    const std::vector<std::string>& errors() const { return errors_; }

    // 分析完成后取全局作用域
    Scope* globalScope() { return scopes_.front().get(); }
    // 取过程名 -> Scope* 映射（供 CodeGen 切换作用域使用）
    const std::unordered_map<std::string, Scope*>& procScopes() const { return procScopes_; }

private:
    std::vector<std::unique_ptr<Scope>> scopes_;
    Scope* current_ = nullptr;
    std::vector<std::string> errors_;
    std::unordered_map<std::string, Scope*> procScopes_; // 过程名->Scope 映射

    void pushScope();
    void popScope();

    void error(const std::string& msg, int line);

    // 类型解析：把 TypeDesc::NAME 解析成具体类型
    std::shared_ptr<TypeDesc> resolveType(const TypeDesc* td);

    // 类型兼容性检查
    bool typeCompatible(const TypeDesc* lhs, const TypeDesc* rhs);

    // 各节点的处理（访问者模式的平铺实现）
    void visitProgram(ASTNode* node);
    void visitDeclarePart(ASTNode* node);
    void visitTypeDec(ASTNode* node);
    void visitVarDec(ASTNode* node);
    void visitProcDec(ASTNode* node);
    void visitStmList(ASTNode* node);
    void visitStm(ASTNode* node);
    std::shared_ptr<TypeDesc> visitExp(ASTNode* node);
    std::shared_ptr<TypeDesc> visitVariable(ASTNode* node);

    // ── 类型工具 ──────────────────────────────────────────────────────────
    // 从 AST 类型节点构建 TypeDesc（支持 integer/char/array/record/别名）
    std::shared_ptr<TypeDesc> buildTypeDesc(ASTNode* typeNode);

    // 计算类型的字节大小（integer=4, char=4, array=N*elem, record=fields之和）
    int typeSize(const TypeDesc* td);

    // 将一批 ID 节点（SimpleVar）注册进当前作用域
    void registerIds(ASTNode* entryNode,
                     std::shared_ptr<TypeDesc> type,
                     SymKind kind,
                     bool isVarParam = false);

    // 当前过程名（用于 return 跳转标签）
    std::string currentProcName_;

public:
    // 调试：打印符号表内容
    void dumpScope(Scope* s, int indent = 0) const;
};

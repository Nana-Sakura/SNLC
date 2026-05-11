#pragma once
#include "token.h"
#include <memory>
#include <string>
#include <vector>

// ─── 前向声明 ──────────────────────────────────────────────────────────────────
struct ASTNode;
using ASTPtr = std::unique_ptr<ASTNode>;
using ASTList = std::vector<ASTPtr>;

// ─── 类型描述（编译期类型系统）────────────────────────────────────────────────
enum class TypeKind { INTEGER, CHAR, ARRAY, RECORD, NAME };

struct TypeDesc {
    TypeKind kind = TypeKind::INTEGER;
    // ARRAY
    int low = 0, top = 0;
    std::shared_ptr<TypeDesc> elemType;
    // RECORD
    struct Field { std::string name; std::shared_ptr<TypeDesc> type; };
    std::vector<Field> fields;
    // NAME（类型别名，待语义阶段解析）
    std::string name;

    TypeDesc() = default;
    TypeDesc(const TypeDesc&) = default;
    TypeDesc& operator=(const TypeDesc&) = default;

    static std::shared_ptr<TypeDesc> makeInt();
    static std::shared_ptr<TypeDesc> makeChar();
    static std::shared_ptr<TypeDesc> makeName(const std::string& n);
};

// ─── AST 节点种类 ──────────────────────────────────────────────────────────────
enum class NodeKind {
    // 顶层
    PROGRAM,

    // 声明
    TYPE_DEC, VAR_DEC, PROC_DEC,

    // 类型节点
    BASE_TYPE, ARRAY_TYPE, REC_TYPE,

    // 语句
    ASSIGN_STM, CALL_STM, IF_STM, WHILE_STM,
    READ_STM, WRITE_STM, RETURN_STM,

    // 表达式
    BINARY_EXP, UNARY_EXP, INT_LITERAL, VAR_EXP,

    // 变量访问
    SIMPLE_VAR, INDEX_VAR, FIELD_VAR,
};

// ─── AST 节点基类 ──────────────────────────────────────────────────────────────
struct ASTNode {
    NodeKind kind;
    int line = 0;

    // 通用字段（按需使用，避免过度子类化）
    std::string name;          // ID / 操作符文本
    int ival = 0;              // 整型常量值

    ASTList children;          // 子节点列表（顺序语义由 kind 决定）

    // 类型信息（语义分析阶段填充）
    std::shared_ptr<TypeDesc> typeInfo;

    // 参数标记（VAR 参数）
    bool isVarParam = false;

    explicit ASTNode(NodeKind k, int l = 0) : kind(k), line(l) {}
};

// ─── 便捷构造函数 ──────────────────────────────────────────────────────────────
inline ASTPtr makeNode(NodeKind k, int line = 0) {
    return std::make_unique<ASTNode>(k, line);
}

// 调试：打印 AST 树（带缩进）
void printAST(const ASTNode* node, int indent = 0);
std::string nodeKindName(NodeKind k);

#include "../include/ast.h"
#include <iostream>

// ─── TypeDesc 工厂 ─────────────────────────────────────────────────────────────
std::shared_ptr<TypeDesc> TypeDesc::makeInt() {
    auto td = std::make_unique<TypeDesc>();
    td->kind = TypeKind::INTEGER;
    return td;
}

std::shared_ptr<TypeDesc> TypeDesc::makeChar() {
    auto td = std::make_unique<TypeDesc>();
    td->kind = TypeKind::CHAR;
    return td;
}

std::shared_ptr<TypeDesc> TypeDesc::makeName(const std::string& n) {
    auto td = std::make_unique<TypeDesc>();
    td->kind = TypeKind::NAME;
    td->name = n;
    return td;
}

// ─── 节点种类名 ───────────────────────────────────────────────────────────────
std::string nodeKindName(NodeKind k) {
    switch (k) {
        case NodeKind::PROGRAM:     return "Program";
        case NodeKind::TYPE_DEC:    return "TypeDec";
        case NodeKind::VAR_DEC:     return "VarDec";
        case NodeKind::PROC_DEC:    return "ProcDec";
        case NodeKind::BASE_TYPE:   return "BaseType";
        case NodeKind::ARRAY_TYPE:  return "ArrayType";
        case NodeKind::REC_TYPE:    return "RecType";
        case NodeKind::ASSIGN_STM:  return "AssignStm";
        case NodeKind::CALL_STM:    return "CallStm";
        case NodeKind::IF_STM:      return "IfStm";
        case NodeKind::WHILE_STM:   return "WhileStm";
        case NodeKind::READ_STM:    return "ReadStm";
        case NodeKind::WRITE_STM:   return "WriteStm";
        case NodeKind::RETURN_STM:  return "ReturnStm";
        case NodeKind::BINARY_EXP:  return "BinaryExp";
        case NodeKind::UNARY_EXP:   return "UnaryExp";
        case NodeKind::INT_LITERAL: return "IntLiteral";
        case NodeKind::VAR_EXP:     return "VarExp";
        case NodeKind::SIMPLE_VAR:  return "SimpleVar";
        case NodeKind::INDEX_VAR:   return "IndexVar";
        case NodeKind::FIELD_VAR:   return "FieldVar";
    }
    return "Unknown";
}

// ─── AST 打印（调试用）────────────────────────────────────────────────────────
void printAST(const ASTNode* node, int indent) {
    if (!node) return;
    std::string pad(indent * 2, ' ');
    std::cout << pad << "[" << nodeKindName(node->kind) << "]";
    if (!node->name.empty()) std::cout << " name=" << node->name;
    if (node->kind == NodeKind::INT_LITERAL) std::cout << " val=" << node->ival;
    if (node->line > 0) std::cout << " (line " << node->line << ")";
    std::cout << "\n";
    for (const auto& child : node->children) {
        printAST(child.get(), indent + 1);
    }
}

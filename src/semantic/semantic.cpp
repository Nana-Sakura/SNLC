#include "../include/semantic.h"
#include <stdexcept>
#include <sstream>

// ─── Scope ────────────────────────────────────────────────────────────────────
Symbol* Scope::lookupLocal(const std::string& name) {
    auto it = table.find(name);
    return it != table.end() ? &it->second : nullptr;
}

Symbol* Scope::lookup(const std::string& name) {
    if (auto* s = lookupLocal(name)) return s;
    return parent ? parent->lookup(name) : nullptr;
}

bool Scope::insert(Symbol sym) {
    if (table.count(sym.name)) return false;
    table[sym.name] = std::move(sym);
    return true;
}

// ─── SemanticAnalyzer ─────────────────────────────────────────────────────────
SemanticAnalyzer::SemanticAnalyzer() {}

void SemanticAnalyzer::pushScope() {
    scopes_.push_back(std::make_unique<Scope>(current_));
    current_ = scopes_.back().get();
}

void SemanticAnalyzer::popScope() {
    if (current_ && current_->parent) {
        current_ = current_->parent;
    }
}

void SemanticAnalyzer::error(const std::string& msg, int line) {
    errors_.push_back("语义错误 第 " + std::to_string(line) + " 行：" + msg);
}

void SemanticAnalyzer::analyze(ASTNode* root) {
    if (!root) return;
    pushScope(); // 全局作用域
    visitProgram(root);
}

// ── Program ───────────────────────────────────────────────────────────────────
void SemanticAnalyzer::visitProgram(ASTNode* node) {
    if (!node || node->children.size() < 3) return;
    // children: [ProgramHead, DeclarePart, ProgramBody]
    visitDeclarePart(node->children[1].get());
    visitStmList(node->children[2].get());
}

// ── DeclarePart ───────────────────────────────────────────────────────────────
void SemanticAnalyzer::visitDeclarePart(ASTNode* node) {
    if (!node || node->children.size() < 3) return;
    visitTypeDec(node->children[0].get());
    visitVarDec(node->children[1].get());
    visitProcDec(node->children[2].get());
}

// ── TypeDec ───────────────────────────────────────────────────────────────────
void SemanticAnalyzer::visitTypeDec(ASTNode* node) {
    if (!node) return;
    for (auto& child : node->children) {
        // 每个 child：name = 类型别名, children[0] = 类型定义
        if (child->children.empty()) continue;
        // TODO: 把类型别名注册到符号表
        // Symbol sym; sym.name = child->name; sym.kind = SymKind::TYPE;
        // sym.type = buildTypeDesc(child->children[0].get());
        // if (!current_->insert(sym)) error("类型重复定义：" + child->name, child->line);
    }
}

// ── VarDec ────────────────────────────────────────────────────────────────────
void SemanticAnalyzer::visitVarDec(ASTNode* node) {
    if (!node) return;
    for (auto& entry : node->children) {
        // entry->children[0] = 类型节点，其余 = ID 节点
        // TODO: 为每个 ID 在当前作用域插入 Symbol
        // 计算偏移：current_->localSize -= sizeof(type)
        // Symbol sym; sym.name = id; sym.kind = SymKind::VAR;
        // sym.offset = -(current_->localSize);
        // current_->insert(sym);
    }
}

// ── ProcDec ───────────────────────────────────────────────────────────────────
void SemanticAnalyzer::visitProcDec(ASTNode* node) {
    if (!node) return;
    for (auto& proc : node->children) {
        // 在父作用域注册过程名
        Symbol procSym;
        procSym.name = proc->name;
        procSym.kind = SymKind::PROC;
        procSym.label = proc->name;
        if (!current_->insert(procSym)) {
            error("过程重复定义：" + proc->name, proc->line);
        }

        pushScope(); // 过程的局部作用域
        // TODO: 处理参数列表（children[0]）
        // TODO: 处理过程体的声明部分（children[1]）和语句（children[2]）
        visitDeclarePart(proc->children.size() > 1 ? proc->children[1].get() : nullptr);
        visitStmList(proc->children.size() > 2 ? proc->children[2].get() : nullptr);
        popScope();
    }
}

// ── StmList ───────────────────────────────────────────────────────────────────
void SemanticAnalyzer::visitStmList(ASTNode* node) {
    if (!node) return;
    for (auto& stm : node->children) {
        visitStm(stm.get());
    }
}

// ── Stm ───────────────────────────────────────────────────────────────────────
void SemanticAnalyzer::visitStm(ASTNode* node) {
    if (!node) return;
    switch (node->kind) {
        case NodeKind::ASSIGN_STM:
            if (node->children.size() >= 2) {
                auto lhsType = visitVariable(node->children[0].get());
                auto rhsType = visitExp(node->children[1].get());
                if (lhsType && rhsType && !typeCompatible(lhsType.get(), rhsType.get())) {
                    error("赋值类型不匹配", node->line);
                }
            }
            break;
        case NodeKind::IF_STM:
            if (node->children.size() >= 3) {
                visitExp(node->children[0].get()); // 条件
                visitStmList(node->children[1].get());
                visitStmList(node->children[2].get());
            }
            break;
        case NodeKind::WHILE_STM:
            if (node->children.size() >= 2) {
                visitExp(node->children[0].get());
                visitStmList(node->children[1].get());
            }
            break;
        case NodeKind::READ_STM: {
            auto* sym = current_->lookup(node->name);
            if (!sym) error("未定义的变量：" + node->name, node->line);
            break;
        }
        case NodeKind::WRITE_STM:
        case NodeKind::RETURN_STM:
            if (!node->children.empty()) visitExp(node->children[0].get());
            break;
        case NodeKind::CALL_STM: {
            auto* sym = current_->lookup(node->name);
            if (!sym || sym->kind != SymKind::PROC) {
                error("未定义的过程：" + node->name, node->line);
            }
            for (auto& arg : node->children) visitExp(arg.get());
            break;
        }
        default:
            for (auto& child : node->children) visitStm(child.get());
            break;
    }
}

// ── Exp ───────────────────────────────────────────────────────────────────────
std::shared_ptr<TypeDesc> SemanticAnalyzer::visitExp(ASTNode* node) {
    if (!node) return nullptr;
    switch (node->kind) {
        case NodeKind::INT_LITERAL:
            node->typeInfo = std::shared_ptr<TypeDesc>(TypeDesc::makeInt().release());
            return node->typeInfo;
        case NodeKind::BINARY_EXP: {
            auto lt = node->children.size() > 0 ? visitExp(node->children[0].get()) : nullptr;
            auto rt = node->children.size() > 1 ? visitExp(node->children[1].get()) : nullptr;
            if (lt && rt && !typeCompatible(lt.get(), rt.get())) {
                error("运算符两侧类型不匹配：" + node->name, node->line);
            }
            node->typeInfo = lt ? lt : rt;
            return node->typeInfo;
        }
        case NodeKind::VAR_EXP:
            return visitVariable(!node->children.empty() ? node->children[0].get() : node);
        default:
            return nullptr;
    }
}

// ── Variable ──────────────────────────────────────────────────────────────────
std::shared_ptr<TypeDesc> SemanticAnalyzer::visitVariable(ASTNode* node) {
    if (!node) return nullptr;
    if (node->kind == NodeKind::SIMPLE_VAR) {
        auto* sym = current_->lookup(node->name);
        if (!sym) {
            error("未定义的变量：" + node->name, node->line);
            return nullptr;
        }
        node->typeInfo = sym->type;
        return sym->type;
    }
    // INDEX_VAR, FIELD_VAR：TODO 进一步处理
    return nullptr;
}

// ── 类型兼容性 ────────────────────────────────────────────────────────────────
bool SemanticAnalyzer::typeCompatible(const TypeDesc* lhs, const TypeDesc* rhs) {
    if (!lhs || !rhs) return true; // 容错
    if (lhs->kind != rhs->kind) return false;
    if (lhs->kind == TypeKind::ARRAY) {
        return lhs->low == rhs->low && lhs->top == rhs->top &&
               typeCompatible(lhs->elemType.get(), rhs->elemType.get());
    }
    return true;
}

std::shared_ptr<TypeDesc> SemanticAnalyzer::resolveType(const TypeDesc* td) {
    if (!td) return nullptr;
    if (td->kind == TypeKind::NAME) {
        auto* sym = current_->lookup(td->name);
        if (!sym || sym->kind != SymKind::TYPE) return nullptr;
        return sym->type;
    }
    return nullptr; // 基础类型已经是具体的
}

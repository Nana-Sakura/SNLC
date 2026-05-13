#include "../include/optimizer.h"
#include <iostream>
#include <climits>

// ─── 构造 ─────────────────────────────────────────────────────────────────────
Optimizer::Optimizer() {}

// ─── 公共入口 ─────────────────────────────────────────────────────────────────
void Optimizer::optimize(ASTNode* root) {
    if (!root) return;
    // 对每个 child 做递归优化（in-place 修改 children 的 unique_ptr）
    for (auto& child : root->children)
        optimizeNode(child);
}

// ─── 工具 ─────────────────────────────────────────────────────────────────────
bool Optimizer::isConst(const ASTNode* node, int& val) const {
    if (!node) return false;
    if (node->kind == NodeKind::INT_LITERAL) {
        val = node->ival;
        return true;
    }
    return false;
}

ASTPtr Optimizer::makeInt(int val, int line) {
    auto n = makeNode(NodeKind::INT_LITERAL, line);
    n->ival = val;
    n->name = std::to_string(val);
    // 传播 INTEGER 类型（可选，语义分析已做过，这里只需要 ival）
    return n;
}

// ─── 核心递归 ─────────────────────────────────────────────────────────────────
void Optimizer::optimizeNode(ASTPtr& node) {
    if (!node) return;

    // 1. 先对所有子节点递归优化（后序遍历：从叶到根）
    for (auto& child : node->children)
        optimizeNode(child);

    // 2. 对当前节点尝试常量折叠
    if (ASTPtr folded = tryFold(node.get())) {
        node = std::move(folded);   // 替换原节点
        ++foldCount_;
        return;
    }

    // 3. 再尝试代数化简（折叠优先于化简）
    if (trySimplify(node)) {
        ++simplifyCount_;
        // 化简后子节点可能变成常量，递归再尝试折叠
        if (ASTPtr folded2 = tryFold(node.get())) {
            node = std::move(folded2);
            ++foldCount_;
        }
    }
}

// ─── 常量折叠 ─────────────────────────────────────────────────────────────────
ASTPtr Optimizer::tryFold(ASTNode* node) {
    if (!node || node->kind != NodeKind::BINARY_EXP) return nullptr;
    if (node->children.size() < 2) return nullptr;

    int lv, rv;
    if (!isConst(node->children[0].get(), lv)) return nullptr;
    if (!isConst(node->children[1].get(), rv)) return nullptr;

    const std::string& op = node->name;
    int result = 0;

    if      (op == "+") result = lv + rv;
    else if (op == "-") result = lv - rv;
    else if (op == "*") result = lv * rv;
    else if (op == "/") {
        if (rv == 0) return nullptr; // 除零不折叠
        result = lv / rv;
    }
    else if (op == "<") result = (lv < rv) ? 1 : 0;
    else if (op == "=") result = (lv == rv) ? 1 : 0;
    else return nullptr;

    return makeInt(result, node->line);
}

// ─── 代数化简 ─────────────────────────────────────────────────────────────────
// 修改 node（可能替换 node 本身），返回是否发生了化简
bool Optimizer::trySimplify(ASTPtr& node) {
    if (!node || node->kind != NodeKind::BINARY_EXP) return false;
    if (node->children.size() < 2) return false;

    ASTNode* L = node->children[0].get();
    ASTNode* R = node->children[1].get();
    const std::string& op = node->name;
    int lv, rv;
    bool Lconst = isConst(L, lv);
    bool Rconst = isConst(R, rv);

    // ── x + 0  →  x ───────────────────────────────────────────────────────
    // ── 0 + x  →  x
    if (op == "+") {
        if (Rconst && rv == 0) { node = std::move(node->children[0]); return true; }
        if (Lconst && lv == 0) { node = std::move(node->children[1]); return true; }
    }

    // ── x - 0  →  x ───────────────────────────────────────────────────────
    if (op == "-") {
        if (Rconst && rv == 0) { node = std::move(node->children[0]); return true; }
    }

    // ── x * 1  →  x ───────────────────────────────────────────────────────
    // ── 1 * x  →  x
    // ── x * 0  →  0
    // ── 0 * x  →  0
    if (op == "*") {
        if (Rconst && rv == 1) { node = std::move(node->children[0]); return true; }
        if (Lconst && lv == 1) { node = std::move(node->children[1]); return true; }
        if (Rconst && rv == 0) { node = makeInt(0, node->line);        return true; }
        if (Lconst && lv == 0) { node = makeInt(0, node->line);        return true; }
    }

    // ── x / 1  →  x ───────────────────────────────────────────────────────
    if (op == "/") {
        if (Rconst && rv == 1) { node = std::move(node->children[0]); return true; }
    }

    return false;
}

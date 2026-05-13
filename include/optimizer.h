#pragma once
#include "ast.h"
#include <vector>
#include <string>

// ─── AST 优化器 ───────────────────────────────────────────────────────────────
//
//  在代码生成之前对 AST 做 pass，当前实现：
//
//  1. 常量折叠（Constant Folding）
//     - 两侧均为整型常量的 BINARY_EXP 直接计算为 INT_LITERAL
//       例：2 + 3  →  5
//           10 * 4 →  40
//           6 / 2  →  3
//           7 < 8  →  1（布尔值）
//
//  2. 代数化简（Algebraic Simplification）
//     - x + 0  →  x          x - 0  →  x
//     - x * 1  →  x          x * 0  →  0
//     - x / 1  →  x
//     - 0 + x  →  x          1 * x  →  x
//     - 0 * x  →  0
//
//  3. 常量传播（Constant Propagation，目前仅对 INT_LITERAL 做）
//     将折叠结果递归地向上传播，使父节点也能享受折叠机会。
//
//  这些优化是安全的（无副作用节点的纯运算），不会改变语义。
//
class Optimizer {
public:
    Optimizer();

    // 对整棵 AST 做优化，直接修改 node（in-place）
    void optimize(ASTNode* root);

    // 统计信息
    int foldCount()    const { return foldCount_; }
    int simplifyCount() const { return simplifyCount_; }

private:
    int foldCount_    = 0;
    int simplifyCount_ = 0;

    // 递归优化一个节点，返回是否发生了变化
    void optimizeNode(ASTPtr& node);

    // 尝试常量折叠：如果成功返回折叠后的新节点（否则 nullptr）
    ASTPtr tryFold(ASTNode* node);

    // 尝试代数化简：修改 node->children（in-place），返回是否化简
    bool trySimplify(ASTPtr& node);

    // 判断节点是否是整型常量，若是则通过 val 输出其值
    bool isConst(const ASTNode* node, int& val) const;

    // 创建整型常量节点
    static ASTPtr makeInt(int val, int line = 0);
};

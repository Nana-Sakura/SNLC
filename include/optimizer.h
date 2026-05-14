#pragma once
#include <ast.h>
#include <semantic.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ─── AST 优化器
// ───────────────────────────────────────────────────────────────
//
//  Pass 1：常量折叠（Constant Folding）
//    两侧均为整型常量的 BINARY_EXP 直接计算
//    例：2 + 3 → 5，10 * 4 → 40
//
//  Pass 2：代数化简（Algebraic Simplification）
//    x+0→x，x*1→x，0*x→0，x/1→x ...
//
//  Pass 3：循环不变式外提（LICM, Loop Invariant Code Motion）
//    检测 WHILE 循环体内"所有操作数在循环中不被赋值"的子表达式，
//    将其提取为临时变量并插到循环前，循环体内替换为变量引用。
//    例：
//      while i < n do
//          a[i] := base * stride + offset;
//          i := i + 1
//      endwh
//    →
//      __licm_0 := base * stride;
//      __licm_1 := __licm_0 + offset;
//      while i < n do
//          a[i] := __licm_1;
//          i := i + 1
//      endwh
//
class Optimizer
{
public:
  Optimizer();

  // 对整棵 AST 做所有优化 pass（原地修改）
  // scope：当前全局作用域，LICM 用来注册提取出来的临时变量
  void optimize(ASTNode* root, Scope* scope = nullptr);

  // 统计
  int
  foldCount() const
  {
    return foldCount_;
  }
  int
  simplifyCount() const
  {
    return simplifyCount_;
  }
  int
  licmCount() const
  {
    return licmCount_;
  }

private:
  int foldCount_ = 0;
  int simplifyCount_ = 0;
  int licmCount_ = 0;
  int tmpCounter_ = 0;     // __licm_N 的计数器
  Scope* scope_ = nullptr; // 用于注册 LICM 临时变量

  // ── Pass 1 & 2：常量折叠 + 代数化简 ─────────────────────────────────
  void optimizeNode(ASTPtr& node);
  ASTPtr tryFold(ASTNode* node);
  bool trySimplify(ASTPtr& node);
  bool isConst(const ASTNode* node, int& val) const;
  static ASTPtr makeInt(int val, int line = 0);

  // ── Pass 3：LICM ──────────────────────────────────────────────────────

  // 顶层 LICM 入口：遍历整棵树找 WHILE_STM 节点
  // parentChildren：包含该节点的 children 列表（用于在 while 前插入语句）
  // idx：该 while 节点在 parentChildren 中的下标
  void licmPass(ASTNode* root);
  void licmVisit(ASTList& siblings, int idx);

  // 第一步：收集循环体内所有被赋值（定义）的变量名
  void collectDefs(const ASTNode* node,
                   std::unordered_set<std::string>& defs) const;

  // 第二步：在表达式子树中找不变量子表达式
  // defs：循环内定义的变量集合
  // hoisted：已提取的临时变量名集合（避免重复提取）
  // out：输出，收集 [临时变量名, 不变量子树] 对
  //      子树会被替换为对应的 SIMPLE_VAR 节点（in-place）
  void findInvariants(ASTPtr& node,
                      const std::unordered_set<std::string>& defs,
                      std::unordered_set<std::string>& hoisted,
                      std::vector<std::pair<std::string, ASTPtr> >& out);

  // 判断一棵表达式子树是否是循环不变量
  // 不变量条件：
  //   1. INT_LITERAL（常量）
  //   2. SIMPLE_VAR 且该变量名不在 defs 里
  //   3. BINARY_EXP 且两侧都是不变量（递归）
  bool isInvariant(const ASTNode* node,
                   const std::unordered_set<std::string>& defs) const;

  // 生成唯一临时变量名
  std::string newTmpName();

  // 构造一个 ASSIGN_STM 节点：tmpName := expr
  static ASTPtr makeAssign(const std::string& tmpName, ASTPtr expr, int line);

  // 构造一个 SIMPLE_VAR 节点
  static ASTPtr makeVar(const std::string& name, int line = 0);
};

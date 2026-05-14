#include <cassert>
#include <climits>
#include <optimizer.h>

// ═══════════════════════════════════════════════════════════════════════════════
// 构造
// ═══════════════════════════════════════════════════════════════════════════════

Optimizer::Optimizer() {}

// ═══════════════════════════════════════════════════════════════════════════════
// 公共入口：所有 pass 按序执行
// ═══════════════════════════════════════════════════════════════════════════════

void
Optimizer::optimize(ASTNode* root, Scope* scope)
{
  if(!root)
    return;
  scope_ = scope; // 保存 scope 供 LICM 注册临时变量

  // Pass 1 & 2：常量折叠 + 代数化简（后序，对每个子节点递归）
  for(auto& child : root->children)
    optimizeNode(child);

  // Pass 3：LICM（在折叠之后运行，因为折叠可能把不变量变成常量，
  //         更容易识别；LICM 再次对提取出来的临时赋值做折叠）
  licmPass(root);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Pass 1 & 2：常量折叠 + 代数化简
// ═══════════════════════════════════════════════════════════════════════════════

bool
Optimizer::isConst(const ASTNode* node, int& val) const
{
  if(!node)
    return false;
  if(node->kind == NodeKind::INT_LITERAL)
    {
      val = node->ival;
      return true;
    }
  return false;
}

ASTPtr
Optimizer::makeInt(int val, int line)
{
  auto n = makeNode(NodeKind::INT_LITERAL, line);
  n->ival = val;
  n->name = std::to_string(val);
  return n;
}

void
Optimizer::optimizeNode(ASTPtr& node)
{
  if(!node)
    return;
  for(auto& child : node->children)
    optimizeNode(child);

  if(ASTPtr folded = tryFold(node.get()))
    {
      node = std::move(folded);
      ++foldCount_;
      return;
    }
  if(trySimplify(node))
    {
      ++simplifyCount_;
      if(ASTPtr folded2 = tryFold(node.get()))
        {
          node = std::move(folded2);
          ++foldCount_;
        }
    }
}

ASTPtr
Optimizer::tryFold(ASTNode* node)
{
  if(!node || node->kind != NodeKind::BINARY_EXP)
    return nullptr;
  if(node->children.size() < 2)
    return nullptr;
  int lv, rv;
  if(!isConst(node->children[0].get(), lv))
    return nullptr;
  if(!isConst(node->children[1].get(), rv))
    return nullptr;
  const std::string& op = node->name;
  int result = 0;
  if(op == "+")
    result = lv + rv;
  else if(op == "-")
    result = lv - rv;
  else if(op == "*")
    result = lv * rv;
  else if(op == "/")
    {
      if(rv == 0)
        return nullptr;
      result = lv / rv;
    }
  else if(op == "<")
    result = (lv < rv) ? 1 : 0;
  else if(op == "=")
    result = (lv == rv) ? 1 : 0;
  else
    return nullptr;
  return makeInt(result, node->line);
}

bool
Optimizer::trySimplify(ASTPtr& node)
{
  if(!node || node->kind != NodeKind::BINARY_EXP)
    return false;
  if(node->children.size() < 2)
    return false;
  ASTNode* L = node->children[0].get();
  ASTNode* R = node->children[1].get();
  const std::string& op = node->name;
  int lv, rv;
  bool Lc = isConst(L, lv), Rc = isConst(R, rv);
  if(op == "+")
    {
      if(Rc && rv == 0)
        {
          node = std::move(node->children[0]);
          return true;
        }
      if(Lc && lv == 0)
        {
          node = std::move(node->children[1]);
          return true;
        }
    }
  if(op == "-")
    {
      if(Rc && rv == 0)
        {
          node = std::move(node->children[0]);
          return true;
        }
    }
  if(op == "*")
    {
      if(Rc && rv == 1)
        {
          node = std::move(node->children[0]);
          return true;
        }
      if(Lc && lv == 1)
        {
          node = std::move(node->children[1]);
          return true;
        }
      if(Rc && rv == 0)
        {
          node = makeInt(0, node->line);
          return true;
        }
      if(Lc && lv == 0)
        {
          node = makeInt(0, node->line);
          return true;
        }
    }
  if(op == "/")
    {
      if(Rc && rv == 1)
        {
          node = std::move(node->children[0]);
          return true;
        }
    }
  return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Pass 3：LICM
// ═══════════════════════════════════════════════════════════════════════════════

// ── 工具
// ──────────────────────────────────────────────────────────────────────

std::string
Optimizer::newTmpName()
{
  return "__licm_" + std::to_string(tmpCounter_++);
}

// 构造 "tmpName := expr" 的 ASSIGN_STM 节点
ASTPtr
Optimizer::makeAssign(const std::string& tmpName, ASTPtr expr, int line)
{
  auto assign = makeNode(NodeKind::ASSIGN_STM, line);
  assign->name = "__licm_assign__";
  // children[0] = LHS（SIMPLE_VAR）
  auto lhs = makeNode(NodeKind::SIMPLE_VAR, line);
  lhs->name = tmpName;
  assign->children.push_back(std::move(lhs));
  // children[1] = RHS（提取出来的不变量表达式）
  assign->children.push_back(std::move(expr));
  return assign;
}

// 构造 SIMPLE_VAR 节点
ASTPtr
Optimizer::makeVar(const std::string& name, int line)
{
  auto v = makeNode(NodeKind::SIMPLE_VAR, line);
  v->name = name;
  return v;
}

// ── 顶层入口：递归遍历整棵 AST 找 WHILE_STM ─────────────────────────────────
void
Optimizer::licmPass(ASTNode* root)
{
  if(!root)
    return;
  // 遍历 root 的 children，找 WHILE_STM 节点
  // 注意：要先递归处理内层循环，再处理外层（让内层先提取，外层再判断）
  for(int i = 0; i < (int)root->children.size(); ++i)
    {
      ASTNode* child = root->children[i].get();
      if(!child)
        continue;

      // 先递归处理该节点的内部（处理嵌套循环）
      licmPass(child);

      // 如果这个节点是 WHILE_STM，对它做 LICM
      if(child->kind == NodeKind::WHILE_STM)
        {
          licmVisit(root->children, i);
          // licmVisit 可能在 i 前面插入了若干语句，
          // 所以 i 要跳过这些新插入的节点
          // 重新扫一遍：找到新的 WHILE_STM 在哪个位置
          // （简单做法：重新找，因为插入后下标变了）
        }

      // 对于 block 容器节点，也要递归进去找 while
      if(child->name == "__block__")
        {
          licmPass(child);
        }
    }
}

// ── 对一个 WHILE_STM 做 LICM
// ───────────────────────────────────────────────── siblings：包含该 while 的
// children 列表 idx：while 在 siblings 中的当前下标
void
Optimizer::licmVisit(ASTList& siblings, int idx)
{
  ASTNode* whileNode = siblings[idx].get();
  // whileNode->children[0] = 条件表达式（RelExp）
  // whileNode->children[1] = 循环体（block）

  if(whileNode->children.size() < 2)
    return;
  ASTNode* body = whileNode->children[1].get();

  // ── 第一步：收集循环体内所有被赋值（定义）的变量名 ──────────────────
  std::unordered_set<std::string> defs;
  collectDefs(body, defs);

  // ── 第二步：在循环体内扫描不变量，替换为临时变量 ─────────────────────
  std::unordered_set<std::string> hoisted; // 已提取过的临时变量，避免重复
  std::vector<std::pair<std::string, ASTPtr> > extracted; // [(tmpName, expr)]

  // 对循环体里的每条语句做扫描
  // body 是一个 block 节点，其 children 是各条语句
  for(auto& stm : body->children)
    {
      // 只对赋值语句的 RHS 做提取（LHS 是写目标，不提取）
      if(stm && stm->kind == NodeKind::ASSIGN_STM && stm->children.size() >= 2)
        {
          findInvariants(stm->children[1], defs, hoisted, extracted);
        }
      // write 语句的表达式也可以提取
      if(stm && stm->kind == NodeKind::WRITE_STM && !stm->children.empty())
        {
          findInvariants(stm->children[0], defs, hoisted, extracted);
        }
      // if/while 条件里也可以提取（但嵌套循环已由递归处理，这里只看条件）
      if(stm && stm->kind == NodeKind::IF_STM && !stm->children.empty())
        {
          findInvariants(stm->children[0], defs, hoisted, extracted);
        }
    }

  if(extracted.empty())
    return; // 没有找到不变量，不做任何修改

  // ── 第三步：把提取出来的赋值语句插到 while 前面 ──────────────────────
  // 插入顺序：extracted[0] 最先（可能被后面的表达式依赖）
  // 使用 insert 把节点插到 siblings[idx] 之前
  for(int k = (int)extracted.size() - 1; k >= 0; --k)
    {
      auto& [tmpName, expr] = extracted[k];
      int line = whileNode->line;

      // 对提取出来的表达式再做一次常量折叠（提取出来后可能还能折）
      optimizeNode(expr);

      // ── 关键：把临时变量注册到符号表，分配栈帧偏移 ──────────────────
      // 这样代码生成器才能找到这个变量的内存位置
      if(scope_)
        {
          // 临时变量类型暂定 INTEGER（SNL 里 LICM 提取的都是整型表达式）
          auto intType = TypeDesc::makeInt();
          scope_->localSize += 4; // 占 4 字节（一个 word）
          Symbol sym;
          sym.name = tmpName;
          sym.kind = SymKind::VAR;
          sym.type = intType;
          sym.offset = -(scope_->localSize); // 负偏移，在 $fp 下方
          scope_->insert(sym);               // 插入符号表（若已存在则忽略）
        }

      ASTPtr assignStm = makeAssign(tmpName, std::move(expr), line);
      siblings.insert(siblings.begin() + idx, std::move(assignStm));
      // idx 随每次插入递增（while 节点被往后推了一位）
      ++idx;
      ++licmCount_;
    }
}

// ── 收集循环体内所有被赋值的变量名
// ───────────────────────────────────────────
void
Optimizer::collectDefs(const ASTNode* node,
                       std::unordered_set<std::string>& defs) const
{
  if(!node)
    return;

  // 赋值语句：LHS 是被定义的变量
  if(node->kind == NodeKind::ASSIGN_STM && node->children.size() >= 1)
    {
      const ASTNode* lhs = node->children[0].get();
      if(lhs)
        {
          // 简单变量：直接记名字
          if(lhs->kind == NodeKind::SIMPLE_VAR)
            defs.insert(lhs->name);
          // 数组元素 a[i] := ...：记录 a 被写了
          // （保守处理：只要数组被写就认为整个数组不是不变量）
          if(lhs->kind == NodeKind::INDEX_VAR && !lhs->children.empty()
             && lhs->children[0])
            defs.insert(lhs->children[0]->name);
          // record 字段 p.x := ...：记录 p 被写了
          if(lhs->kind == NodeKind::FIELD_VAR && !lhs->children.empty()
             && lhs->children[0])
            defs.insert(lhs->children[0]->name);
        }
    }

  // read 语句也是赋值：read(x) 定义了 x
  if(node->kind == NodeKind::READ_STM)
    defs.insert(node->name);

  // 过程调用里的 VAR 参数也可能改变变量（保守：把所有实参都记为 defs）
  if(node->kind == NodeKind::CALL_STM)
    for(auto& arg : node->children)
      if(arg && arg->kind == NodeKind::SIMPLE_VAR)
        defs.insert(arg->name);

  // 递归处理子节点（但不进入嵌套的 WHILE 内部的定义，
  // 因为内层循环已经由 licmPass 的递归优先处理了）
  // 这里我们仍然进入，采用保守策略：
  // 嵌套 while 里的 defs 也算到外层的 defs 里
  for(auto& child : node->children)
    collectDefs(child.get(), defs);
}

// ── 判断表达式子树是否是循环不变量
// ───────────────────────────────────────────
bool
Optimizer::isInvariant(const ASTNode* node,
                       const std::unordered_set<std::string>& defs) const
{
  if(!node)
    return false;

  // 整型常量：永远不变
  if(node->kind == NodeKind::INT_LITERAL)
    return true;

  // 简单变量：不在定义集里 → 不变量
  if(node->kind == NodeKind::SIMPLE_VAR)
    return defs.find(node->name) == defs.end();

  // 二元运算：两侧都是不变量 → 整体是不变量
  if(node->kind == NodeKind::BINARY_EXP && node->children.size() == 2)
    return isInvariant(node->children[0].get(), defs)
           && isInvariant(node->children[1].get(), defs);

  // VAR_EXP 包装层：透传
  if(node->kind == NodeKind::VAR_EXP && !node->children.empty())
    return isInvariant(node->children[0].get(), defs);

  // 数组访问、字段访问、过程调用等：保守认为不是不变量
  return false;
}

// ── 在表达式子树中找不变量，替换为临时变量引用
// ───────────────────────────────
//
// 策略：从上到下扫描表达式树
//   - 如果整棵子树是不变量 → 提取，替换为 SIMPLE_VAR(__licm_N)
//   - 如果不是整体不变量，但子树是 BINARY_EXP → 递归扫描左右子树
//
// 只提取 BINARY_EXP 级别的表达式（单个变量和常量不值得提取）
//
void
Optimizer::findInvariants(ASTPtr& node,
                          const std::unordered_set<std::string>& defs,
                          std::unordered_set<std::string>& hoisted,
                          std::vector<std::pair<std::string, ASTPtr> >& out)
{

  if(!node)
    return;

  // 只对 BINARY_EXP 做提取（单个变量/常量不提取）
  if(node->kind != NodeKind::BINARY_EXP)
    {
      // 但要继续递归 VAR_EXP 内部
      if(node->kind == NodeKind::VAR_EXP)
        {
          for(auto& c : node->children)
            findInvariants(c, defs, hoisted, out);
        }
      return;
    }

  // 如果整棵子树是不变量 → 提取
  if(isInvariant(node.get(), defs))
    {
      // 用子树的"结构字符串"作 key，避免重复提取相同的表达式
      // 简单实现：用节点地址（同一棵树里不重复）
      // 更精确：用表达式的文本表示（这里用简单方案）
      std::string tmpName = newTmpName();
      int line = node->line;

      // 把当前节点的所有权转移到 out，node 替换为变量引用
      out.push_back({ tmpName, std::move(node) });
      node = makeVar(tmpName, line); // 原地替换为 SIMPLE_VAR
      hoisted.insert(tmpName);
      return;
    }

  // 整体不是不变量，但可能有不变量子树 → 递归左右
  for(auto& child : node->children)
    findInvariants(child, defs, hoisted, out);
}

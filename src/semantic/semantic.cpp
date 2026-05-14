#include <cassert>
#include <iostream>
#include <semantic.h>
#include <sstream>
#include <stdexcept>

// ═══════════════════════════════════════════════════════════════════════════════
// Scope
// ═══════════════════════════════════════════════════════════════════════════════

Symbol*
Scope::lookupLocal(const std::string& name)
{
  auto it = table.find(name);
  return it != table.end() ? &it->second : nullptr;
}

Symbol*
Scope::lookup(const std::string& name)
{
  if(auto* s = lookupLocal(name))
    return s;
  return parent ? parent->lookup(name) : nullptr;
}

bool
Scope::insert(Symbol sym)
{
  if(table.count(sym.name))
    return false;
  table[sym.name] = std::move(sym);
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 生命周期
// ═══════════════════════════════════════════════════════════════════════════════

SemanticAnalyzer::SemanticAnalyzer() {}

void
SemanticAnalyzer::pushScope()
{
  scopes_.push_back(std::make_unique<Scope>(current_));
  current_ = scopes_.back().get();
}

void
SemanticAnalyzer::popScope()
{
  if(current_ && current_->parent)
    current_ = current_->parent;
}

void
SemanticAnalyzer::error(const std::string& msg, int line)
{
  errors_.push_back("语义错误 第 " + std::to_string(line) + " 行：" + msg);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 类型工具
// ═══════════════════════════════════════════════════════════════════════════════

std::shared_ptr<TypeDesc>
SemanticAnalyzer::buildTypeDesc(ASTNode* node)
{
  if(!node)
    return nullptr;

  // ── BaseType（name = "integer" / "char" / 类型别名）────────────────────
  if(node->kind == NodeKind::BASE_TYPE)
    {
      if(node->name == "integer")
        return TypeDesc::makeInt();
      if(node->name == "char")
        return TypeDesc::makeChar();
      // 类型别名查表
      auto* sym = current_->lookup(node->name);
      if(!sym || sym->kind != SymKind::TYPE)
        {
          error("未定义的类型名：" + node->name, node->line);
          return TypeDesc::makeInt();
        }
      return sym->type;
    }

  // ── ArrayType（ival=low, name=top字符串, children[0]=elemType）──────────
  if(node->kind == NodeKind::ARRAY_TYPE)
    {
      auto td = std::make_unique<TypeDesc>();
      td->kind = TypeKind::ARRAY;
      td->low = node->ival;
      td->top = std::stoi(node->name);
      td->elemType = !node->children.empty()
                         ? buildTypeDesc(node->children[0].get())
                         : TypeDesc::makeInt();
      return std::shared_ptr<TypeDesc>(std::move(td));
    }

  // ── RecType（children 交替存放 SimpleVar 和 typeNode）───────────────────
  if(node->kind == NodeKind::REC_TYPE)
    {
      auto td = std::make_unique<TypeDesc>();
      td->kind = TypeKind::RECORD;

      std::vector<ASTNode*> pendingIds;
      // 这里的遍历顺序是：先出现若干
      // SimpleVar（字段名），随后出现对应的类型节点， 再出现下一组字段名 …
      // 如此交替。原实现在遇到类型节点时先检查 curType 是否已存在，
      // 导致第一次字段（如 integer x）
      // 没有被正确关联到其类型，进而导致后续字段的类型错位。
      // 修正思路：在遇到非
      // SIMPLE_VAR（即类型节点）时，先构建该类型，然后把之前收集的 pendingIds
      // 立即绑定到该类型，最后再准备收集下一组字段。
      std::shared_ptr<TypeDesc> curType;

      for(auto& child : node->children)
        {
          if(child->kind == NodeKind::SIMPLE_VAR)
            {
              // 收集字段名，等待后面的类型节点来决定它们的类型
              pendingIds.push_back(child.get());
            }
          else
            {
              // 先把当前类型节点解析为 TypeDesc
              curType = buildTypeDesc(child.get());
              // 然后把之前收集的字段名全部绑定到该类型
              if(curType && !pendingIds.empty())
                {
                  for(auto* id : pendingIds)
                    {
                      TypeDesc::Field f;
                      f.name = id->name;
                      f.type = std::make_shared<TypeDesc>(*curType);
                      td->fields.push_back(std::move(f));
                    }
                  pendingIds.clear();
                }
            }
        }
      // 若最后一次循环结束后仍有未绑定的字段（理论上不会出现），也进行一次绑定
      if(curType && !pendingIds.empty())
        {
          for(auto* id : pendingIds)
            {
              TypeDesc::Field f;
              f.name = id->name;
              f.type = std::make_shared<TypeDesc>(*curType);
              td->fields.push_back(std::move(f));
            }
        }
      return std::shared_ptr<TypeDesc>(std::move(td));
    }

  return TypeDesc::makeInt();
}

int
SemanticAnalyzer::typeSize(const TypeDesc* td)
{
  if(!td)
    return 4;
  switch(td->kind)
    {
    case TypeKind::INTEGER:
      return 4;
    case TypeKind::CHAR:
      return 4;
    case TypeKind::NAME:
      {
        auto* sym = current_->lookup(td->name);
        return sym && sym->kind == SymKind::TYPE ? typeSize(sym->type.get())
                                                 : 4;
      }
    case TypeKind::ARRAY:
      {
        int n = std::max(1, td->top - td->low + 1);
        return n * typeSize(td->elemType.get());
      }
    case TypeKind::RECORD:
      {
        int total = 0;
        for(auto& f : td->fields)
          total += typeSize(f.type.get());
        return total;
      }
    }
  return 4;
}

// entryNode->children：[typeNode, SimpleVar, SimpleVar, ...]
void
SemanticAnalyzer::registerIds(ASTNode* entryNode,
                              std::shared_ptr<TypeDesc> type, SymKind kind,
                              bool isVarParam)
{
  if(!entryNode)
    return;
  int sz = isVarParam ? 4 : typeSize(type.get());

  for(size_t i = 1; i < entryNode->children.size(); ++i)
    {
      auto* id = entryNode->children[i].get();
      if(id->kind != NodeKind::SIMPLE_VAR)
        continue;

      Symbol sym;
      sym.name = id->name;
      sym.kind = kind;
      sym.type = type;
      sym.isVarParam = isVarParam;
      current_->localSize += sz;
      sym.offset = -(current_->localSize); // 负偏移 = 局部变量

      if(!current_->insert(sym))
        error("变量重复定义：" + id->name, id->line);
      id->typeInfo = type;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 分析入口
// ═══════════════════════════════════════════════════════════════════════════════

void
SemanticAnalyzer::analyze(ASTNode* root)
{
  if(!root)
    return;
  pushScope();
  currentProcName_ = "main";
  visitProgram(root);
}

// ═══════════════════════════════════════════════════════════════════════════════
// visit 系列
// ═══════════════════════════════════════════════════════════════════════════════

void
SemanticAnalyzer::visitProgram(ASTNode* node)
{
  if(!node || node->children.size() < 3)
    return;
  visitDeclarePart(node->children[1].get());
  visitStmList(node->children[2].get());
}

void
SemanticAnalyzer::visitDeclarePart(ASTNode* node)
{
  if(!node || node->children.size() < 3)
    return;
  visitTypeDec(node->children[0].get());
  visitVarDec(node->children[1].get());
  visitProcDec(node->children[2].get());
}

// ── TypeDec
// ───────────────────────────────────────────────────────────────────
// node->children[0] = TypeDecList 节点，其 children 才是各条别名声明
void
SemanticAnalyzer::visitTypeDec(ASTNode* node)
{
  if(!node)
    return;
  for(auto& listNode : node->children)
    { // TypeDecList
      for(auto& child : listNode->children)
        { // 单条别名：name=别名, children[0]=类型
          if(child->children.empty())
            continue;
          auto type = buildTypeDesc(child->children[0].get());
          if(!type)
            {
              error("无法解析类型：" + child->name, child->line);
              continue;
            }

          Symbol sym;
          sym.name = child->name;
          sym.kind = SymKind::TYPE;
          sym.type = type;
          if(!current_->insert(sym))
            error("类型名重复定义：" + child->name, child->line);
        }
    }
}

// ── VarDec
// ────────────────────────────────────────────────────────────────────
void
SemanticAnalyzer::visitVarDec(ASTNode* node)
{
  if(!node)
    return;
  for(auto& entry : node->children)
    {
      if(entry->children.empty())
        continue;
      auto type = buildTypeDesc(entry->children[0].get());
      if(!type)
        {
          error("无法解析变量类型", entry->line);
          continue;
        }
      registerIds(entry.get(), type, SymKind::VAR);
    }
}

// ── ProcDec
// ───────────────────────────────────────────────────────────────────
void
SemanticAnalyzer::visitProcDec(ASTNode* node)
{
  if(!node)
    return;
  for(auto& proc : node->children)
    {
      // 父作用域注册过程名
      Symbol procSym;
      procSym.name = proc->name;
      procSym.kind = SymKind::PROC;
      procSym.label = proc->name;
      if(!current_->insert(procSym))
        error("过程重复定义：" + proc->name, proc->line);

      pushScope();
      procScopes_[proc->name] = current_; // 注册过程作用域供 CodeGen 使用
      std::string savedProc = currentProcName_;
      currentProcName_ = proc->name;

      // 处理参数：负偏移，从 -12($fp) 起（-4 和 -8 留给 old_ra/old_fp）
      // 即 localSize 从 8 开始，使第一个参数在 -12($fp)
      current_->localSize = 8; // 预留 old_ra(4) + old_fp(4)
      if(!proc->children.empty())
        {
          ASTNode* params = proc->children[0].get();
          for(auto& param : params->children)
            {
              if(param->children.empty())
                continue;
              auto type = buildTypeDesc(param->children[0].get());
              if(!type)
                continue;
              bool isVar = param->isVarParam;
              int sz = isVar ? 4 : typeSize(type.get());

              for(size_t i = 1; i < param->children.size(); ++i)
                {
                  auto* id = param->children[i].get();
                  if(id->kind != NodeKind::SIMPLE_VAR)
                    continue;
                  Symbol sym;
                  sym.name = id->name;
                  sym.kind = isVar ? SymKind::VAR_PARAM : SymKind::PARAM;
                  sym.type = type;
                  sym.isVarParam = isVar;
                  // 参数也用负偏移，和局部变量一样放在 $fp 下方
                  current_->localSize += sz;
                  sym.offset = -(current_->localSize);
                  if(!current_->insert(sym))
                    error("参数名重复：" + id->name, id->line);
                  id->typeInfo = type;
                }
            }
        }

      // 局部声明
      if(proc->children.size() > 1)
        visitDeclarePart(proc->children[1].get());
      // 过程体语句
      if(proc->children.size() > 2)
        visitStmList(proc->children[2].get());

      // 把帧大小存回父作用域的过程符号（offset 字段暂借用）
      int frameSize = current_->localSize;
      if(auto* s = current_->parent->lookupLocal(proc->name))
        s->offset = frameSize;

      currentProcName_ = savedProc;
      popScope();
    }
}

// ── StmList
// ───────────────────────────────────────────────────────────────────
void
SemanticAnalyzer::visitStmList(ASTNode* node)
{
  if(!node)
    return;
  for(auto& stm : node->children)
    visitStm(stm.get());
}

// ── Stm
// ───────────────────────────────────────────────────────────────────────
void
SemanticAnalyzer::visitStm(ASTNode* node)
{
  if(!node)
    return;

  // block 容器
  if(node->name == "__block__")
    {
      visitStmList(node);
      return;
    }

  switch(node->kind)
    {
    case NodeKind::ASSIGN_STM:
      {
        if(node->children.size() < 2)
          break;
        auto lhsType = visitVariable(node->children[0].get());
        auto rhsType = visitExp(node->children[1].get());
        if(lhsType && rhsType && !typeCompatible(lhsType.get(), rhsType.get()))
          error("赋值两侧类型不兼容", node->line);
        break;
      }
    case NodeKind::IF_STM:
      if(node->children.size() >= 3)
        {
          visitExp(node->children[0].get());
          visitStmList(node->children[1].get());
          visitStmList(node->children[2].get());
        }
      break;
    case NodeKind::WHILE_STM:
      if(node->children.size() >= 2)
        {
          visitExp(node->children[0].get());
          visitStmList(node->children[1].get());
        }
      break;
    case NodeKind::READ_STM:
      {
        auto* sym = current_->lookup(node->name);
        if(!sym || sym->kind == SymKind::PROC || sym->kind == SymKind::TYPE)
          error("read 对象不是变量：" + node->name, node->line);
        break;
      }
    case NodeKind::WRITE_STM:
    case NodeKind::RETURN_STM:
      if(!node->children.empty())
        visitExp(node->children[0].get());
      break;
    case NodeKind::CALL_STM:
      {
        auto* sym = current_->lookup(node->name);
        if(!sym || sym->kind != SymKind::PROC)
          error("未定义的过程：" + node->name, node->line);
        for(auto& arg : node->children)
          visitExp(arg.get());
        break;
      }
    default:
      for(auto& c : node->children)
        visitStm(c.get());
      break;
    }
}

// ── Exp
// ───────────────────────────────────────────────────────────────────────
std::shared_ptr<TypeDesc>
SemanticAnalyzer::visitExp(ASTNode* node)
{
  if(!node)
    return nullptr;
  switch(node->kind)
    {
    case NodeKind::INT_LITERAL:
      node->typeInfo = TypeDesc::makeInt();
      return node->typeInfo;
    case NodeKind::BINARY_EXP:
      {
        auto lt = node->children.size() > 0 ? visitExp(node->children[0].get())
                                            : nullptr;
        auto rt = node->children.size() > 1 ? visitExp(node->children[1].get())
                                            : nullptr;
        bool isCmp = (node->name == "<" || node->name == "=");
        if(!isCmp && lt && rt && !typeCompatible(lt.get(), rt.get()))
          error("运算符 '" + node->name + "' 两侧类型不一致", node->line);
        node->typeInfo = TypeDesc::makeInt();
        return node->typeInfo;
      }
    case NodeKind::VAR_EXP:
      return visitVariable(!node->children.empty() ? node->children[0].get()
                                                   : node);
    case NodeKind::SIMPLE_VAR:
      return visitVariable(node);
    default:
      for(auto& c : node->children)
        visitExp(c.get());
      return nullptr;
    }
}

// ── Variable
// ──────────────────────────────────────────────────────────────────
std::shared_ptr<TypeDesc>
SemanticAnalyzer::visitVariable(ASTNode* node)
{
  if(!node)
    return nullptr;

  if(node->kind == NodeKind::SIMPLE_VAR)
    {
      auto* sym = current_->lookup(node->name);
      if(!sym || sym->kind == SymKind::PROC || sym->kind == SymKind::TYPE)
        {
          error("未定义的变量：" + node->name, node->line);
          return nullptr;
        }
      node->typeInfo = sym->type;
      return sym->type;
    }

  if(node->kind == NodeKind::INDEX_VAR)
    {
      auto baseType = !node->children.empty()
                          ? visitVariable(node->children[0].get())
                          : nullptr;
      auto idxType = node->children.size() > 1
                         ? visitExp(node->children[1].get())
                         : nullptr;
      if(!baseType || baseType->kind != TypeKind::ARRAY)
        error("下标访问的对象不是数组", node->line);
      if(idxType && idxType->kind != TypeKind::INTEGER)
        error("数组下标必须是整型", node->line);
      if(baseType && baseType->kind == TypeKind::ARRAY)
        {
          node->typeInfo = baseType->elemType;
          return node->typeInfo;
        }
      return nullptr;
    }

  if(node->kind == NodeKind::FIELD_VAR)
    {
      auto baseType = !node->children.empty()
                          ? visitVariable(node->children[0].get())
                          : nullptr;
      if(!baseType || baseType->kind != TypeKind::RECORD)
        {
          error("字段访问对象不是 record", node->line);
          return nullptr;
        }
      for(auto& f : baseType->fields)
        {
          if(f.name == node->name)
            {
              node->typeInfo = f.type;
              if(node->children.size() > 1)
                {
                  auto idxType = visitExp(node->children[1].get());
                  if(!node->typeInfo
                     || node->typeInfo->kind != TypeKind::ARRAY)
                    error("字段 " + node->name + " 不是数组", node->line);
                  else if(idxType && idxType->kind != TypeKind::INTEGER)
                    error("数组下标必须是整型", node->line);
                  else
                    node->typeInfo = node->typeInfo->elemType;
                }
              return node->typeInfo;
            }
        }
      error("record 中不存在字段：" + node->name, node->line);
      return nullptr;
    }

  return nullptr;
}

// ── 类型兼容性
// ────────────────────────────────────────────────────────────────
bool
SemanticAnalyzer::typeCompatible(const TypeDesc* lhs, const TypeDesc* rhs)
{
  if(!lhs || !rhs)
    return true;
  if(lhs->kind != rhs->kind)
    return false;
  if(lhs->kind == TypeKind::ARRAY)
    return lhs->low == rhs->low && lhs->top == rhs->top
           && typeCompatible(lhs->elemType.get(), rhs->elemType.get());
  if(lhs->kind == TypeKind::RECORD)
    {
      if(lhs->fields.size() != rhs->fields.size())
        return false;
      for(size_t i = 0; i < lhs->fields.size(); ++i)
        if(!typeCompatible(lhs->fields[i].type.get(),
                           rhs->fields[i].type.get()))
          return false;
    }
  return true;
}

std::shared_ptr<TypeDesc>
SemanticAnalyzer::resolveType(const TypeDesc* td)
{
  if(!td || td->kind != TypeKind::NAME)
    return nullptr;
  auto* sym = current_->lookup(td->name);
  return (sym && sym->kind == SymKind::TYPE) ? sym->type : nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 调试：打印符号表
// ═══════════════════════════════════════════════════════════════════════════════

static const char*
symKindName(SymKind k)
{
  switch(k)
    {
    case SymKind::VAR:
      return "var";
    case SymKind::PARAM:
      return "param";
    case SymKind::VAR_PARAM:
      return "var-param";
    case SymKind::PROC:
      return "proc";
    case SymKind::TYPE:
      return "type";
    }
  return "?";
}

static std::string
typeName(const TypeDesc* td)
{
  if(!td)
    return "nil";
  switch(td->kind)
    {
    case TypeKind::INTEGER:
      return "integer";
    case TypeKind::CHAR:
      return "char";
    case TypeKind::NAME:
      return td->name;
    case TypeKind::ARRAY:
      return "array[" + std::to_string(td->low) + ".."
             + std::to_string(td->top) + "] of "
             + typeName(td->elemType.get());
    case TypeKind::RECORD:
      {
        std::string s = "record{";
        for(auto& f : td->fields)
          s += f.name + ":" + typeName(f.type.get()) + " ";
        return s + "}";
      }
    }
  return "?";
}

void
SemanticAnalyzer::dumpScope(Scope* s, int indent) const
{
  if(!s)
    return;
  std::string pad(indent * 2, ' ');
  for(auto& [name, sym] : s->table)
    {
      std::cout << pad << symKindName(sym.kind) << "  " << name
                << "  type=" << typeName(sym.type.get())
                << "  offset=" << sym.offset << "\n";
    }
}

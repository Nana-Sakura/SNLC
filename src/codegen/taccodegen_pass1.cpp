#include <cassert>
#include <stdexcept>
#include <taccodegen.h>

static int
typeSize(const TypeDesc* td)
{
  if(!td)
    return 4;
  switch(td->kind)
    {
    case TypeKind::INTEGER:
      return 4;
    case TypeKind::CHAR:
      return 4;
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
    case TypeKind::NAME:
      return 4;
    }
  return 4;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 工具
// ═══════════════════════════════════════════════════════════════════════════════

TACCodeGen::TACCodeGen(Scope* gs) : global_(gs), current_(gs) {}

std::string
TACCodeGen::newVReg()
{
  return "%" + std::to_string(vregCtr_++);
}
std::string
TACCodeGen::newLabel(const std::string& hint)
{
  return hint + "_" + std::to_string(labelCtr_++);
}

void
TACCodeGen::newBlock(const std::string& label)
{
  blocks_.emplace_back();
  blocks_.back().label = label;
  curBB_ = &blocks_.back();
}

void
TACCodeGen::emit(TACInstr instr)
{
  if(!curBB_)
    newBlock();
  curBB_->instrs.push_back(std::move(instr));
}

// ─── 控制流连接：扫描 JUMP / BRANCH_EQ0 / LABEL，建立 succ/pred
// ──────────────
void
TACCodeGen::linkBlocks()
{
  // 建立 label → block 映射
  std::unordered_map<std::string, BasicBlock*> labelMap;
  for(auto& bb : blocks_)
    if(!bb.label.empty())
      labelMap[bb.label] = &bb;

  for(int i = 0; i < (int)blocks_.size(); ++i)
    {
      auto& bb = blocks_[i];
      if(bb.instrs.empty())
        continue;
      auto& last = bb.instrs.back();

      auto link = [&](BasicBlock* from, BasicBlock* to)
        {
          from->succs.push_back(to);
          to->preds.push_back(from);
        };

      if(last.op == TACOp::JUMP)
        {
          auto it = labelMap.find(last.label);
          if(it != labelMap.end())
            link(&bb, it->second);
        }
      else if(last.op == TACOp::BRANCH_EQ0)
        {
          // fall-through 到下一个块
          if(i + 1 < (int)blocks_.size())
            link(&bb, &blocks_[i + 1]);
          // 跳转目标
          auto it = labelMap.find(last.label);
          if(it != labelMap.end())
            link(&bb, it->second);
        }
      else if(last.op != TACOp::RETURN && last.op != TACOp::SYSCALL)
        {
          // 顺序控制流
          if(i + 1 < (int)blocks_.size())
            link(&bb, &blocks_[i + 1]);
        }
    }
}

// ─── 局部 CSE：仅在基本块内消除重复表达式 ───────────────────────────────
void
TACCodeGen::cseBlocks(std::vector<BasicBlock>& blocks)
{
  auto isCommutative = [](TACOp op)
    { return op == TACOp::ADD || op == TACOp::MUL || op == TACOp::SEQ; };

  auto isCseOp = [](TACOp op)
    {
      return op == TACOp::ADD || op == TACOp::SUB || op == TACOp::MUL
             || op == TACOp::DIV || op == TACOp::SLT || op == TACOp::SEQ
             || op == TACOp::SHL;
    };

  for(auto& bb : blocks)
    {
      std::unordered_map<std::string, std::string> expr2vreg;
      for(auto& instr : bb.instrs)
        {
          if(!isCseOp(instr.op) || instr.dst.empty())
            continue;

          std::string s1 = instr.src1;
          std::string s2 = instr.src2;
          int imm = instr.imm;

          if(isCommutative(instr.op) && s2 < s1)
            std::swap(s1, s2);

          std::string key;
          if(instr.op == TACOp::SHL)
            {
              key = std::to_string((int)instr.op) + "|" + s1 + "|"
                    + std::to_string(imm);
            }
          else
            {
              key = std::to_string((int)instr.op) + "|" + s1 + "|" + s2;
            }

          auto it = expr2vreg.find(key);
          if(it != expr2vreg.end())
            {
              instr = TACInstr::make(TACOp::COPY, instr.dst, it->second);
              continue;
            }

          expr2vreg[key] = instr.dst;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 第一趟：AST → TAC
// ═══════════════════════════════════════════════════════════════════════════════

std::string
TACCodeGen::generate(ASTNode* root)
{
  emitData(".data");
  emitData("__newline: .asciiz \"\\n\"");
  out_ << ".text\n.globl main\n\n";
  // MARS starts at the first .text instruction; jump to main explicitly.
  out_ << "j     main\n";
  out_ << "nop\n\n";
  tacProgram(root);
  return data_.str() + "\n" + out_.str();
}

// ── Program
// ───────────────────────────────────────────────────────────────────
void
TACCodeGen::tacProgram(ASTNode* node)
{
  if(!node || node->children.size() < 3)
    return;
  ASTNode* decl = node->children[1].get();

  // 先生成所有过程
  if(decl && decl->children.size() >= 3)
    tacProcDec(decl->children[2].get());

  // main
  blocks_.clear();
  vregCtr_ = 0;
  newBlock("main");
  Scope* saved = current_;
  current_ = global_;
  tacStmList(node->children[2].get());
  // exit syscall
  emit(TACInstr::make(TACOp::LOAD_IMM, "$v0", "", "", 10));
  emit(TACInstr::make(TACOp::SYSCALL, "", "", "", 10));
  current_ = saved;

  linkBlocks();
  emitProc("main", global_->localSize, blocks_);
}

// ── ProcDec
// ───────────────────────────────────────────────────────────────────
void
TACCodeGen::tacProcDec(ASTNode* node)
{
  if(!node)
    return;
  for(auto& proc : node->children)
    {
      // 切换到过程作用域
      Scope* procScope = nullptr;
      auto it = procScopeMap_.find(proc->name);
      if(it != procScopeMap_.end())
        procScope = it->second;

      blocks_.clear();
      vregCtr_ = 0;
      newBlock(proc->name);

      Scope* saved = current_;
      if(procScope)
        current_ = procScope;

      // 把 $a0-$a3 存到参数槽（与 CodeGen 保持一致）
      auto pit = procParamOrder_.find(proc->name);
      if(pit != procParamOrder_.end())
        {
          static const char* ar[] = { "$a0", "$a1", "$a2", "$a3" };
          int ai = 0;
          for(Symbol* sym : pit->second)
            {
              if(ai >= 4)
                break;
              // STORE ar[ai] → sym->offset($fp)
              // 用特殊标记 "$fp" 作 base，imm = offset
              emit(TACInstr::make(TACOp::STORE, "", ar[ai], "$fp", sym->offset,
                                  "# save_arg " + sym->name));
              ++ai;
            }
        }

      if(proc->children.size() > 2)
        tacStmList(proc->children[2].get());

      current_ = saved;
      linkBlocks();

      int localBytes = procScope ? procScope->localSize : 0;
      emitProc(proc->name, localBytes, blocks_);
    }
}

// ── StmList
// ───────────────────────────────────────────────────────────────────
void
TACCodeGen::tacStmList(ASTNode* node)
{
  if(!node)
    return;
  for(auto& stm : node->children)
    tacStm(stm.get());
}

// ── Stm
// ───────────────────────────────────────────────────────────────────────
void
TACCodeGen::tacStm(ASTNode* node)
{
  if(!node)
    return;
  if(node->name == "__block__")
    {
      tacStmList(node);
      return;
    }

  switch(node->kind)
    {

    // ── 赋值 ──────────────────────────────────────────────────────────
    case NodeKind::ASSIGN_STM:
      {
        if(node->children.size() < 2)
          break;
        std::string rhs = tacExp(node->children[1].get());
        tacVarStore(node->children[0].get(), rhs);
        break;
      }

    // ── 过程调用 ──────────────────────────────────────────────────────
    case NodeKind::CALL_STM:
      {
        auto pit = procParamOrder_.find(node->name);
        int ai = 0;
        static const char* ar[] = { "$a0", "$a1", "$a2", "$a3" };
        for(auto& arg : node->children)
          {
            bool isVar = pit != procParamOrder_.end()
                         && ai < (int)pit->second.size()
                         && pit->second[ai]->isVarParam;
            std::string val;
            if(isVar)
              {
                val = newVReg();
                tacLoadAddr(arg.get(), val);
              }
            else
              {
                val = tacExp(arg.get());
              }
            if(ai < 4)
              {
                emit(TACInstr::make(TACOp::COPY, ar[ai], val));
              }
            else
              {
                emit(TACInstr::make(TACOp::PARAM, "", val));
              }
            ++ai;
          }
        emit(TACInstr::make(TACOp::CALL, "", node->name, "",
                            (int)node->children.size()));
        break;
      }

    // ── if ────────────────────────────────────────────────────────────
    case NodeKind::IF_STM:
      {
        if(node->children.size() < 3)
          break;
        std::string cond = tacRelExp(node->children[0].get());
        std::string lElse = newLabel("else");
        std::string lEnd = newLabel("fi");
        emit(TACInstr::make(TACOp::BRANCH_EQ0, "", cond, "", 0, lElse));
        newBlock(lElse + "_thenentry");
        tacStmList(node->children[1].get());
        emit(TACInstr::make(TACOp::JUMP, "", "", "", 0, lEnd));
        newBlock(lElse);
        tacStmList(node->children[2].get());
        newBlock(lEnd);
        break;
      }

    // ── while ─────────────────────────────────────────────────────────
    case NodeKind::WHILE_STM:
      {
        if(node->children.size() < 2)
          break;
        std::string lTop = newLabel("while");
        std::string lEnd = newLabel("endwh");
        newBlock(lTop);
        std::string cond = tacRelExp(node->children[0].get());
        emit(TACInstr::make(TACOp::BRANCH_EQ0, "", cond, "", 0, lEnd));
        newBlock(lTop + "_body");
        tacStmList(node->children[1].get());
        emit(TACInstr::make(TACOp::JUMP, "", "", "", 0, lTop));
        newBlock(lEnd);
        break;
      }

    // ── read ──────────────────────────────────────────────────────────
    case NodeKind::READ_STM:
      {
        emit(TACInstr::make(TACOp::LOAD_IMM, "$v0", "", "", 5));
        emit(TACInstr::make(TACOp::SYSCALL, "", "", "", 5));
        // 把 $v0 存入变量
        Symbol* sym = current_->lookup(node->name);
        if(sym)
          {
            emit(TACInstr::make(TACOp::STORE, "", "$v0", "$fp", sym->offset));
          }
        break;
      }

    // ── write ─────────────────────────────────────────────────────────
    case NodeKind::WRITE_STM:
      {
        if(node->children.empty())
          break;
        std::string val = tacExp(node->children[0].get());
        emit(TACInstr::make(TACOp::COPY, "$a0", val));
        emit(TACInstr::make(TACOp::LOAD_IMM, "$v0", "", "", 1));
        emit(TACInstr::make(TACOp::SYSCALL, "", "", "", 1));
        // 打印换行
        emit(TACInstr::make(TACOp::LOAD_IMM, "$v0", "", "", 4));
        emit(TACInstr::make(TACOp::SYSCALL, "", "", "", 4));
        break;
      }

    // ── return ────────────────────────────────────────────────────────
    case NodeKind::RETURN_STM:
      {
        if(!node->children.empty())
          {
            std::string val = tacExp(node->children[0].get());
            emit(TACInstr::make(TACOp::COPY, "$v0", val));
          }
        emit(TACInstr::make(TACOp::RETURN));
        newBlock(newLabel("after_ret"));
        break;
      }

    default:
      for(auto& c : node->children)
        tacStm(c.get());
      break;
    }
}

// ── 表达式 → vreg
// ─────────────────────────────────────────────────────────────
std::string
TACCodeGen::tacExp(ASTNode* node)
{
  if(!node)
    return "%zero";

  switch(node->kind)
    {
    case NodeKind::INT_LITERAL:
      {
        std::string d = newVReg();
        emit(TACInstr::make(TACOp::LOAD_IMM, d, "", "", node->ival));
        return d;
      }

    case NodeKind::BINARY_EXP:
      {
        if(node->children.size() < 2)
          return newVReg();
        std::string l = tacExp(node->children[0].get());
        std::string r = tacExp(node->children[1].get());
        std::string d = newVReg();
        TACOp op;
        const std::string& name = node->name;
        if(name == "+")
          op = TACOp::ADD;
        else if(name == "-")
          op = TACOp::SUB;
        else if(name == "*")
          op = TACOp::MUL;
        else if(name == "/")
          op = TACOp::DIV;
        else if(name == "<")
          op = TACOp::SLT;
        else if(name == "=")
          op = TACOp::SEQ;
        else
          op = TACOp::ADD;
        emit(TACInstr::make(op, d, l, r));
        return d;
      }

    case NodeKind::VAR_EXP:
      if(!node->children.empty())
        return tacExp(node->children[0].get());
      return newVReg();

    case NodeKind::SIMPLE_VAR:
    case NodeKind::INDEX_VAR:
    case NodeKind::FIELD_VAR:
      return tacVarLoad(node);

    default:
      return newVReg();
    }
}

// RelExp 与普通 Exp 用同一套机制（BINARY_EXP 处理 < =）
std::string
TACCodeGen::tacRelExp(ASTNode* node)
{
  return tacExp(node);
}

// ── 读变量值
// ──────────────────────────────────────────────────────────────────
std::string
TACCodeGen::tacVarLoad(ASTNode* node)
{
  if(!node)
    return newVReg();

  if(node->kind == NodeKind::SIMPLE_VAR)
    {
      Symbol* sym = current_->lookup(node->name);
      if(!sym)
        {
          auto d = newVReg();
          emit(TACInstr::make(TACOp::LOAD_IMM, d, "", "", 0));
          return d;
        }
      std::string d = newVReg();
      if(sym->isVarParam)
        {
          // 先取指针，再解引用
          std::string ptr = newVReg();
          emit(TACInstr::make(TACOp::LOAD, ptr, "$fp", "", sym->offset));
          emit(TACInstr::make(TACOp::LOAD, d, ptr, "", 0));
        }
      else
        {
          emit(TACInstr::make(TACOp::LOAD, d, "$fp", "", sym->offset));
        }
      return d;
    }

  if(node->kind == NodeKind::INDEX_VAR)
    {
      std::string addr = tacIndexAddr(node);
      std::string d = newVReg();
      emit(TACInstr::make(TACOp::LOAD, d, addr, "", 0));
      return d;
    }

  if(node->kind == NodeKind::FIELD_VAR)
    {
      std::string addr = tacFieldAddr(node);
      std::string d = newVReg();
      emit(TACInstr::make(TACOp::LOAD, d, addr, "", 0));
      return d;
    }

  return newVReg();
}

// ── 写变量
// ────────────────────────────────────────────────────────────────────
void
TACCodeGen::tacVarStore(ASTNode* node, const std::string& src)
{
  if(!node)
    return;
  // 跳过 VAR_EXP 包装
  ASTNode* inner = (node->kind == NodeKind::VAR_EXP && !node->children.empty())
                       ? node->children[0].get()
                       : node;

  if(inner->kind == NodeKind::SIMPLE_VAR)
    {
      Symbol* sym = current_->lookup(inner->name);
      if(!sym)
        return;
      if(sym->isVarParam)
        {
          std::string ptr = newVReg();
          emit(TACInstr::make(TACOp::LOAD, ptr, "$fp", "", sym->offset));
          emit(TACInstr::make(TACOp::STORE, "", src, ptr, 0));
        }
      else
        {
          emit(TACInstr::make(TACOp::STORE, "", src, "$fp", sym->offset));
        }
      return;
    }

  if(inner->kind == NodeKind::INDEX_VAR)
    {
      std::string addr = tacIndexAddr(inner);
      emit(TACInstr::make(TACOp::STORE, "", src, addr, 0));
      return;
    }

  if(inner->kind == NodeKind::FIELD_VAR)
    {
      std::string addr = tacFieldAddr(inner);
      emit(TACInstr::make(TACOp::STORE, "", src, addr, 0));
      return;
    }
}

// ── 取变量地址（VAR 参数传递用）─────────────────────────────────────────────
void
TACCodeGen::tacLoadAddr(ASTNode* node, const std::string& dst)
{
  if(!node)
    return;
  ASTNode* inner = (node->kind == NodeKind::VAR_EXP && !node->children.empty())
                       ? node->children[0].get()
                       : node;
  if(inner->kind == NodeKind::SIMPLE_VAR)
    {
      Symbol* sym = current_->lookup(inner->name);
      if(!sym)
        return;
      if(sym->isVarParam)
        {
          // 已是指针，直接加载指针值
          emit(TACInstr::make(TACOp::LOAD, dst, "$fp", "", sym->offset));
        }
      else
        {
          emit(TACInstr::make(TACOp::LOAD_ADDR, dst, "$fp", "", sym->offset));
        }
    }
  else if(inner->kind == NodeKind::INDEX_VAR)
    {
      std::string addr = tacIndexAddr(inner);
      emit(TACInstr::make(TACOp::COPY, dst, addr));
    }
  else if(inner->kind == NodeKind::FIELD_VAR)
    {
      std::string addr = tacFieldAddr(inner);
      emit(TACInstr::make(TACOp::COPY, dst, addr));
    }
}

// ── 数组地址计算
// ──────────────────────────────────────────────────────────────
std::string
TACCodeGen::tacIndexAddr(ASTNode* node)
{
  if(node->children.size() < 2)
    return newVReg();
  ASTNode* base = node->children[0].get();
  ASTNode* index = node->children[1].get();

  // base 地址
  ASTNode* b = (base->kind == NodeKind::VAR_EXP && !base->children.empty())
                   ? base->children[0].get()
                   : base;
  Symbol* sym
      = b->kind == NodeKind::SIMPLE_VAR ? current_->lookup(b->name) : nullptr;

  std::string baseAddr = newVReg();
  if(sym)
    {
      if(sym->isVarParam)
        {
          emit(TACInstr::make(TACOp::LOAD, baseAddr, "$fp", "", sym->offset));
        }
      else
        {
          emit(TACInstr::make(TACOp::LOAD_ADDR, baseAddr, "$fp", "",
                              sym->offset));
        }
    }
  else
    {
      emit(TACInstr::make(TACOp::LOAD_IMM, baseAddr, "", "", 0));
    }

  // index 值
  std::string idx = tacExp(index);

  // index - low
  if(sym && sym->type && sym->type->kind == TypeKind::ARRAY
     && sym->type->low != 0)
    {
      std::string low = newVReg();
      emit(TACInstr::make(TACOp::LOAD_IMM, low, "", "", sym->type->low));
      std::string adj = newVReg();
      emit(TACInstr::make(TACOp::SUB, adj, idx, low));
      idx = adj;
    }

  // byte offset = index * 4
  std::string off = newVReg();
  emit(TACInstr::make(TACOp::SHL, off, idx, "", 2));

  // addr = baseAddr + off
  std::string addr = newVReg();
  emit(TACInstr::make(TACOp::ADD, addr, baseAddr, off));
  return addr;
}

// ── record 字段地址计算
// ───────────────────────────────────────────────────────
std::string
TACCodeGen::tacFieldAddr(ASTNode* node)
{
  if(node->children.empty())
    return newVReg();
  ASTNode* base = node->children[0].get();
  ASTNode* b = (base->kind == NodeKind::VAR_EXP && !base->children.empty())
                   ? base->children[0].get()
                   : base;
  Symbol* sym
      = b->kind == NodeKind::SIMPLE_VAR ? current_->lookup(b->name) : nullptr;

  // 字段偏移
  int fieldOff = 0;
  if(sym && sym->type && sym->type->kind == TypeKind::RECORD)
    {
      for(auto& f : sym->type->fields)
        {
          if(f.name == node->name)
            break;
            fieldOff += typeSize(f.type.get());
        }
    }

  std::string addr = newVReg();
  if(sym)
    {
      int totalOff = sym->offset + fieldOff;
      if(sym->isVarParam)
        {
          // 先取指针，再加字段偏移
          std::string ptr = newVReg();
          emit(TACInstr::make(TACOp::LOAD, ptr, "$fp", "", sym->offset));
          if(fieldOff == 0)
            {
              addr = ptr;
            }
          else
            {
              std::string fv = newVReg();
              emit(TACInstr::make(TACOp::LOAD_IMM, fv, "", "", fieldOff));
              emit(TACInstr::make(TACOp::ADD, addr, ptr, fv));
            }
        }
      else
        {
          emit(TACInstr::make(TACOp::LOAD_ADDR, addr, "$fp", "", totalOff));
        }
    }
  else
    {
      emit(TACInstr::make(TACOp::LOAD_IMM, addr, "", "", 0));
    }

  // FieldVarMore（字段本身是数组）
  if(node->children.size() > 1)
    {
      std::string idx = tacExp(node->children[1].get());
      std::string off = newVReg();
      emit(TACInstr::make(TACOp::SHL, off, idx, "", 2));
      std::string newAddr = newVReg();
      emit(TACInstr::make(TACOp::ADD, newAddr, addr, off));
      addr = newAddr;
    }
  return addr;
}

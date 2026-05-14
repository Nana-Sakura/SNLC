#include <cassert>
#include <sstream>
#include <taccodegen.h>

// ═══════════════════════════════════════════════════════════════════════════════
// 过程发射入口：TAC → 图着色 → MIPS 汇编
// ═══════════════════════════════════════════════════════════════════════════════
void
TACCodeGen::emitProc(const std::string& procLabel, int localBytes,
                     std::vector<BasicBlock>& blocks)
{
  // 0. 基本块内公共子表达式消除
  cseBlocks(blocks);

  // 1. 图着色寄存器分配
  RegAllocator ra;
  allocation_ = ra.allocate(blocks);
  totalSpills_ += (int)allocation_.spillSlots.size();

  // 2. 计算帧大小（localBytes + spill 槽）
  int fsize = frameSize(localBytes, allocation_.spillBytes);
  currentFrameSize_ = fsize;
  currentProcLabel_ = procLabel;

  // spill 槽在 $fp 之下，紧接局部变量
  // spillBase_：spill 变量的 offset 是 allocation_.spillSlots[v]（负值），
  // 但这些 offset 是相对于 spill 区起点的，需要加上局部变量区的偏移
  // 约定：spill 槽 offset 直接使用 allocation_.spillSlots[v]（已经是相对 $fp
  // 的负值） 为了不与局部变量重叠，把 spill 槽放在 localBytes 之后：
  //   实际 $fp 偏移 = allocation_.spillSlots[v] - localBytes
  spillBase_ = -localBytes; // spill 区从这里往下

  // 3. 发射
  emitLbl(procLabel);
  emitPrologue(fsize);
  bool firstBlock = true;
  for(auto& bb : blocks)
    {
      if(!bb.label.empty())
        {
          // 第一个块的标签就是过程名，已在 emitProc 开头发射过，跳过
          if(!(firstBlock && bb.label == procLabel))
            emitLbl(bb.label);
        }
      firstBlock = false;
      for(auto& instr : bb.instrs)
        {
          emitInstr(instr);
        }
    }
  emitEpilogue(fsize);
  out_ << "\n";
}

// ─── prologue / epilogue
// ──────────────────────────────────────────────────────
void
TACCodeGen::emitPrologue(int fsize)
{
  emitCmt("prologue  frame=" + std::to_string(fsize));
  emitLine("addiu $sp, $sp, -" + std::to_string(fsize));
  emitLine("sw    $ra, " + std::to_string(fsize - 4) + "($sp)");
  emitLine("sw    $fp, " + std::to_string(fsize - 8) + "($sp)");
  emitLine("addiu $fp, $sp, " + std::to_string(fsize - 4));
}

void
TACCodeGen::emitEpilogue(int fsize)
{
  emitCmt("epilogue");
  emitLbl("__ret_" + currentProcLabel_);
  emitLine("lw    $ra, " + std::to_string(fsize - 4) + "($sp)");
  emitLine("lw    $fp, " + std::to_string(fsize - 8) + "($sp)");
  emitLine("addiu $sp, $sp, " + std::to_string(fsize));
  emitLine("jr    $ra");
  emitLine("nop");
}

// ─── 虚拟寄存器解析
// ─────────────────────────────────────────────────────────── 如果 vreg
// 被分配了物理寄存器 → 直接返回物理寄存器名 如果 vreg 被 spill → 从栈槽 load
// 到 tmp，返回 tmp 如果 vreg 是物理寄存器（$v0, $a0...）或 $fp/$sp → 直接返回
std::string
TACCodeGen::resolve(const std::string& vreg, bool load, const std::string& tmp)
{
  if(vreg.empty() || vreg == "$zero")
    return "$zero";
  // 已经是物理寄存器
  if(vreg[0] == '$')
    return vreg;
  // 虚拟寄存器
  auto cit = allocation_.coloring.find(vreg);
  if(cit != allocation_.coloring.end() && !cit->second.empty())
    return cit->second; // 分配到物理寄存器

  // Spill：从栈槽加载
  auto sit = allocation_.spillSlots.find(vreg);
  if(sit != allocation_.spillSlots.end())
    {
      int off = spillBase_ + sit->second; // 相对 $fp 的偏移
      if(load)
        emitLine("lw    " + tmp + ", " + std::to_string(off)
                 + "($fp)  # spill load " + vreg);
      return tmp;
    }
  // 未分配（不应发生）
  return tmp;
}

// 如果 vreg 是 spill 变量，把 tmp 中的值写回栈槽
void
TACCodeGen::writeback(const std::string& vreg, const std::string& tmp)
{
  if(vreg.empty() || vreg[0] == '$')
    return;
  auto cit = allocation_.coloring.find(vreg);
  if(cit != allocation_.coloring.end() && !cit->second.empty())
    return; // 已在寄存器，无需回写

  auto sit = allocation_.spillSlots.find(vreg);
  if(sit != allocation_.spillSlots.end())
    {
      int off = spillBase_ + sit->second;
      emitLine("sw    " + tmp + ", " + std::to_string(off)
               + "($fp)  # spill store " + vreg);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 逐条 TAC 指令发射
// ═══════════════════════════════════════════════════════════════════════════════
void
TACCodeGen::emitInstr(const TACInstr& I)
{
  switch(I.op)
    {

    // ── LOAD_IMM: dst = imm ────────────────────────────────────────────
    case TACOp::LOAD_IMM:
      {
        std::string d = resolve(I.dst, false, "$t8");
        emitLine("li    " + d + ", " + std::to_string(I.imm));
        writeback(I.dst, d);
        break;
      }

    // ── COPY: dst = src ────────────────────────────────────────────────
    case TACOp::COPY:
      {
        std::string s = resolve(I.src1, true, "$t9");
        std::string d = resolve(I.dst, false, "$t8");
        if(s != d)
          emitLine("move  " + d + ", " + s);
        writeback(I.dst, d);
        break;
      }

    // ── LOAD: dst = mem[base + off] ────────────────────────────────────
    case TACOp::LOAD:
      {
        std::string base = resolve(I.src1, true, "$t9");
        std::string d = resolve(I.dst, false, "$t8");
        emitLine("lw    " + d + ", " + std::to_string(I.imm) + "(" + base
                 + ")");
        writeback(I.dst, d);
        break;
      }

    // ── STORE: mem[base + off] = src ───────────────────────────────────
    case TACOp::STORE:
      {
        // src1=src, src2=base, imm=offset
        // 注意：src 和 base 不能同时占 $t8/$t9，分开加载
        std::string src = resolve(I.src1, true, "$t9");
        std::string base = resolve(I.src2, true, "$t8");
        // 如果 base 是 $fp 之类的物理寄存器则不用 load
        emitLine("sw    " + src + ", " + std::to_string(I.imm) + "(" + base
                 + ")");
        break;
      }

    // ── LOAD_ADDR: dst = base + off ────────────────────────────────────
    case TACOp::LOAD_ADDR:
      {
        std::string base = resolve(I.src1, true, "$t9");
        std::string d = resolve(I.dst, false, "$t8");
        emitLine("addiu " + d + ", " + base + ", " + std::to_string(I.imm));
        writeback(I.dst, d);
        break;
      }

    // ── 算术运算 ──────────────────────────────────────────────────────
    case TACOp::ADD:
    case TACOp::SUB:
    case TACOp::MUL:
    case TACOp::DIV:
    case TACOp::SLT:
    case TACOp::SEQ:
    case TACOp::SHL:
      {
        std::string l = resolve(I.src1, true, "$t8");
        std::string d = resolve(I.dst, false, "$t8");
        std::string r;
        if(I.op != TACOp::SHL)
          r = resolve(I.src2, true, "$t9");
        // l 和 d 可能都是 $t8，r 先算好在 $t9 里，所以顺序没问题
        switch(I.op)
          {
          case TACOp::ADD:
            emitLine("add   " + d + ", " + l + ", " + r);
            break;
          case TACOp::SUB:
            emitLine("sub   " + d + ", " + l + ", " + r);
            break;
          case TACOp::MUL:
            emitLine("mul   " + d + ", " + l + ", " + r);
            break;
          case TACOp::DIV:
            emitLine("div   " + l + ", " + r);
            emitLine("mflo  " + d);
            break;
          case TACOp::SLT:
            emitLine("slt   " + d + ", " + l + ", " + r);
            break;
          case TACOp::SEQ:
            emitLine("xor   " + d + ", " + l + ", " + r);
            emitLine("sltiu " + d + ", " + d + ", 1");
            break;
          case TACOp::SHL:
            emitLine("sll   " + d + ", " + l + ", " + std::to_string(I.imm));
            break;
          default:
            break;
          }
        writeback(I.dst, d);
        break;
      }

    // ── 控制流 ────────────────────────────────────────────────────────
    case TACOp::JUMP:
      emitLine("j     " + I.label);
      emitLine("nop");
      break;

    case TACOp::BRANCH_EQ0:
      {
        std::string cond = resolve(I.src1, true, "$t8");
        emitLine("beqz  " + cond + ", " + I.label);
        emitLine("nop");
        break;
      }

    case TACOp::LABEL:
      emitLbl(I.label);
      break;

    // ── 调用 ──────────────────────────────────────────────────────────
    case TACOp::CALL:
      emitLine("jal   " + I.src1);
      emitLine("nop");
      if(I.imm > 4)
        emitLine("addiu $sp, $sp, " + std::to_string((I.imm - 4) * 4));
      break;

    case TACOp::RETURN:
      emitLine("j     __ret_" + currentProcLabel_);
      emitLine("nop");
      break;

    // ── syscall ───────────────────────────────────────────────────────
    case TACOp::SYSCALL:
      if(I.imm == 4)
        {
          // print string (newline)
          emitLine("la    $a0, __newline");
        }
      emitLine("syscall");
      if(I.imm == 10)
        {
          // exit：紧跟着的 epilogue 不再执行，直接 jr $ra
          // syscall 10 是退出，不需要额外处理
        }
      break;

    case TACOp::PARAM:
      // 超出 $a0-$a3 的参数压栈（已在 CALL_STM 处理为 COPY 到 $a0-$a3）
      {
        std::string s = resolve(I.src1, true, "$t8");
        emitLine("addiu $sp, $sp, -4");
        emitLine("sw    " + s + ", 0($sp)");
      }
      break;

    case TACOp::NOP:
    default:
      break;
    }
}

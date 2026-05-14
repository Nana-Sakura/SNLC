#pragma once
#include <algorithm>
#include <ast.h>
#include <regalloc.h>
#include <semantic.h>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// ─── 基于 TAC + 图着色寄存器分配的 MIPS32 代码生成器
// ─────────────────────────
//
//  两趟架构：
//    第一趟：AST → TAC（三地址码，使用无限虚拟寄存器 %0, %1, ...）
//    图着色：虚拟寄存器 → 物理寄存器（$t0-$t7）或 spill 到栈
//    第二趟：TAC + 着色结果 → MIPS32 汇编文本
//
class TACCodeGen
{
public:
  explicit TACCodeGen(Scope* globalScope);

  void
  registerProcScope(const std::string& name, Scope* scope)
  {
    procScopeMap_[name] = scope;
  }
  void
  registerProcParamOrder(const std::string& name,
                         const std::vector<Symbol*>& order)
  {
    procParamOrder_[name] = order;
  }

  std::string generate(ASTNode* root);

  // 统计：spill 次数
  int
  spillCount() const
  {
    return totalSpills_;
  }

private:
  Scope* global_;
  Scope* current_;
  std::unordered_map<std::string, Scope*> procScopeMap_;
  std::unordered_map<std::string, std::vector<Symbol*> > procParamOrder_;

  // ── 第一趟：TAC 生成 ─────────────────────────────────────────────────
  std::vector<BasicBlock> blocks_; // 当前过程/main 的基本块列表
  BasicBlock* curBB_ = nullptr;    // 当前基本块
  int vregCtr_ = 0;                // 虚拟寄存器计数
  int labelCtr_ = 0;

  std::string newVReg(); // 分配新虚拟寄存器 "%N"
  std::string newLabel(const std::string& hint = "L");
  void newBlock(const std::string& label = "");
  void emit(TACInstr instr); // 向当前基本块追加指令

  // 连接基本块控制流
  void linkBlocks();
  void cseBlocks(std::vector<BasicBlock>& blocks);

  // AST → TAC
  void tacProgram(ASTNode* node);
  void tacProcDec(ASTNode* node);
  void tacStmList(ASTNode* node);
  void tacStm(ASTNode* node);
  std::string tacExp(ASTNode* node); // 返回存结果的虚拟寄存器名
  std::string tacRelExp(ASTNode* node);
  std::string tacVarLoad(ASTNode* node);                   // 读变量值 → vreg
  void tacVarStore(ASTNode* node, const std::string& src); // 写变量
  void tacLoadAddr(ASTNode* node, const std::string& dst); // 取地址
  std::string tacIndexAddr(ASTNode* node);                 // 数组地址 → vreg
  std::string tacFieldAddr(ASTNode* node);                 // 字段地址 → vreg

  // ── 第二趟：TAC + 着色 → MIPS 发射 ─────────────────────────────────
  std::ostringstream out_;
  std::ostringstream data_;

  // 当前过程的寄存器分配结果
  RegAllocator::Result allocation_;
  int spillBase_ = 0; // spill 槽从 $fp 往下的起始偏移

  void
  emitLine(const std::string& s)
  {
    out_ << "    " << s << "\n";
  }
  void
  emitLbl(const std::string& l)
  {
    out_ << l << ":\n";
  }
  void
  emitCmt(const std::string& c)
  {
    out_ << "    # " << c << "\n";
  }
  void
  emitData(const std::string& s)
  {
    data_ << s << "\n";
  }

  // 把虚拟寄存器解析为物理寄存器，若 spill 则用 $t8 作为临时载体
  // load=true 表示在使用前先从 spill 槽加载到 $t8
  std::string resolve(const std::string& vreg, bool load = true,
                      const std::string& tmp = "$t8");

  // 把虚拟寄存器的值写回 spill 槽（如果它是 spill 变量）
  void writeback(const std::string& vreg, const std::string& tmp = "$t8");

  // 生成一个过程/main 的汇编
  void emitProc(const std::string& procLabel, int localBytes,
                std::vector<BasicBlock>& blocks);

  // 逐条 TAC 指令发射
  void emitInstr(const TACInstr& instr);

  // 帧大小计算
  static int
  frameSize(int localBytes, int extraSpill)
  {
    int total = 8 + localBytes + extraSpill;
    return (total + 7) & ~7;
  }

  int totalSpills_ = 0;
  std::string currentProcLabel_;
  int currentFrameSize_ = 0;

  // 过程 prologue/epilogue
  void emitPrologue(int fsize);
  void emitEpilogue(int fsize);
};

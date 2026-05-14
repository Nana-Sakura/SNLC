#pragma once
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ─── 虚拟寄存器名格式：%0, %1, %2, ...
// ─── 物理寄存器池：$t0-$t7（8 个）
// ─── $t8/$t9 保留给 CodeGen 内部临时用，不参与分配

// ═══════════════════════════════════════════════════════════════════════════════
// 三地址指令（TAC）—— 寄存器分配的中间表示
// ═══════════════════════════════════════════════════════════════════════════════
enum class TACOp
{
  // 算术
  ADD,
  SUB,
  MUL,
  DIV,
  SHL,
  // 比较（结果 0/1）
  SLT,
  SEQ,
  // 数据移动
  COPY,      // dst = src
  LOAD_IMM,  // dst = imm
  LOAD,      // dst = mem[base + off]
  STORE,     // mem[base + off] = src
  LOAD_ADDR, // dst = &(base + off)
             // 控制流
  LABEL,
  JUMP,       // goto label
  BRANCH_EQ0, // if src == 0 goto label
              // 调用约定
  PARAM,      // param src  （设置参数）
  CALL,       // dst = call label, nargs
  RETURN,     // return src
              // 系统调用
  SYSCALL,    // syscall imm
  NOP
};

struct TACInstr
{
  TACOp op;
  std::string dst;   // 目标（虚拟寄存器名 "%N" 或 "$xx" 或 ""）
  std::string src1;  // 操作数1
  std::string src2;  // 操作数2（部分指令用）
  int imm = 0;       // 立即数（LOAD_IMM / SYSCALL / LOAD / STORE 的偏移）
  std::string label; // LABEL / JUMP / BRANCH 目标

  // 工厂函数
  static TACInstr make(TACOp op, const std::string& dst = "",
                       const std::string& src1 = "",
                       const std::string& src2 = "", int imm = 0,
                       const std::string& label = "");
};

// ═══════════════════════════════════════════════════════════════════════════════
// 基本块
// ═══════════════════════════════════════════════════════════════════════════════
struct BasicBlock
{
  std::string label; // 入口标签（可能为空）
  std::vector<TACInstr> instrs;

  // 活跃变量分析结果
  std::unordered_set<std::string> liveIn;
  std::unordered_set<std::string> liveOut;
  std::unordered_set<std::string> def; // 本块定义的变量
  std::unordered_set<std::string> use; // 本块先用后定义的变量

  // 控制流
  std::vector<BasicBlock*> succs;
  std::vector<BasicBlock*> preds;
};

// ═══════════════════════════════════════════════════════════════════════════════
// 干涉图
// ═══════════════════════════════════════════════════════════════════════════════
struct InterferenceGraph
{
  // 邻接表（虚拟寄存器名 → 干涉的邻居集合）
  std::unordered_map<std::string, std::unordered_set<std::string> > adj;

  void
  addEdge(const std::string& a, const std::string& b)
  {
    if(a == b)
      return;
    adj[a].insert(b);
    adj[b].insert(a);
  }
  void
  addNode(const std::string& a)
  {
    adj.emplace(a, std::unordered_set<std::string>{});
  }
  int
  degree(const std::string& a) const
  {
    auto it = adj.find(a);
    return it != adj.end() ? (int)it->second.size() : 0;
  }
};

// ═══════════════════════════════════════════════════════════════════════════════
// 图着色寄存器分配器
// ═══════════════════════════════════════════════════════════════════════════════
class RegAllocator
{
public:
  static constexpr int K = 8; // $t0-$t7
  static const char* PHYS[K]; // "$t0"..."$t7"

  // 分配入口：输入 TAC 指令序列，返回
  //   coloring: 虚拟寄存器 → 物理寄存器（或 "" 表示已 spill）
  //   spillSlots: 虚拟寄存器 → spill 栈偏移（相对 $fp，由调用者分配）
  struct Result
  {
    std::unordered_map<std::string, std::string> coloring; // vreg → "$tN"
    std::unordered_map<std::string, int> spillSlots;       // vreg → offset
    int spillBytes = 0; // 需要追加到栈帧的字节数
  };

  Result allocate(std::vector<BasicBlock>& blocks);

private:
  // 活跃变量分析（计算每个基本块的 liveIn/liveOut）
  void livenessAnalysis(std::vector<BasicBlock>& blocks);

  // 构建干涉图
  InterferenceGraph buildInterferenceGraph(std::vector<BasicBlock>& blocks);

  // Chaitin-Briggs 简化 + 着色
  // 返回 vreg → 物理寄存器映射；spill 的 vreg 值为 ""
  std::unordered_map<std::string, std::string>
  colorGraph(InterferenceGraph& ig, const std::vector<std::string>& vregs);

  // 判断是否是虚拟寄存器（以 % 开头）
  static bool
  isVReg(const std::string& s)
  {
    return !s.empty() && s[0] == '%';
  }

  // 获取指令的定义集合和使用集合
  static std::vector<std::string> getDefs(const TACInstr& i);
  static std::vector<std::string> getUses(const TACInstr& i);
};

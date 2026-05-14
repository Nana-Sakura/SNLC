#include <algorithm>
#include <cassert>
#include <regalloc.h>
#include <stack>

// ─── 物理寄存器池
// ─────────────────────────────────────────────────────────────
const char* RegAllocator::PHYS[RegAllocator::K]
    = { "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7" };

// ─── TACInstr 工厂
// ────────────────────────────────────────────────────────────
TACInstr
TACInstr::make(TACOp op, const std::string& dst, const std::string& src1,
               const std::string& src2, int imm, const std::string& label)
{
  TACInstr i;
  i.op = op;
  i.dst = dst;
  i.src1 = src1;
  i.src2 = src2;
  i.imm = imm;
  i.label = label;
  return i;
}

// ─── def / use 集合
// ───────────────────────────────────────────────────────────
std::vector<std::string>
RegAllocator::getDefs(const TACInstr& i)
{
  std::vector<std::string> d;
  if(!i.dst.empty() && isVReg(i.dst))
    d.push_back(i.dst);
  return d;
}

std::vector<std::string>
RegAllocator::getUses(const TACInstr& i)
{
  std::vector<std::string> u;
  auto add = [&](const std::string& s)
    {
      if(isVReg(s))
        u.push_back(s);
    };
  switch(i.op)
    {
    case TACOp::ADD:
    case TACOp::SUB:
    case TACOp::MUL:
    case TACOp::DIV:
    case TACOp::SLT:
    case TACOp::SEQ:
    case TACOp::SHL:
      add(i.src1);
      add(i.src2);
      break;
    case TACOp::COPY:
    case TACOp::BRANCH_EQ0:
    case TACOp::PARAM:
    case TACOp::RETURN:
      add(i.src1);
      break;
    case TACOp::LOAD:
      add(i.src1);
      break; // base
    case TACOp::STORE:
      add(i.src1);
      add(i.src2);
      break; // src, base
    case TACOp::LOAD_ADDR:
      add(i.src1);
      break; // base
    case TACOp::CALL:
      // 参数寄存器由 PARAM 指令处理，CALL 本身不额外 use
      break;
    default:
      break;
    }
  return u;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 活跃变量分析（经典逆序数据流）
// ═══════════════════════════════════════════════════════════════════════════════
void
RegAllocator::livenessAnalysis(std::vector<BasicBlock>& blocks)
{
  // 1. 计算每个基本块的局部 def/use 集合
  for(auto& bb : blocks)
    {
      bb.def.clear();
      bb.use.clear();
      for(auto& instr : bb.instrs)
        {
          // use：在本块中先被使用（未被前面指令 def）的变量
          for(auto& u : getUses(instr))
            if(!bb.def.count(u))
              bb.use.insert(u);
          // def：本块中定义的变量
          for(auto& d : getDefs(instr))
            bb.def.insert(d);
        }
    }

  // 2. 迭代计算 liveIn / liveOut 直到不动点
  bool changed = true;
  while(changed)
    {
      changed = false;
      // 逆序遍历（近似后序遍历，收敛更快）
      for(int i = (int)blocks.size() - 1; i >= 0; --i)
        {
          auto& bb = blocks[i];

          // liveOut = ∪ liveIn(succ)
          std::unordered_set<std::string> newOut;
          for(auto* succ : bb.succs)
            for(auto& v : succ->liveIn)
              newOut.insert(v);

          // liveIn = use ∪ (liveOut - def)
          std::unordered_set<std::string> newIn = bb.use;
          for(auto& v : newOut)
            if(!bb.def.count(v))
              newIn.insert(v);

          if(newIn != bb.liveIn || newOut != bb.liveOut)
            {
              bb.liveIn = std::move(newIn);
              bb.liveOut = std::move(newOut);
              changed = true;
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 构建干涉图
// ═══════════════════════════════════════════════════════════════════════════════
InterferenceGraph
RegAllocator::buildInterferenceGraph(std::vector<BasicBlock>& blocks)
{

  InterferenceGraph ig;

  for(auto& bb : blocks)
    {
      // 从 liveOut 开始，逆序扫描指令，维护当前活跃集合
      std::unordered_set<std::string> live = bb.liveOut;

      for(int i = (int)bb.instrs.size() - 1; i >= 0; --i)
        {
          auto& instr = bb.instrs[i];
          auto defs = getDefs(instr);
          auto uses = getUses(instr);

          // 确保所有相关虚拟寄存器都在图中
          for(auto& v : live)
            ig.addNode(v);
          for(auto& d : defs)
            ig.addNode(d);
          for(auto& u : uses)
            ig.addNode(u);

          // 定义点：def 与所有当前活跃变量干涉
          for(auto& d : defs)
            for(auto& v : live)
              ig.addEdge(d, v);

          // 更新活跃集合（逆序）
          for(auto& d : defs)
            live.erase(d);
          for(auto& u : uses)
            live.insert(u);
        }
    }
  return ig;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Chaitin-Briggs 图着色
// ═══════════════════════════════════════════════════════════════════════════════
std::unordered_map<std::string, std::string>
RegAllocator::colorGraph(InterferenceGraph& ig,
                         const std::vector<std::string>& vregs)
{

  std::unordered_map<std::string, std::string> coloring;
  if(vregs.empty())
    return coloring;

  // 工作集：当前图中还未简化的节点
  std::unordered_set<std::string> remaining(vregs.begin(), vregs.end());
  // 简化栈
  std::stack<std::string> stk;
  // spill 候选（度数 >= K 的节点，按溢出代价选）
  std::unordered_set<std::string> spilled;

  // 复制邻接表（简化时需要从图中删节点）
  auto adjCopy = ig.adj; // string → set<string>

  // ── 简化阶段 ──────────────────────────────────────────────────────────────
  while(!remaining.empty())
    {
      // 找度数 < K 的节点
      std::string chosen;
      for(auto& v : remaining)
        {
          // 计算在 remaining 子图中的度数
          int deg = 0;
          auto it = adjCopy.find(v);
          if(it != adjCopy.end())
            for(auto& nb : it->second)
              if(remaining.count(nb))
                ++deg;
          if(deg < K)
            {
              chosen = v;
              break;
            }
        }

      if(chosen.empty())
        {
          // 所有节点度数 >= K → 乐观着色（Briggs 扩展）
          // 选度数最大的节点作为潜在 spill（先压栈，着色失败再 spill）
          int maxDeg = -1;
          for(auto& v : remaining)
            {
              int deg = 0;
              auto it = adjCopy.find(v);
              if(it != adjCopy.end())
                for(auto& nb : it->second)
                  if(remaining.count(nb))
                    ++deg;
              if(deg > maxDeg)
                {
                  maxDeg = deg;
                  chosen = v;
                }
            }
        }

      stk.push(chosen);
      remaining.erase(chosen);
    }

  // ── 着色阶段（栈弹出，逆序分配颜色）────────────────────────────────────
  while(!stk.empty())
    {
      std::string v = stk.top();
      stk.pop();

      // 找邻居已用的颜色
      std::unordered_set<std::string> usedColors;
      auto it = ig.adj.find(v);
      if(it != ig.adj.end())
        {
          for(auto& nb : it->second)
            {
              auto cit = coloring.find(nb);
              if(cit != coloring.end() && !cit->second.empty())
                usedColors.insert(cit->second);
            }
        }

      // 分配第一个可用的物理寄存器
      std::string assigned;
      for(int k = 0; k < K; ++k)
        {
          std::string pr = PHYS[k];
          if(!usedColors.count(pr))
            {
              assigned = pr;
              break;
            }
        }

      if(assigned.empty())
        {
          // 乐观着色失败 → 真正 spill
          coloring[v] = "";
          spilled.insert(v);
        }
      else
        {
          coloring[v] = assigned;
        }
    }

  return coloring;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 主分配入口
// ═══════════════════════════════════════════════════════════════════════════════
RegAllocator::Result
RegAllocator::allocate(std::vector<BasicBlock>& blocks)
{
  Result result;

  // 1. 活跃变量分析
  livenessAnalysis(blocks);

  // 2. 收集所有虚拟寄存器
  std::vector<std::string> vregs;
  {
    std::unordered_set<std::string> seen;
    for(auto& bb : blocks)
      for(auto& instr : bb.instrs)
        {
          for(auto& d : getDefs(instr))
            if(seen.insert(d).second)
              vregs.push_back(d);
          for(auto& u : getUses(instr))
            if(seen.insert(u).second)
              vregs.push_back(u);
        }
  }
  if(vregs.empty())
    return result;

  // 3. 构建干涉图
  auto ig = buildInterferenceGraph(blocks);

  // 4. 图着色
  result.coloring = colorGraph(ig, vregs);

  // 5. 为 spill 变量分配栈槽
  int spillOffset = 0;
  for(auto& [v, color] : result.coloring)
    {
      if(color.empty())
        {
          spillOffset -= 4; // 向低地址增长（相对 $fp）
          result.spillSlots[v] = spillOffset;
        }
    }
  result.spillBytes = -spillOffset; // 正数，表示需要额外扩大的帧字节数

  return result;
}

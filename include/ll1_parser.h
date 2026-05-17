#pragma once
#include <map>
#include <parser.h>
#include <set>
#include <stack>
#include <vector>

// ═══════════════════════════════════════════════════════════════════════
// 非终结符枚举
// ═══════════════════════════════════════════════════════════════════════
enum class NT
{
  PROGRAM,
  PROGRAM_HEAD,
  DECLARE_PART,
  PROGRAM_BODY,
  PROC_BODY,    // 过程体，BEGIN...END（不带 '.'）

  TYPE_DEC,
  TYPE_DEC_LIST,
  TYPE_DEC_MORE,
  TYPE_NAME,
  BASE_TYPE,
  STRUCTURE_TYPE,
  ARRAY_TYPE,
  REC_TYPE,
  FIELD_DEC_LIST,
  FIELD_DEC_MORE,
  ID_LIST,
  ID_MORE,

  VAR_DEC,
  VAR_DEC_LIST,
  VAR_DEC_MORE,
  VAR_ID_LIST,
  VAR_ID_MORE,

  PROC_DEC,
  PROC_DEC_MORE,
  PARAM_LIST,
  PARAM_DEC_LIST,
  PARAM_MORE,
  PARAM,
  FORM_LIST,
  FID_MORE,

  STM_LIST,
  STM_MORE,
  STM,
  ASSIGN_OR_CALL,
  VARI_MORE,
  FIELD_VAR_MORE,
  ACT_PARAM_LIST,
  ACT_PARAM_MORE,
  COND_STM,
  LOOP_STM,
  INPUT_STM,
  OUTPUT_STM,
  RETURN_STM,

  REL_EXP,
  OTHER_REL_E,
  EXP,
  OTHER_EXP,
  TERM,
  OTHER_TERM,
  FACTOR,
  VARIABLE,

  EPSILON,
};

// ─── 文法符号 ─────────────────────────────────────────────────────────
struct GSym
{
  bool isTerm;
  TokenType term;
  NT nt;
  static GSym
  T(TokenType t)
  {
    return { true, t, NT::EPSILON };
  }
  static GSym
  N(NT n)
  {
    return { false, {}, n };
  }
  bool
  isEps() const
  {
    return !isTerm && nt == NT::EPSILON;
  }
};

// ─── 产生式 ───────────────────────────────────────────────────────────
struct Production
{
  NT lhs;
  std::vector<GSym> rhs; // 空 == ε
};

using ParseTable = std::map<NT, std::map<TokenType, int> >;

// ═══════════════════════════════════════════════════════════════════════
// LL1Parser
//
// 继承 Parser，复用全部 parseXxx() 函数构建 AST。
// 驱动循环只负责"用预测表验证输入合法性"，
// 遇到叶子非终结符时调用对应 parseXxx()，
// 最终拼装出与递归下降完全相同的 AST 结构。
// ═══════════════════════════════════════════════════════════════════════
class LL1Parser : public Parser
{
public:
  explicit LL1Parser(std::vector<Token> tokens);

  // 对齐 Parser::parseProgram()，main.cpp 无缝切换
  ASTPtr parseProgram();

private:
  std::vector<Production> prods_;
  ParseTable table_;

  void buildGrammar();
  void addProd(NT lhs, std::vector<GSym> rhs,
               std::initializer_list<TokenType> firstSet);
  void addEps(NT lhs, std::initializer_list<TokenType> followSet);

  // 核心驱动：沿预测表消费 startNT，
  // 把每个"叶子非终结符"的解析结果追加到 out
  void drive(NT startNT, std::vector<ASTPtr>& out);

  // 语法错误同步集
  static const std::set<TokenType> kSyncSet;
  static std::string ntName(NT nt);
};

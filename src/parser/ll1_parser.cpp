#include "ll1_parser.h"
#include <cassert>
#include <iostream>

// ═══════════════════════════════════════════════════════════════════════
// 同步集：语法错误时遇到这些 token 时停止跳过
// ═══════════════════════════════════════════════════════════════════════
const std::set<TokenType> LL1Parser::kSyncSet = {
  TokenType::SEMI,     TokenType::KW_END,  TokenType::KW_FI,
  TokenType::KW_ENDWH, TokenType::KW_ELSE, TokenType::KW_BEGIN,
  TokenType::EOF_TOK,
};

// ═══════════════════════════════════════════════════════════════════════
// 构造
// ═══════════════════════════════════════════════════════════════════════
LL1Parser::LL1Parser(std::vector<Token> tokens)
    : Parser(std::move(tokens)) // tokens 传给 Parser，pos_/tokens_ 由父类管理
{
  buildGrammar();
}

// ═══════════════════════════════════════════════════════════════════════
// addProd / addEps
// ═══════════════════════════════════════════════════════════════════════
void
LL1Parser::addProd(NT lhs, std::vector<GSym> rhs,
                   std::initializer_list<TokenType> firstSet)
{
  int idx = (int)prods_.size();
  prods_.push_back({ lhs, std::move(rhs) });
  for(TokenType t : firstSet)
    table_[lhs][t] = idx;
}

void
LL1Parser::addEps(NT lhs, std::initializer_list<TokenType> followSet)
{
  int idx = (int)prods_.size();
  prods_.push_back({ lhs, {} }); // 空 rhs == ε
  for(TokenType t : followSet)
    table_[lhs][t] = idx;
}

// ─── 宏简写 ──────────────────────────────────────────────────────────
#define TT(x) GSym::T(TokenType::x)
#define NN(x) GSym::N(NT::x)

// ═══════════════════════════════════════════════════════════════════════
// buildGrammar
//
// 把 SNL LL(1) 文法的所有产生式写进 prods_，
// 并在 table_ 里为每个 FIRST/FOLLOW token 填入产生式下标。
//
// FIRST/FOLLOW 集按标准 SNL 文法手工计算；
// 每条 addProd/addEps 的第三参数即该产生式的预测 token 集合。
// ═══════════════════════════════════════════════════════════════════════
void
LL1Parser::buildGrammar()
{
  // ── 顶层 ─────────────────────────────────────────────────────────
  // Program → ProgramHead DeclarePart ProgramBody
  addProd(NT::PROGRAM,
          { NN(PROGRAM_HEAD), NN(DECLARE_PART), NN(PROGRAM_BODY) },
          { TokenType::KW_PROGRAM });

  // ProgramHead → PROGRAM ID
  addProd(NT::PROGRAM_HEAD, { TT(KW_PROGRAM), TT(ID) },
          { TokenType::KW_PROGRAM });

  // DeclarePart → TypeDec VarDec ProcDec
  addProd(NT::DECLARE_PART, { NN(TYPE_DEC), NN(VAR_DEC), NN(PROC_DEC) },
          { TokenType::KW_TYPE, TokenType::KW_VAR, TokenType::KW_PROCEDURE,
            TokenType::KW_BEGIN });

  // ProgramBody → BEGIN StmList END
  addProd(NT::PROGRAM_BODY, { TT(KW_BEGIN), NN(STM_LIST), TT(KW_END) },
          { TokenType::KW_BEGIN });

  // ── 类型声明 ─────────────────────────────────────────────────────
  // TypeDec → TYPE TypeDecList | ε
  addProd(NT::TYPE_DEC, { TT(KW_TYPE), NN(TYPE_DEC_LIST) },
          { TokenType::KW_TYPE });
  addEps(NT::TYPE_DEC,
         { TokenType::KW_VAR, TokenType::KW_PROCEDURE, TokenType::KW_BEGIN });

  // TypeDecList → ID = TypeName ; TypeDecMore
  addProd(NT::TYPE_DEC_LIST,
          { TT(ID), TT(EQ), NN(TYPE_NAME), TT(SEMI), NN(TYPE_DEC_MORE) },
          { TokenType::ID });

  // TypeDecMore → TypeDecList | ε
  addProd(NT::TYPE_DEC_MORE, { NN(TYPE_DEC_LIST) }, { TokenType::ID });
  addEps(NT::TYPE_DEC_MORE,
         { TokenType::KW_VAR, TokenType::KW_PROCEDURE, TokenType::KW_BEGIN });

  // TypeName → BaseType | StructureType | ID(别名)
  addProd(NT::TYPE_NAME, { NN(BASE_TYPE) },
          { TokenType::KW_INTEGER, TokenType::KW_CHAR });
  addProd(NT::TYPE_NAME, { NN(STRUCTURE_TYPE) },
          { TokenType::KW_ARRAY, TokenType::KW_RECORD });
  addProd(NT::TYPE_NAME, { TT(ID) }, { TokenType::ID });

  // BaseType → INTEGER | CHAR
  addProd(NT::BASE_TYPE, { TT(KW_INTEGER) }, { TokenType::KW_INTEGER });
  addProd(NT::BASE_TYPE, { TT(KW_CHAR) }, { TokenType::KW_CHAR });

  // StructureType → ArrayType | RecType
  addProd(NT::STRUCTURE_TYPE, { NN(ARRAY_TYPE) }, { TokenType::KW_ARRAY });
  addProd(NT::STRUCTURE_TYPE, { NN(REC_TYPE) }, { TokenType::KW_RECORD });

  // ArrayType → ARRAY [ INTC .. INTC ] OF BaseType
  addProd(NT::ARRAY_TYPE,
          { TT(KW_ARRAY), TT(LBRACKET), TT(INTC), TT(DOTDOT), TT(INTC),
            TT(RBRACKET), TT(KW_OF), NN(BASE_TYPE) },
          { TokenType::KW_ARRAY });

  // RecType → RECORD FieldDecList END
  addProd(NT::REC_TYPE, { TT(KW_RECORD), NN(FIELD_DEC_LIST), TT(KW_END) },
          { TokenType::KW_RECORD });

  // FieldDecList → BaseType IdList ; FieldDecMore
  //              | ArrayType IdList ; FieldDecMore
  addProd(NT::FIELD_DEC_LIST,
          { NN(BASE_TYPE), NN(ID_LIST), TT(SEMI), NN(FIELD_DEC_MORE) },
          { TokenType::KW_INTEGER, TokenType::KW_CHAR });
  addProd(NT::FIELD_DEC_LIST,
          { NN(ARRAY_TYPE), NN(ID_LIST), TT(SEMI), NN(FIELD_DEC_MORE) },
          { TokenType::KW_ARRAY });

  // FieldDecMore → ε | FieldDecList
  addEps(NT::FIELD_DEC_MORE, { TokenType::KW_END });
  addProd(NT::FIELD_DEC_MORE, { NN(FIELD_DEC_LIST) },
          { TokenType::KW_INTEGER, TokenType::KW_CHAR, TokenType::KW_ARRAY });

  // IdList → ID IdMore
  addProd(NT::ID_LIST, { TT(ID), NN(ID_MORE) }, { TokenType::ID });

  // IdMore → ε | , IdList
  addEps(NT::ID_MORE, { TokenType::SEMI });
  addProd(NT::ID_MORE, { TT(COMMA), NN(ID_LIST) }, { TokenType::COMMA });

  // ── 变量声明 ─────────────────────────────────────────────────────
  // VarDec → VAR VarDecList | ε
  addProd(NT::VAR_DEC, { TT(KW_VAR), NN(VAR_DEC_LIST) },
          { TokenType::KW_VAR });
  addEps(NT::VAR_DEC, { TokenType::KW_PROCEDURE, TokenType::KW_BEGIN });

  // VarDecList → TypeName VarIdList ; VarDecMore
  addProd(NT::VAR_DEC_LIST,
          { NN(TYPE_NAME), NN(VAR_ID_LIST), TT(SEMI), NN(VAR_DEC_MORE) },
          { TokenType::KW_INTEGER, TokenType::KW_CHAR, TokenType::KW_ARRAY,
            TokenType::KW_RECORD, TokenType::ID });

  // VarDecMore → ε | VarDecList
  addEps(NT::VAR_DEC_MORE, { TokenType::KW_PROCEDURE, TokenType::KW_BEGIN });
  addProd(NT::VAR_DEC_MORE, { NN(VAR_DEC_LIST) },
          { TokenType::KW_INTEGER, TokenType::KW_CHAR, TokenType::KW_ARRAY,
            TokenType::KW_RECORD, TokenType::ID });

  // VarIdList → ID VarIdMore
  addProd(NT::VAR_ID_LIST, { TT(ID), NN(VAR_ID_MORE) }, { TokenType::ID });

  // VarIdMore → ε | , VarIdList
  addEps(NT::VAR_ID_MORE, { TokenType::SEMI });
  addProd(NT::VAR_ID_MORE, { TT(COMMA), NN(VAR_ID_LIST) },
          { TokenType::COMMA });

  // ── 过程声明 ─────────────────────────────────────────────────────
  // ProcDec → ε | PROCEDURE ID ( ParamList ) ; DeclarePart ProgramBody
  // ProcDecMore
  addEps(NT::PROC_DEC, { TokenType::KW_BEGIN });
  addProd(NT::PROC_DEC,
          { TT(KW_PROCEDURE), TT(ID), TT(LPAREN), NN(PARAM_LIST), TT(RPAREN),
            TT(SEMI), NN(DECLARE_PART), NN(PROC_BODY), NN(PROC_DEC_MORE) },
          { TokenType::KW_PROCEDURE });

  // ProcDecMore → ε | ProcDec
  addEps(NT::PROC_DEC_MORE, { TokenType::KW_BEGIN });
  addProd(NT::PROC_DEC_MORE, { NN(PROC_DEC) }, { TokenType::KW_PROCEDURE });

  // ParamList → ε | ParamDecList
  addEps(NT::PARAM_LIST, { TokenType::RPAREN });
  addProd(NT::PARAM_LIST, { NN(PARAM_DEC_LIST) },
          { TokenType::KW_VAR, TokenType::KW_INTEGER, TokenType::KW_CHAR,
            TokenType::KW_ARRAY, TokenType::KW_RECORD, TokenType::ID });

  // ParamDecList → Param ParamMore
  addProd(NT::PARAM_DEC_LIST, { NN(PARAM), NN(PARAM_MORE) },
          { TokenType::KW_VAR, TokenType::KW_INTEGER, TokenType::KW_CHAR,
            TokenType::KW_ARRAY, TokenType::KW_RECORD, TokenType::ID });

  // ParamMore → ε | ; ParamDecList
  addEps(NT::PARAM_MORE, { TokenType::RPAREN });
  addProd(NT::PARAM_MORE, { TT(SEMI), NN(PARAM_DEC_LIST) },
          { TokenType::SEMI });

  // Param → TypeName FormList | VAR TypeName FormList
  addProd(NT::PARAM, { NN(TYPE_NAME), NN(FORM_LIST) },
          { TokenType::KW_INTEGER, TokenType::KW_CHAR, TokenType::KW_ARRAY,
            TokenType::KW_RECORD, TokenType::ID });
  addProd(NT::PARAM, { TT(KW_VAR), NN(TYPE_NAME), NN(FORM_LIST) },
          { TokenType::KW_VAR });

  // FormList → ID FidMore
  addProd(NT::FORM_LIST, { TT(ID), NN(FID_MORE) }, { TokenType::ID });

  // FidMore → ε | , FormList
  addEps(NT::FID_MORE, { TokenType::SEMI, TokenType::RPAREN });
  addProd(NT::FID_MORE, { TT(COMMA), NN(FORM_LIST) }, { TokenType::COMMA });

  // ── 语句 ─────────────────────────────────────────────────────────
  // StmList → Stm StmMore
  addProd(NT::STM_LIST, { NN(STM), NN(STM_MORE) },
          { TokenType::KW_IF, TokenType::KW_WHILE, TokenType::KW_READ,
            TokenType::KW_WRITE, TokenType::KW_RETURN, TokenType::ID });

  // StmMore → ε | ; StmList
  addEps(NT::STM_MORE, { TokenType::KW_END, TokenType::KW_FI,
                         TokenType::KW_ENDWH, TokenType::KW_ELSE });
  addProd(NT::STM_MORE, { TT(SEMI), NN(STM_LIST) }, { TokenType::SEMI });

  // Stm 分发（每个 token 对应一类语句）
  addProd(NT::STM, { NN(COND_STM) }, { TokenType::KW_IF });
  addProd(NT::STM, { NN(LOOP_STM) }, { TokenType::KW_WHILE });
  addProd(NT::STM, { NN(INPUT_STM) }, { TokenType::KW_READ });
  addProd(NT::STM, { NN(OUTPUT_STM) }, { TokenType::KW_WRITE });
  addProd(NT::STM, { NN(RETURN_STM) }, { TokenType::KW_RETURN });
  addProd(NT::STM, { NN(ASSIGN_OR_CALL) }, { TokenType::ID });

  // AssignOrCall → ID VariMore := Exp  |  ID ( ActParamList )
  // 两条产生式 FIRST 都是 ID，LL(1) 无法区分；
  // 因此这里只注册一条占位，实际调用 parseAssignOrCall() 统一处理。
  addProd(NT::ASSIGN_OR_CALL, { TT(ID) }, { TokenType::ID });

  // VariMore → ε | [ Exp ] | . ID FieldVarMore
  static const std::initializer_list<TokenType> variMoreFollow = {
    TokenType::ASSIGN,  TokenType::COMMA,    TokenType::SEMI,
    TokenType::RPAREN,  TokenType::RBRACKET, TokenType::KW_END,
    TokenType::KW_FI,   TokenType::KW_ENDWH, TokenType::KW_ELSE,
    TokenType::PLUS,    TokenType::MINUS,    TokenType::TIMES,
    TokenType::DIVIDE,  TokenType::LT,       TokenType::EQ,
    TokenType::EOF_TOK,
  };
  addEps(NT::VARI_MORE, variMoreFollow);
  addProd(NT::VARI_MORE, { TT(LBRACKET), NN(EXP), TT(RBRACKET) },
          { TokenType::LBRACKET });
  addProd(NT::VARI_MORE, { TT(DOT), TT(ID), NN(FIELD_VAR_MORE) },
          { TokenType::DOT });

  // FieldVarMore → ε | [ Exp ]
  addEps(NT::FIELD_VAR_MORE, variMoreFollow);
  addProd(NT::FIELD_VAR_MORE, { TT(LBRACKET), NN(EXP), TT(RBRACKET) },
          { TokenType::LBRACKET });

  // ConditionalStm → IF RelExp THEN StmList ELSE StmList FI
  addProd(NT::COND_STM,
          { TT(KW_IF), NN(REL_EXP), TT(KW_THEN), NN(STM_LIST), TT(KW_ELSE),
            NN(STM_LIST), TT(KW_FI) },
          { TokenType::KW_IF });

  // LoopStm → WHILE RelExp DO StmList ENDWH
  addProd(NT::LOOP_STM,
          { TT(KW_WHILE), NN(REL_EXP), TT(KW_DO), NN(STM_LIST), TT(KW_ENDWH) },
          { TokenType::KW_WHILE });

  // InputStm → READ ( ID )
  addProd(NT::INPUT_STM, { TT(KW_READ), TT(LPAREN), TT(ID), TT(RPAREN) },
          { TokenType::KW_READ });

  // OutputStm → WRITE ( Exp )
  addProd(NT::OUTPUT_STM, { TT(KW_WRITE), TT(LPAREN), NN(EXP), TT(RPAREN) },
          { TokenType::KW_WRITE });

  // ReturnStm → RETURN ( Exp )
  addProd(NT::RETURN_STM, { TT(KW_RETURN), TT(LPAREN), NN(EXP), TT(RPAREN) },
          { TokenType::KW_RETURN });

  // ActParamList → ε | Exp ActParamMore
  addEps(NT::ACT_PARAM_LIST, { TokenType::RPAREN });
  addProd(NT::ACT_PARAM_LIST, { NN(EXP), NN(ACT_PARAM_MORE) },
          { TokenType::LPAREN, TokenType::INTC, TokenType::ID });

  // ActParamMore → ε | , Exp ActParamMore
  addEps(NT::ACT_PARAM_MORE, { TokenType::RPAREN });
  addProd(NT::ACT_PARAM_MORE, { TT(COMMA), NN(EXP), NN(ACT_PARAM_MORE) },
          { TokenType::COMMA });

  // ── 表达式 ───────────────────────────────────────────────────────
  // RelExp → Exp OtherRelE
  addProd(NT::REL_EXP, { NN(EXP), NN(OTHER_REL_E) },
          { TokenType::LPAREN, TokenType::INTC, TokenType::ID });

  // OtherRelE → < Exp | = Exp
  addProd(NT::OTHER_REL_E, { TT(LT), NN(EXP) }, { TokenType::LT });
  addProd(NT::OTHER_REL_E, { TT(EQ), NN(EXP) }, { TokenType::EQ });

  // Exp → Term OtherExp
  addProd(NT::EXP, { NN(TERM), NN(OTHER_EXP) },
          { TokenType::LPAREN, TokenType::INTC, TokenType::ID });

  static const std::initializer_list<TokenType> expFollow = {
    TokenType::RPAREN,  TokenType::RBRACKET, TokenType::SEMI,
    TokenType::KW_THEN, TokenType::KW_DO,    TokenType::KW_END,
    TokenType::KW_FI,   TokenType::KW_ENDWH, TokenType::KW_ELSE,
    TokenType::LT,      TokenType::EQ,       TokenType::EOF_TOK,
    TokenType::COMMA,
  };

  // OtherExp → ε | + Term OtherExp | - Term OtherExp
  addEps(NT::OTHER_EXP, expFollow);
  addProd(NT::OTHER_EXP, { TT(PLUS), NN(TERM), NN(OTHER_EXP) },
          { TokenType::PLUS });
  addProd(NT::OTHER_EXP, { TT(MINUS), NN(TERM), NN(OTHER_EXP) },
          { TokenType::MINUS });

  // Term → Factor OtherTerm
  addProd(NT::TERM, { NN(FACTOR), NN(OTHER_TERM) },
          { TokenType::LPAREN, TokenType::INTC, TokenType::ID });

  static const std::initializer_list<TokenType> termFollow = {
    TokenType::RPAREN,  TokenType::RBRACKET, TokenType::SEMI,
    TokenType::KW_THEN, TokenType::KW_DO,    TokenType::KW_END,
    TokenType::KW_FI,   TokenType::KW_ENDWH, TokenType::KW_ELSE,
    TokenType::LT,      TokenType::EQ,       TokenType::PLUS,
    TokenType::MINUS,   TokenType::EOF_TOK,  TokenType::COMMA,
  };

  // OtherTerm → ε | * Factor OtherTerm | / Factor OtherTerm
  addEps(NT::OTHER_TERM, termFollow);
  addProd(NT::OTHER_TERM, { TT(TIMES), NN(FACTOR), NN(OTHER_TERM) },
          { TokenType::TIMES });
  addProd(NT::OTHER_TERM, { TT(DIVIDE), NN(FACTOR), NN(OTHER_TERM) },
          { TokenType::DIVIDE });

  // Factor → ( Exp ) | INTC | Variable
  addProd(NT::FACTOR, { TT(LPAREN), NN(EXP), TT(RPAREN) },
          { TokenType::LPAREN });
  addProd(NT::FACTOR, { TT(INTC) }, { TokenType::INTC });
  addProd(NT::FACTOR, { NN(VARIABLE) }, { TokenType::ID });

  // Variable → ID VariMore
  addProd(NT::VARIABLE, { TT(ID), NN(VARI_MORE) }, { TokenType::ID });
}

#undef TT
#undef NN

// ═══════════════════════════════════════════════════════════════════════
// drive() — 核心驱动循环
//
// 用显式符号栈走预测分析；
// 遇到"叶子非终结符"（即直接对应一个 parseXxx()）时，
// 调用父类的对应函数，把返回的 ASTPtr 放入 out，
// 并让父类的 pos_ 自然前进（两者共享同一个 tokens_ 和 pos_）。
//
// "叶子非终结符"的定义：
//   该 NT 的所有产生式右部不含其他非终结符，
//   或者它对应的 parseXxx() 能完整消费其推导出的所有终结符。
//   实践上我们把以下 NT 标记为叶子，其余 NT 的产生式只含终结符，
//   在驱动循环里直接匹配：
//
//   PROGRAM_HEAD     → parseProgramHead()
//   TYPE_DEC         → parseTypeDec()
//   VAR_DEC          → parseVarDec()
//   PROC_DEC         → parseProcDec()
//   PROGRAM_BODY     → parseProgramBody()   （即 StmList 容器）
//   COND_STM         → parseConditionalStm()
//   LOOP_STM         → parseLoopStm()
//   INPUT_STM        → parseInputStm()
//   OUTPUT_STM       → parseOutputStm()
//   RETURN_STM       → parseReturnStm()
//   ASSIGN_OR_CALL   → parseAssignOrCall()
//
// 驱动循环遇到这些 NT 时直接调用对应函数，不展开其产生式。
// ═══════════════════════════════════════════════════════════════════════

// 这些 NT 交给 parseXxx() 全权处理，驱动循环不展开它们
static bool
isLeafNT(NT nt)
{
  switch(nt)
    {
    case NT::PROGRAM_HEAD:
    case NT::TYPE_DEC:
    case NT::VAR_DEC:
    case NT::PROC_DEC:
    case NT::PROGRAM_BODY:
    case NT::PROC_BODY:
    case NT::COND_STM:
    case NT::LOOP_STM:
    case NT::INPUT_STM:
    case NT::OUTPUT_STM:
    case NT::RETURN_STM:
    case NT::ASSIGN_OR_CALL:
      return true;
    default:
      return false;
    }
}

void
LL1Parser::drive(NT startNT, std::vector<ASTPtr>& out)
{
  // 显式符号栈：存放"还没处理的文法符号"
  std::stack<GSym> symStack;
  symStack.push(GSym::N(startNT));

  while(!symStack.empty())
    {
      GSym top = symStack.top();
      symStack.pop();

      // ── ε 符号，直接忽略 ────────────────────────────────────────
      if(top.isEps())
        continue;

      // ── 终结符：必须与当前 token 匹配 ───────────────────────────
      if(top.isTerm)
        {
          if(peek().type == top.term)
            {
              advance(); // 消耗：调父类 advance()，pos_ 前进
            }
          else
            {
              // 错误：记录并做恐慌恢复
              syncError("期望 token 类型 " + std::to_string((int)top.term)
                        + "，得到 '" + peek().value + "'");
              // 跳过 token 直到同步集
              while(kSyncSet.find(peek().type) == kSyncSet.end()
                    && peek().type != TokenType::EOF_TOK)
                advance();
            }
          continue;
        }

      // ── 非终结符 ─────────────────────────────────────────────────
      NT nt = top.nt;

      // 叶子 NT：交给对应的 parseXxx()，结果收集到 out
      if(isLeafNT(nt))
        {
          ASTPtr node;
          switch(nt)
            {
            case NT::PROGRAM_HEAD:
              node = parseProgramHead();
              break;
            case NT::TYPE_DEC:
              node = parseTypeDec();
              break;
            case NT::VAR_DEC:
              node = parseVarDec();
              break;
            case NT::PROC_DEC:
              node = parseProcDec();
              break;
            case NT::PROGRAM_BODY:
              node = parseProgramBody();
              break;
            case NT::PROC_BODY:
              node = parseProcBody();
              break;
            case NT::COND_STM:
              node = parseConditionalStm();
              break;
            case NT::LOOP_STM:
              node = parseLoopStm();
              break;
            case NT::INPUT_STM:
              node = parseInputStm();
              break;
            case NT::OUTPUT_STM:
              node = parseOutputStm();
              break;
            case NT::RETURN_STM:
              node = parseReturnStm();
              break;
            case NT::ASSIGN_OR_CALL:
              node = parseAssignOrCall();
              break;
            default:
              break;
            }
          if(node)
            out.push_back(std::move(node));
          continue;
        }

      // 非叶子 NT：查预测表，把产生式右部逆序压栈
      auto tableIt = table_.find(nt);
      if(tableIt == table_.end())
        {
          syncError(ntName(nt) + " 不在预测表中，token='" + peek().value
                    + "'");
          continue;
        }
      auto cellIt = tableIt->second.find(peek().type);
      if(cellIt == tableIt->second.end())
        {
          // 表中无项：报错，恢复
          syncError("非终结符 " + ntName(nt) + " 遇到意外 token '"
                    + peek().value + "' (行 " + std::to_string(peek().line)
                    + ")");
          while(kSyncSet.find(peek().type) == kSyncSet.end()
                && peek().type != TokenType::EOF_TOK)
            advance();
          continue;
        }

      // 找到产生式，逆序压右部
      const Production& prod = prods_[cellIt->second];
      for(auto it = prod.rhs.rbegin(); it != prod.rhs.rend(); ++it)
        {
          if(!it->isEps())
            symStack.push(*it);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
// parseProgram() — 主入口
//
// 结构对标递归下降的 Parser::parseProgram()：
//   返回 NodeKind::PROGRAM 节点，children = [head, declarePart, body]
//
// 这里用 drive() 驱动 PROGRAM 这条产生式：
//   PROGRAM → PROGRAM_HEAD DECLARE_PART PROGRAM_BODY
//
// DECLARE_PART 本身是 3 个叶子 NT（TypeDec/VarDec/ProcDec）的容器，
// 我们在 drive() 里展开 DECLARE_PART，让它把 3 个子节点放到 parts 里，
// 再自己组装 declarePart 节点，保证 children 顺序与递归下降一致。
// ═══════════════════════════════════════════════════════════════════════
ASTPtr
LL1Parser::parseProgram()
{
  int rootLine = peek().line;

  // ── Step 1: ProgramHead（叶子）─────────────────────────────────
  // 驱动 PROGRAM_HEAD，让 parseProgramHead() 消费 "program ID"
  std::vector<ASTPtr> headParts;
  drive(NT::PROGRAM_HEAD, headParts);
  ASTPtr headNode = headParts.empty() ? makeNode(NodeKind::PROGRAM, rootLine)
                                      : std::move(headParts[0]);

  // ── Step 2: DeclarePart = TypeDec + VarDec + ProcDec ───────────
  // 依次驱动三个叶子 NT，收集结果
  std::vector<ASTPtr> typeParts, varParts, procParts;
  drive(NT::TYPE_DEC, typeParts);
  drive(NT::VAR_DEC, varParts);
  drive(NT::PROC_DEC, procParts);

  // 组装 declarePart 节点（对齐 Parser::parseDeclarePart() 的结构）
  auto declarePart = makeNode(NodeKind::TYPE_DEC, rootLine);
  declarePart->name = "__DeclarePart__";
  declarePart->children.push_back(typeParts.empty()
                                      ? makeNode(NodeKind::TYPE_DEC, rootLine)
                                      : std::move(typeParts[0]));
  declarePart->children.push_back(varParts.empty()
                                      ? makeNode(NodeKind::VAR_DEC, rootLine)
                                      : std::move(varParts[0]));
  declarePart->children.push_back(procParts.empty()
                                      ? makeNode(NodeKind::PROC_DEC, rootLine)
                                      : std::move(procParts[0]));

  // ── Step 3: ProgramBody ─────────────────────────────────────────
  std::vector<ASTPtr> bodyParts;
  drive(NT::PROGRAM_BODY, bodyParts);
  ASTPtr bodyNode = bodyParts.empty()
                        ? makeNode(NodeKind::ASSIGN_STM, rootLine)
                        : std::move(bodyParts[0]);

  // ── Step 4: 期望 EOF ────────────────────────────────────────────
  if(peek().type != TokenType::EOF_TOK)
    syncError("程序结束后存在多余内容：'" + peek().value + "'");

  // ── Step 5: 组装根节点（对齐 Parser::parseProgram() 的结构）────
  //   children[0] = ProgramHead
  //   children[1] = DeclarePart
  //   children[2] = ProgramBody (StmList)
  auto root = makeNode(NodeKind::PROGRAM, rootLine);
  root->name = headNode->name; // 程序名
  root->children.push_back(std::move(headNode));
  root->children.push_back(std::move(declarePart));
  root->children.push_back(std::move(bodyNode));
  return root;
}

// ═══════════════════════════════════════════════════════════════════════
// ntName — 调试用
// ═══════════════════════════════════════════════════════════════════════
std::string
LL1Parser::ntName(NT nt)
{
  switch(nt)
    {
#define C(x)                                                                  \
  case NT::x:                                                                 \
    return #x
      C(PROGRAM);
      C(PROGRAM_HEAD);
      C(DECLARE_PART);
      C(PROGRAM_BODY);
      C(PROC_BODY);
      C(TYPE_DEC);
      C(TYPE_DEC_LIST);
      C(TYPE_DEC_MORE);
      C(TYPE_NAME);
      C(BASE_TYPE);
      C(STRUCTURE_TYPE);
      C(ARRAY_TYPE);
      C(REC_TYPE);
      C(FIELD_DEC_LIST);
      C(FIELD_DEC_MORE);
      C(ID_LIST);
      C(ID_MORE);
      C(VAR_DEC);
      C(VAR_DEC_LIST);
      C(VAR_DEC_MORE);
      C(VAR_ID_LIST);
      C(VAR_ID_MORE);
      C(PROC_DEC);
      C(PROC_DEC_MORE);
      C(PARAM_LIST);
      C(PARAM_DEC_LIST);
      C(PARAM_MORE);
      C(PARAM);
      C(FORM_LIST);
      C(FID_MORE);
      C(STM_LIST);
      C(STM_MORE);
      C(STM);
      C(ASSIGN_OR_CALL);
      C(VARI_MORE);
      C(FIELD_VAR_MORE);
      C(ACT_PARAM_LIST);
      C(ACT_PARAM_MORE);
      C(COND_STM);
      C(LOOP_STM);
      C(INPUT_STM);
      C(OUTPUT_STM);
      C(RETURN_STM);
      C(REL_EXP);
      C(OTHER_REL_E);
      C(EXP);
      C(OTHER_EXP);
      C(TERM);
      C(OTHER_TERM);
      C(FACTOR);
      C(VARIABLE);
      C(EPSILON);
#undef C
    default:
      return "UNKNOWN";
    }
}

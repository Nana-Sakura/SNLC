#include <cassert>
#include <parser.h>
#include <stdexcept>

// ─── 构造
// ──────────────────────────────────────────────────────────────────────
Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens))
{
  // 确保末尾有 EOF
  if(tokens_.empty() || tokens_.back().type != TokenType::EOF_TOK)
    {
      tokens_.emplace_back(TokenType::EOF_TOK, "", 0, 0);
    }
}

// ─── Token 操作
// ────────────────────────────────────────────────────────────────
const Token&
Parser::peek(int offset) const
{
  size_t idx = pos_ + offset;
  if(idx >= tokens_.size())
    return tokens_.back(); // EOF
  return tokens_[idx];
}

const Token&
Parser::advance()
{
  const Token& t = tokens_[pos_];
  if(pos_ + 1 < tokens_.size())
    ++pos_;
  return t;
}

bool
Parser::check(TokenType t) const
{
  return peek().type == t;
}

bool
Parser::match(TokenType t)
{
  if(check(t))
    {
      advance();
      return true;
    }
  return false;
}

const Token&
Parser::expect(TokenType t, const std::string& msg)
{
  if(!check(t))
    {
      syncError("第 " + std::to_string(peek().line) + " 行：" + msg
                + "，得到 '" + peek().value + "'");
    }
  return advance();
}

void
Parser::syncError(const std::string& msg)
{
  errors_.push_back(msg);
  // 简单错误恢复：跳到下一个分号或 END
  while(!check(TokenType::EOF_TOK) && !check(TokenType::SEMI)
        && !check(TokenType::KW_END) && !check(TokenType::KW_FI)
        && !check(TokenType::KW_ENDWH))
    {
      advance();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 顶层
// ═══════════════════════════════════════════════════════════════════════════════

// Program ::= ProgramHead DeclarePart ProgramBody
ASTPtr
Parser::parseProgram()
{
  auto node = makeNode(NodeKind::PROGRAM, peek().line);
  node->children.push_back(parseProgramHead());
  node->children.push_back(parseDeclarePart());
  node->children.push_back(parseProgramBody());
  expect(TokenType::EOF_TOK, "程序结束后存在多余内容");
  return node;
}

// ProgramHead ::= PROGRAM ProgramName(ID)
ASTPtr
Parser::parseProgramHead()
{
  expect(TokenType::KW_PROGRAM, "期望 'program'");
  auto tok = expect(TokenType::ID, "期望程序名（标识符）");
  auto node = makeNode(NodeKind::PROGRAM, tok.line);
  node->name = tok.value;
  return node;
}

// DeclarePart ::= TypeDec VarDec ProcDec
ASTPtr
Parser::parseDeclarePart()
{
  auto node = makeNode(NodeKind::TYPE_DEC, peek().line); // 复用节点作容器
  node->name = "__DeclarePart__";
  node->children.push_back(parseTypeDec());
  node->children.push_back(parseVarDec());
  node->children.push_back(parseProcDec());
  return node;
}

// ProgramBody ::= BEGIN StmList END .   （顶层程序，结尾为 end.）
ASTPtr
Parser::parseProgramBody()
{
  expect(TokenType::KW_BEGIN, "期望 'begin'");
  auto body = parseStmList();
  expect(TokenType::KW_END, "期望 'end'");
  expect(TokenType::DOT, "期望 '.'（程序结束标志 'end.'）");
  return body;
}

// ProcBody ::= BEGIN StmList END       （过程体，结尾只有 end，不带 '.'）
ASTPtr
Parser::parseProcBody()
{
  expect(TokenType::KW_BEGIN, "期望 'begin'");
  auto body = parseStmList();
  expect(TokenType::KW_END, "期望 'end'");
  return body;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 类型声明
// ═══════════════════════════════════════════════════════════════════════════════

// TypeDec ::= ε | TYPE TypeDecList
ASTPtr
Parser::parseTypeDec()
{
  auto node = makeNode(NodeKind::TYPE_DEC, peek().line);
  if(check(TokenType::KW_TYPE))
    {
      advance();                                    // 消耗 TYPE
      node->children.push_back(parseTypeDecList()); // 把整组别名声明挂入
    }
  return node;
}

// TypeDecList ::= TypeId = TypeName ; TypeDecMore
// TypeDecMore ::= ε | TypeDecList
ASTPtr
Parser::parseTypeDecList()
{
  auto node = makeNode(NodeKind::TYPE_DEC, peek().line);
  do
    {
      auto id = expect(TokenType::ID, "期望类型别名名称");
      expect(TokenType::EQ, "期望 '='");
      auto typeNode = parseTypeName();
      expect(TokenType::SEMI, "期望 ';'");

      auto entry = makeNode(NodeKind::TYPE_DEC, id.line);
      entry->name = id.value;
      entry->children.push_back(std::move(typeNode));
      node->children.push_back(std::move(entry));
    }
  while(check(TokenType::ID)); // TypeDecMore：ID 开头表示还有更多
  return node;
}

// TypeName ::= BaseType | StructureType | ID
ASTPtr
Parser::parseTypeName()
{
  if(check(TokenType::KW_INTEGER) || check(TokenType::KW_CHAR))
    {
      return parseBaseType();
    }
  if(check(TokenType::KW_ARRAY) || check(TokenType::KW_RECORD))
    {
      return parseStructureType();
    }
  // 类型别名
  auto tok = expect(TokenType::ID, "期望类型名");
  auto node = makeNode(NodeKind::BASE_TYPE, tok.line);
  node->name = tok.value; // 语义阶段解析别名
  return node;
}

// BaseType ::= INTEGER | CHAR
ASTPtr
Parser::parseBaseType()
{
  auto tok = advance();
  auto node = makeNode(NodeKind::BASE_TYPE, tok.line);
  node->name = (tok.type == TokenType::KW_INTEGER) ? "integer" : "char";
  return node;
}

// StructureType ::= ArrayType | RecType
ASTPtr
Parser::parseStructureType()
{
  if(check(TokenType::KW_ARRAY))
    return parseArrayType();
  return parseRecType();
}

// ArrayType ::= ARRAY [ low .. top ] OF BaseType
ASTPtr
Parser::parseArrayType()
{
  int line = peek().line;
  expect(TokenType::KW_ARRAY, "期望 'array'");
  expect(TokenType::LBRACKET, "期望 '['");
  auto low = expect(TokenType::INTC, "期望数组下界（整数）");
  expect(TokenType::DOTDOT, "期望 '..'");
  auto top = expect(TokenType::INTC, "期望数组上界（整数）");
  expect(TokenType::RBRACKET, "期望 ']'");
  expect(TokenType::KW_OF, "期望 'of'");
  auto elemType = parseBaseType();

  auto node = makeNode(NodeKind::ARRAY_TYPE, line);
  node->ival = std::stoi(low.value); // low，存在 ival
  node->name = top.value;            // top，存在 name（临时复用）
  node->children.push_back(std::move(elemType));
  return node;
}

// RecType ::= RECORD FieldDecList END
ASTPtr
Parser::parseRecType()
{
  int line = peek().line;
  expect(TokenType::KW_RECORD, "期望 'record'");
  auto node = makeNode(NodeKind::REC_TYPE, line);
  // FieldDecList：BaseType|ArrayType IdList ; FieldDecMore
  while(!check(TokenType::KW_END) && !check(TokenType::EOF_TOK))
    {
      ASTPtr typeNode;
      if(check(TokenType::KW_ARRAY))
        typeNode = parseArrayType();
      else
        typeNode = parseBaseType();

      parseIdList(node.get()); // IdList 直接挂到 node
      // 给最后几个 children 标记类型
      expect(TokenType::SEMI, "期望 ';'");

      // 给本轮的 ID 节点打上类型（用 typeNode）
      // 简化：字段的类型信息在语义阶段处理
      node->children.push_back(std::move(typeNode));
    }
  expect(TokenType::KW_END, "期望 'end'（record 结束）");
  return node;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 变量声明
// ═══════════════════════════════════════════════════════════════════════════════

// VarDec ::= ε | VAR VarDecList
ASTPtr
Parser::parseVarDec()
{
  auto node = makeNode(NodeKind::VAR_DEC, peek().line);
  if(!check(TokenType::KW_VAR))
    return node;
  advance(); // 消耗 VAR
  // VarDecList ::= TypeName VarIdList ; VarDecMore
  do
    {
      auto typeNode = parseTypeName();
      auto entry = makeNode(NodeKind::VAR_DEC, peek().line);
      entry->children.push_back(std::move(typeNode));
      parseVarIdList(entry.get());
      expect(TokenType::SEMI, "期望 ';'");
      node->children.push_back(std::move(entry));
    }
  while(!check(TokenType::KW_PROCEDURE) && !check(TokenType::KW_BEGIN)
        && !check(TokenType::EOF_TOK) &&
        // VarDecMore 的 FIRST 集：TypeName 的 FIRST
        (check(TokenType::KW_INTEGER) || check(TokenType::KW_CHAR)
         || check(TokenType::KW_ARRAY) || check(TokenType::KW_RECORD)
         || check(TokenType::ID)));
  return node;
}

// IdList ::= ID IdMore   IdMore ::= ε | , IdList
void
Parser::parseIdList(ASTNode* parent)
{
  do
    {
      auto tok = expect(TokenType::ID, "期望标识符");
      auto id = makeNode(NodeKind::SIMPLE_VAR, tok.line);
      id->name = tok.value;
      parent->children.push_back(std::move(id));
    }
  while(match(TokenType::COMMA));
}

// VarIdList ::= ID VarIdMore   VarIdMore ::= ε | , VarIdList
void
Parser::parseVarIdList(ASTNode* parent)
{
  parseIdList(parent); // 结构相同
}

// ═══════════════════════════════════════════════════════════════════════════════
// 过程声明
// ═══════════════════════════════════════════════════════════════════════════════

// ProcDec ::= ε | ProcDeclaration ProcDecMore
ASTPtr
Parser::parseProcDec()
{
  auto node = makeNode(NodeKind::PROC_DEC, peek().line);
  while(check(TokenType::KW_PROCEDURE))
    {
      advance(); // 消耗 PROCEDURE
      auto procName = expect(TokenType::ID, "期望过程名");
      expect(TokenType::LPAREN, "期望 '('");
      auto params = parseParamList();
      expect(TokenType::RPAREN, "期望 ')'");
      expect(TokenType::SEMI, "期望 ';'");

      auto proc = makeNode(NodeKind::PROC_DEC, procName.line);
      proc->name = procName.value;
      proc->children.push_back(std::move(params));  // 参数
      proc->children.push_back(parseDeclarePart()); // ProcDecPart
      proc->children.push_back(parseProcBody());    // ProcBody（BEGIN...END，无点号）

      node->children.push_back(std::move(proc));
    }
  return node;
}

// ParamList ::= ε | ParamDecList
ASTPtr
Parser::parseParamList()
{
  auto node = makeNode(NodeKind::VAR_DEC, peek().line);
  node->name = "__params__";
  if(check(TokenType::RPAREN))
    return node; // 空参数列表
  // ParamDecList ::= Param ParamMore  ParamMore ::= ε | ; ParamDecList
  do
    {
      node->children.push_back(parseParam());
    }
  while(match(TokenType::SEMI));
  return node;
}

// Param ::= TypeName FormList | VAR TypeName FormList
ASTPtr
Parser::parseParam()
{
  bool isVar = false;
  if(check(TokenType::KW_VAR))
    {
      advance();
      isVar = true;
    }
  auto typeNode = parseTypeName();
  auto param = makeNode(NodeKind::VAR_DEC, peek().line);
  param->isVarParam = isVar;
  param->children.push_back(std::move(typeNode));
  // FormList ::= ID FidMore  FidMore ::= ε | , FormList
  parseIdList(param.get());
  return param;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 语句
// ═══════════════════════════════════════════════════════════════════════════════

// StmList ::= Stm StmMore  StmMore ::= ε | ; StmList
ASTPtr
Parser::parseStmList()
{
  // 返回一个 "block" 容器节点，children 是语句列表
  auto block = makeNode(NodeKind::ASSIGN_STM, peek().line); // 临时借用
  block->name = "__block__";
  block->children.push_back(parseStm());
  while(match(TokenType::SEMI))
    {
      // StmMore：遇到 END/FI/ENDWH 则结束
      if(check(TokenType::KW_END) || check(TokenType::KW_FI)
         || check(TokenType::KW_ENDWH) || check(TokenType::KW_ELSE)
         || check(TokenType::EOF_TOK))
        break;
      block->children.push_back(parseStm());
    }
  return block;
}

// Stm ::= ConditionalStm | LoopStm | InputStm | OutputStm | ReturnStm | ID
// AssCall
ASTPtr
Parser::parseStm()
{
  switch(peek().type)
    {
    case TokenType::KW_IF:
      return parseConditionalStm();
    case TokenType::KW_WHILE:
      return parseLoopStm();
    case TokenType::KW_READ:
      return parseInputStm();
    case TokenType::KW_WRITE:
      return parseOutputStm();
    case TokenType::KW_RETURN:
      return parseReturnStm();
    case TokenType::ID:
      return parseAssignOrCall();
    default:
      syncError("期望语句，得到 '" + peek().value + "'");
      return makeNode(NodeKind::ASSIGN_STM, peek().line);
    }
}

// ID AssCall：看下一个 token 决定是赋值还是调用
ASTPtr
Parser::parseAssignOrCall()
{
  auto idTok = advance(); // 消耗 ID
  if(check(TokenType::LPAREN))
    {
      return parseCallStmRest(idTok.value, idTok.line);
    }
  return parseAssignmentRest(idTok.value, idTok.line);
}

// AssignmentRest ::= VariMore := Exp
ASTPtr
Parser::parseAssignmentRest(const std::string& id, int line)
{
  auto lhs = makeNode(NodeKind::SIMPLE_VAR, line);
  lhs->name = id;
  lhs = parseVariMore(std::move(lhs)); // 处理 [Exp] 或 .field

  expect(TokenType::ASSIGN, "期望 ':='");

  auto node = makeNode(NodeKind::ASSIGN_STM, line);
  node->children.push_back(std::move(lhs));
  node->children.push_back(parseExp());
  return node;
}

// CallStmRest ::= ( ActParamList )
ASTPtr
Parser::parseCallStmRest(const std::string& id, int line)
{
  expect(TokenType::LPAREN, "期望 '('");
  auto node = makeNode(NodeKind::CALL_STM, line);
  node->name = id;
  // ActParamList ::= ε | Exp ActParamMore
  if(!check(TokenType::RPAREN))
    {
      node->children.push_back(parseExp());
      while(match(TokenType::COMMA))
        {
          node->children.push_back(parseExp());
        }
    }
  expect(TokenType::RPAREN, "期望 ')'");
  return node;
}

// IF RelExp THEN StmList ELSE StmList FI
ASTPtr
Parser::parseConditionalStm()
{
  int line = peek().line;
  expect(TokenType::KW_IF, "期望 'if'");
  auto node = makeNode(NodeKind::IF_STM, line);
  node->children.push_back(parseRelExp()); // 条件
  expect(TokenType::KW_THEN, "期望 'then'");
  node->children.push_back(parseStmList()); // then 分支
  expect(TokenType::KW_ELSE, "期望 'else'");
  node->children.push_back(parseStmList()); // else 分支
  expect(TokenType::KW_FI, "期望 'fi'");
  return node;
}

// WHILE RelExp DO StmList ENDWH
ASTPtr
Parser::parseLoopStm()
{
  int line = peek().line;
  expect(TokenType::KW_WHILE, "期望 'while'");
  auto node = makeNode(NodeKind::WHILE_STM, line);
  node->children.push_back(parseRelExp());
  expect(TokenType::KW_DO, "期望 'do'");
  node->children.push_back(parseStmList());
  expect(TokenType::KW_ENDWH, "期望 'endwh'");
  return node;
}

// READ ( Invar )  Invar ::= ID
ASTPtr
Parser::parseInputStm()
{
  int line = peek().line;
  expect(TokenType::KW_READ, "期望 'read'");
  expect(TokenType::LPAREN, "期望 '('");
  auto idTok = expect(TokenType::ID, "期望变量名");
  expect(TokenType::RPAREN, "期望 ')'");
  auto node = makeNode(NodeKind::READ_STM, line);
  node->name = idTok.value;
  return node;
}

// WRITE ( Exp )
ASTPtr
Parser::parseOutputStm()
{
  int line = peek().line;
  expect(TokenType::KW_WRITE, "期望 'write'");
  expect(TokenType::LPAREN, "期望 '('");
  auto node = makeNode(NodeKind::WRITE_STM, line);
  node->children.push_back(parseExp());
  expect(TokenType::RPAREN, "期望 ')'");
  return node;
}

// RETURN ( Exp )
ASTPtr
Parser::parseReturnStm()
{
  int line = peek().line;
  expect(TokenType::KW_RETURN, "期望 'return'");
  expect(TokenType::LPAREN, "期望 '('");
  auto node = makeNode(NodeKind::RETURN_STM, line);
  node->children.push_back(parseExp());
  expect(TokenType::RPAREN, "期望 ')'");
  return node;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 表达式
// ═══════════════════════════════════════════════════════════════════════════════

// RelExp ::= Exp CmpOp Exp
ASTPtr
Parser::parseRelExp()
{
  auto left = parseExp();
  // OtherRelE ::= CmpOp Exp
  std::string op;
  if(check(TokenType::LT))
    {
      op = "<";
      advance();
    }
  else if(check(TokenType::EQ))
    {
      op = "=";
      advance();
    }
  else
    {
      syncError("期望比较运算符 '<' 或 '='");
      return left;
    }
  auto right = parseExp();
  auto node = makeNode(NodeKind::BINARY_EXP, left->line);
  node->name = op;
  node->children.push_back(std::move(left));
  node->children.push_back(std::move(right));
  return node;
}

// Exp ::= Term OtherTerm   OtherTerm ::= ε | AddOp Exp
ASTPtr
Parser::parseExp()
{
  auto left = parseTerm();
  if(check(TokenType::PLUS) || check(TokenType::MINUS))
    {
      std::string op = peek().value;
      advance();
      auto right = parseExp();
      auto node = makeNode(NodeKind::BINARY_EXP, left->line);
      node->name = op;
      node->children.push_back(std::move(left));
      node->children.push_back(std::move(right));
      return node;
    }
  return left;
}

// Term ::= Factor OtherFactor   OtherFactor ::= ε | MultOp Term
ASTPtr
Parser::parseTerm()
{
  auto left = parseFactor();
  if(check(TokenType::TIMES) || check(TokenType::DIVIDE))
    {
      std::string op = peek().value;
      advance();
      auto right = parseTerm();
      auto node = makeNode(NodeKind::BINARY_EXP, left->line);
      node->name = op;
      node->children.push_back(std::move(left));
      node->children.push_back(std::move(right));
      return node;
    }
  return left;
}

// Factor ::= ( Exp ) | INTC | Variable
ASTPtr
Parser::parseFactor()
{
  if(check(TokenType::LPAREN))
    {
      advance();
      auto e = parseExp();
      expect(TokenType::RPAREN, "期望 ')'");
      return e;
    }
  if(check(TokenType::INTC))
    {
      auto tok = advance();
      auto node = makeNode(NodeKind::INT_LITERAL, tok.line);
      node->ival = std::stoi(tok.value);
      node->name = tok.value;
      return node;
    }
  if(check(TokenType::CHARC))
    {
      auto tok = advance();
      auto node = makeNode(NodeKind::CHAR_LITERAL, tok.line);
      node->name = tok.value;
      node->ival = static_cast<int>(tok.value[0]);
      return node;
    }
  return parseVariable();
}

// Variable ::= ID VariMore
ASTPtr
Parser::parseVariable()
{
  auto idTok = expect(TokenType::ID, "期望变量名或整数");
  auto base = makeNode(NodeKind::SIMPLE_VAR, idTok.line);
  base->name = idTok.value;
  auto varNode = makeNode(NodeKind::VAR_EXP, idTok.line);
  varNode->children.push_back(parseVariMore(std::move(base)));
  return varNode;
}

// VariMore ::= ε | [ Exp ] | . FieldVar
ASTPtr
Parser::parseVariMore(ASTPtr base)
{
  if(check(TokenType::LBRACKET))
    {
      advance();
      auto idx = parseExp();
      expect(TokenType::RBRACKET, "期望 ']'");
      auto node = makeNode(NodeKind::INDEX_VAR, base->line);
      node->children.push_back(std::move(base));
      node->children.push_back(std::move(idx));
      return node;
    }
  if(check(TokenType::DOT))
    {
      advance();
      auto fieldTok = expect(TokenType::ID, "期望字段名");
      auto node = makeNode(NodeKind::FIELD_VAR, base->line);
      node->name = fieldTok.value;
      node->children.push_back(std::move(base));
      // FieldVarMore ::= ε | [ Exp ]
      if(check(TokenType::LBRACKET))
        {
          advance();
          node->children.push_back(parseExp());
          expect(TokenType::RBRACKET, "期望 ']'");
        }
      return node;
    }
  return base;
}

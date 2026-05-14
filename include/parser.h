#pragma once
#include <ast.h>
#include <string>
#include <token.h>
#include <vector>

// ─── Parser：Token 流 → AST（LL(1)
// 递归下降）─────────────────────────────────
class Parser
{
public:
  explicit Parser(std::vector<Token> tokens);

  // 入口：解析整个程序，返回根节点
  ASTPtr parseProgram();

  bool
  hasError() const
  {
    return !errors_.empty();
  }
  const std::vector<std::string>&
  errors() const
  {
    return errors_;
  }

private:
  std::vector<Token> tokens_;
  size_t pos_ = 0;
  std::vector<std::string> errors_;

  // ── Token 操作 ────────────────────────────────────────────────────────
  const Token& peek(int offset = 0) const;
  const Token& advance();
  bool check(TokenType t) const;
  bool match(TokenType t);
  const Token& expect(TokenType t, const std::string& msg);

  void syncError(const std::string& msg);

  // ── 文法产生式（对应文法规则编号）────────────────────────────────────
  // Program ::= ProgramHead DeclarePart ProgramBody
  ASTPtr parseProgramHead(); // 规则 2-3
  ASTPtr parseDeclarePart(); // 规则 4
  ASTPtr parseProgramBody(); // 规则 57

  // 类型声明
  ASTPtr parseTypeDec();       // 规则 6-7
  ASTPtr parseTypeDecList();   // 规则 8-10
  ASTPtr parseTypeName();      // 规则 12-14
  ASTPtr parseBaseType();      // 规则 15-16
  ASTPtr parseStructureType(); // 规则 17-18
  ASTPtr parseArrayType();     // 规则 19-21
  ASTPtr parseRecType();       // 规则 22-26

  // 变量声明
  ASTPtr parseVarDec(); // 规则 30-38
  ASTPtr parseVarDecList();
  void parseIdList(ASTNode* parent);    // 规则 27-29
  void parseVarIdList(ASTNode* parent); // 规则 36-38

  // 过程声明
  ASTPtr parseProcDec();   // 规则 39-55
  ASTPtr parseParamList(); // 规则 45-54
  ASTPtr parseParam();     // 规则 50-54

  // 语句
  ASTPtr parseStmList();                                       // 规则 58-60
  ASTPtr parseStm();                                           // 规则 61-66
  ASTPtr parseAssignOrCall();                                  // 规则 67-68
  ASTPtr parseAssignmentRest(const std::string& id, int line); // 规则 69
  ASTPtr parseCallStmRest(const std::string& id, int line);    // 规则 76-80
  ASTPtr parseConditionalStm();                                // 规则 70
  ASTPtr parseLoopStm();                                       // 规则 71
  ASTPtr parseInputStm();                                      // 规则 72-73
  ASTPtr parseOutputStm();                                     // 规则 74
  ASTPtr parseReturnStm();                                     // 规则 75

  // 表达式
  ASTPtr parseRelExp();   // 规则 81-82
  ASTPtr parseExp();      // 规则 83-85
  ASTPtr parseTerm();     // 规则 86-88
  ASTPtr parseFactor();   // 规则 89-91
  ASTPtr parseVariable(); // 规则 92-98
  ASTPtr parseVariMore(ASTPtr base);
};

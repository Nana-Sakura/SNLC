#pragma once
#include "token.h"
#include <string>
#include <vector>

// ─── Lexer：字符流 → Token 流 ──────────────────────────────────────────────────
class Lexer {
public:
    explicit Lexer(std::string source);

    // 返回所有 Token（含末尾的 EOF_TOK）
    std::vector<Token> tokenize();

    // 逐个取 Token（供 Parser 流式消费）
    Token nextToken();

    bool hasError() const { return !errors_.empty(); }
    const std::vector<std::string>& errors() const { return errors_; }

private:
    std::string src_;
    size_t pos_ = 0;
    int line_   = 1;
    int col_    = 1;

    std::vector<std::string> errors_;

    // 基础字符操作
    char peek(int offset = 0) const;
    char advance();
    void skipWhitespaceAndComments();

    // 各类 Token 的扫描函数
    Token readIdentifierOrKeyword();
    Token readNumber();
    Token readOperatorOrPunct();

    // 报错并返回 ERROR token
    Token makeError(const std::string& msg);
    Token makeToken(TokenType t, const std::string& val, int line, int col);
};

#include "../include/lexer.h"
#include <cctype>
#include <stdexcept>

// ─── 构造 ──────────────────────────────────────────────────────────────────────
Lexer::Lexer(std::string source) : src_(std::move(source)) {}

// ─── 公共接口 ──────────────────────────────────────────────────────────────────
std::vector<Token> Lexer::tokenize() {
    std::vector<Token> result;
    while (true) {
        Token tok = nextToken();
        result.push_back(tok);
        if (tok.type == TokenType::EOF_TOK) break;
    }
    return result;
}

Token Lexer::nextToken() {
    skipWhitespaceAndComments();

    if (pos_ >= src_.size()) {
        return makeToken(TokenType::EOF_TOK, "", line_, col_);
    }

    char c = peek();

    // 标识符或关键字
    if (std::isalpha(c) || c == '_') {
        return readIdentifierOrKeyword();
    }

    // 整型常量
    if (std::isdigit(c)) {
        return readNumber();
    }

    // 运算符和标点
    return readOperatorOrPunct();
}

// ─── 字符操作 ──────────────────────────────────────────────────────────────────
char Lexer::peek(int offset) const {
    size_t idx = pos_ + offset;
    if (idx >= src_.size()) return '\0';
    return src_[idx];
}

char Lexer::advance() {
    char c = src_[pos_++];
    if (c == '\n') { ++line_; col_ = 1; }
    else           { ++col_; }
    return c;
}

// ─── 跳过空白和注释 ───────────────────────────────────────────────────────────
// SNL 注释：{ ... }（花括号包围，不可嵌套）
void Lexer::skipWhitespaceAndComments() {
    while (pos_ < src_.size()) {
        char c = peek();
        if (std::isspace(c)) {
            advance();
        } else if (c == '{') {
            // 跳过注释
            int startLine = line_;
            advance(); // 消耗 '{'
            while (pos_ < src_.size() && peek() != '}') advance();
            if (pos_ >= src_.size()) {
                errors_.push_back("第 " + std::to_string(startLine) +
                                  " 行：注释未闭合（缺少 '}'）");
                return;
            }
            advance(); // 消耗 '}'
        } else {
            break;
        }
    }
}

// ─── 标识符 / 关键字 ───────────────────────────────────────────────────────────
Token Lexer::readIdentifierOrKeyword() {
    int startLine = line_, startCol = col_;
    std::string val;
    while (pos_ < src_.size() && (std::isalnum(peek()) || peek() == '_')) {
        val += advance();
    }

    // SNL 关键字不区分大小写：统一转小写后查表
    std::string lower = val;
    for (char& ch : lower) ch = std::tolower(ch);

    auto it = KEYWORDS.find(lower);
    if (it != KEYWORDS.end()) {
        return makeToken(it->second, val, startLine, startCol);
    }
    return makeToken(TokenType::ID, val, startLine, startCol);
}

// ─── 整型常量 ──────────────────────────────────────────────────────────────────
Token Lexer::readNumber() {
    int startLine = line_, startCol = col_;
    std::string val;
    while (pos_ < src_.size() && std::isdigit(peek())) {
        val += advance();
    }
    // SNL 只有整型，遇到小数点是 '..' 语法，不是浮点数
    return makeToken(TokenType::INTC, val, startLine, startCol);
}

// ─── 运算符和标点 ──────────────────────────────────────────────────────────────
Token Lexer::readOperatorOrPunct() {
    int startLine = line_, startCol = col_;
    char c = advance();

    switch (c) {
        case '+': return makeToken(TokenType::PLUS,     "+", startLine, startCol);
        case '-': return makeToken(TokenType::MINUS,    "-", startLine, startCol);
        case '*': return makeToken(TokenType::TIMES,    "*", startLine, startCol);
        case '/': return makeToken(TokenType::DIVIDE,   "/", startLine, startCol);
        case '<': return makeToken(TokenType::LT,       "<", startLine, startCol);
        case '=': return makeToken(TokenType::EQ,       "=", startLine, startCol);
        case '(': return makeToken(TokenType::LPAREN,   "(", startLine, startCol);
        case ')': return makeToken(TokenType::RPAREN,   ")", startLine, startCol);
        case '[': return makeToken(TokenType::LBRACKET, "[", startLine, startCol);
        case ']': return makeToken(TokenType::RBRACKET, "]", startLine, startCol);
        case ';': return makeToken(TokenType::SEMI,     ";", startLine, startCol);
        case ',': return makeToken(TokenType::COMMA,    ",", startLine, startCol);

        case ':':
            // 必须是 ':='
            if (peek() == '=') {
                advance();
                return makeToken(TokenType::ASSIGN, ":=", startLine, startCol);
            }
            return makeError("第 " + std::to_string(startLine) +
                             " 行 " + std::to_string(startCol) +
                             " 列：非法字符 ':'，是否想写 ':='？");

        case '.':
            // '..' 或单独的 '.'
            if (peek() == '.') {
                advance();
                return makeToken(TokenType::DOTDOT, "..", startLine, startCol);
            }
            return makeToken(TokenType::DOT, ".", startLine, startCol);

        default:
            return makeError("第 " + std::to_string(startLine) +
                             " 行 " + std::to_string(startCol) +
                             " 列：未知字符 '" + c + "'");
    }
}

// ─── 工具 ──────────────────────────────────────────────────────────────────────
Token Lexer::makeToken(TokenType t, const std::string& val, int line, int col) {
    return Token(t, val, line, col);
}

Token Lexer::makeError(const std::string& msg) {
    errors_.push_back(msg);
    return Token(TokenType::ERROR, msg, line_, col_);
}

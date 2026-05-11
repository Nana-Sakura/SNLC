#pragma once
#include <string>
#include <unordered_map>

// ─── Token 种类 ────────────────────────────────────────────────────────────────
enum class TokenType {
    // 关键字
    KW_PROGRAM, KW_PROCEDURE, KW_TYPE, KW_VAR,
    KW_BEGIN, KW_END,
    KW_IF, KW_THEN, KW_ELSE, KW_FI,
    KW_WHILE, KW_DO, KW_ENDWH,
    KW_RECORD, KW_ARRAY, KW_OF,
    KW_INTEGER, KW_CHAR,
    KW_READ, KW_WRITE, KW_RETURN,

    // 标识符 / 字面量
    ID,     // 变量名、函数名等
    INTC,   // 整型常量

    // 运算符
    ASSIGN,     // :=
    LT,         // <
    EQ,         // =
    PLUS,       // +
    MINUS,      // -
    TIMES,      // *
    DIVIDE,     // /

    // 分隔符
    LPAREN,     // (
    RPAREN,     // )
    LBRACKET,   // [
    RBRACKET,   // ]
    SEMI,       // ;
    COMMA,      // ,
    DOT,        // .
    DOTDOT,     // ..

    // 特殊
    EOF_TOK,
    ERROR
};

// ─── 关键字表 ──────────────────────────────────────────────────────────────────
inline const std::unordered_map<std::string, TokenType> KEYWORDS = {
    {"program",   TokenType::KW_PROGRAM},
    {"procedure", TokenType::KW_PROCEDURE},
    {"type",      TokenType::KW_TYPE},
    {"var",       TokenType::KW_VAR},
    {"begin",     TokenType::KW_BEGIN},
    {"end",       TokenType::KW_END},
    {"if",        TokenType::KW_IF},
    {"then",      TokenType::KW_THEN},
    {"else",      TokenType::KW_ELSE},
    {"fi",        TokenType::KW_FI},
    {"while",     TokenType::KW_WHILE},
    {"do",        TokenType::KW_DO},
    {"endwh",     TokenType::KW_ENDWH},
    {"record",    TokenType::KW_RECORD},
    {"array",     TokenType::KW_ARRAY},
    {"of",        TokenType::KW_OF},
    {"integer",   TokenType::KW_INTEGER},
    {"char",      TokenType::KW_CHAR},
    {"read",      TokenType::KW_READ},
    {"write",     TokenType::KW_WRITE},
    {"return",    TokenType::KW_RETURN},
};

// ─── Token 结构体 ──────────────────────────────────────────────────────────────
struct Token {
    TokenType type;
    std::string value;   // 原始文本
    int line;            // 行号（从 1 开始）
    int col;             // 列号（从 1 开始）

    Token(TokenType t, std::string v, int l, int c)
        : type(t), value(std::move(v)), line(l), col(c) {}
};

// 调试用：把 TokenType 转成可读字符串
std::string tokenTypeName(TokenType t);

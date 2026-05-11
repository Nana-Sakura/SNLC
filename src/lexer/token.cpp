#include "../include/token.h"

std::string tokenTypeName(TokenType t) {
    switch (t) {
        case TokenType::KW_PROGRAM:   return "PROGRAM";
        case TokenType::KW_PROCEDURE: return "PROCEDURE";
        case TokenType::KW_TYPE:      return "TYPE";
        case TokenType::KW_VAR:       return "VAR";
        case TokenType::KW_BEGIN:     return "BEGIN";
        case TokenType::KW_END:       return "END";
        case TokenType::KW_IF:        return "IF";
        case TokenType::KW_THEN:      return "THEN";
        case TokenType::KW_ELSE:      return "ELSE";
        case TokenType::KW_FI:        return "FI";
        case TokenType::KW_WHILE:     return "WHILE";
        case TokenType::KW_DO:        return "DO";
        case TokenType::KW_ENDWH:     return "ENDWH";
        case TokenType::KW_RECORD:    return "RECORD";
        case TokenType::KW_ARRAY:     return "ARRAY";
        case TokenType::KW_OF:        return "OF";
        case TokenType::KW_INTEGER:   return "INTEGER";
        case TokenType::KW_CHAR:      return "CHAR";
        case TokenType::KW_READ:      return "READ";
        case TokenType::KW_WRITE:     return "WRITE";
        case TokenType::KW_RETURN:    return "RETURN";
        case TokenType::ID:           return "ID";
        case TokenType::INTC:         return "INTC";
        case TokenType::ASSIGN:       return "ASSIGN(:=)";
        case TokenType::LT:           return "LT(<)";
        case TokenType::EQ:           return "EQ(=)";
        case TokenType::PLUS:         return "PLUS(+)";
        case TokenType::MINUS:        return "MINUS(-)";
        case TokenType::TIMES:        return "TIMES(*)";
        case TokenType::DIVIDE:       return "DIVIDE(/)";
        case TokenType::LPAREN:       return "LPAREN(()";
        case TokenType::RPAREN:       return "RPAREN())";
        case TokenType::LBRACKET:     return "LBRACKET([)";
        case TokenType::RBRACKET:     return "RBRACKET(])";
        case TokenType::SEMI:         return "SEMI(;)";
        case TokenType::COMMA:        return "COMMA(,)";
        case TokenType::DOT:          return "DOT(.)";
        case TokenType::DOTDOT:       return "DOTDOT(..)";
        case TokenType::EOF_TOK:      return "EOF";
        case TokenType::ERROR:        return "ERROR";
    }
    return "UNKNOWN";
}

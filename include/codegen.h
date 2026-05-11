#pragma once
#include "ast.h"
#include "semantic.h"
#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <algorithm>

// ─── MIPS32 代码生成器 ────────────────────────────────────────────────────────
//
//  调用约定（仿 MIPS O32）：
//    $a0-$a3  前 4 个实参（多余的压栈）
//    $v0      返回值
//    $ra      返回地址（由 jal 设置）
//    $fp      帧指针
//    $sp      栈顶指针
//    $t8/$t9  内部临时寄存器（不对外暴露）
//
//  栈帧布局（frameSize = F）：
//    $fp+0    = old $ra（存在 $sp+F-4）
//    $fp-4    = old $fp（存在 $sp+F-8）
//    $fp-8    = 第一个局部变量（sym.offset = -4 相对 $fp）
//    ...
//
class CodeGen {
public:
    explicit CodeGen(Scope* globalScope);

    // 注册过程作用域（由 main 在语义分析后调用）
    void registerProcScope(const std::string& name, Scope* scope) {
        procScopeMap_[name] = scope;
    }

    std::string generate(ASTNode* root);

private:
    Scope* global_;
    Scope* current_;
    std::unordered_map<std::string, Scope*> procScopeMap_;
    // 过程名 → 按声明顺序排列的参数 Symbol* 列表
    std::unordered_map<std::string, std::vector<Symbol*>> procParamOrder_;

    std::ostringstream out_;
    std::ostringstream data_;
    int labelCounter_ = 0;
    int tempReg_      = 0;

    // 当前过程名和帧大小（用于 return 和 epilogue）
    std::string currentProcLabel_;
    int         currentFrameSize_ = 0;

    // ── 工具 ──────────────────────────────────────────────────────────────
    std::string newLabel(const std::string& hint = "L");
    std::string allocTemp();
    void emit(const std::string& instr);
    void emitLabel(const std::string& label);
    void emitComment(const std::string& comment);
    void emitData(const std::string& line);

    // ── 帧 ────────────────────────────────────────────────────────────────
    void genPrologue(int frameSize);
    void genEpilogue(int frameSize);

    // ── 过程/语句 ─────────────────────────────────────────────────────────
    void genProgram(ASTNode* node);
    void genProcDec(ASTNode* node);
    void genStmList(ASTNode* node);
    void genStm(ASTNode* node);
    void genAssign(ASTNode* node);
    void genCall(ASTNode* node);
    void genIf(ASTNode* node);
    void genWhile(ASTNode* node);
    void genRead(ASTNode* node);
    void genWrite(ASTNode* node);
    void genReturn(ASTNode* node);

    // ── 表达式 ────────────────────────────────────────────────────────────
    // 结果放入 dest 寄存器，返回 dest（或实际使用的寄存器名）
    std::string genExp(ASTNode* node, const std::string& dest = "");
    std::string genRelExp(ASTNode* node, const std::string& dest = "");

    // ── 变量寻址 ──────────────────────────────────────────────────────────
    std::string genVarAddr(ASTNode* node);          // 返回 "off($fp)" 或 "0($tN)"
    void        genStore(ASTNode* lhs, const std::string& val);  // 写回 LHS
    void        genLoadAddr(ASTNode* node, const std::string& dest); // 取地址（VAR 参数）
    void        genIndexAddr(ASTNode* node);  // 数组地址 → $t8
    void        genFieldAddr(ASTNode* node);  // 字段地址 → $t8
    void        pushReg(const std::string& r);
    void        popReg(const std::string& r);
};

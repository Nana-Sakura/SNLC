#pragma once
#include "ast.h"
#include "semantic.h"
#include <string>
#include <sstream>
#include <vector>

// ─── MIPS32 代码生成器 ────────────────────────────────────────────────────────
//
//  调用约定（仿 MIPS O32）：
//    $a0-$a3  前四个实参（多余的压栈）
//    $v0      返回值
//    $ra      返回地址（由 jal 设置）
//    $fp      帧指针（指向当前栈帧底部）
//    $sp      栈顶指针
//
//  栈帧布局（从高到低）：
//    [old $fp]       <- 进入时保存
//    [old $ra]
//    [参数区]        <- VAR 参数传地址
//    [局部变量区]    <- 按 Symbol::offset 寻址（负偏移）
//
class CodeGen {
public:
    CodeGen(Scope* globalScope);

    // 对 AST 生成 MIPS32 汇编文本
    std::string generate(ASTNode* root);

private:
    Scope* global_;
    Scope* current_ = nullptr;

    std::ostringstream out_;    // 汇编输出流
    int labelCounter_ = 0;     // 唯一标签计数器
    int tempReg_ = 0;          // 临时寄存器轮转（$t0-$t9）

    // ── 工具 ──────────────────────────────────────────────────────────────
    std::string newLabel(const std::string& hint = "L");
    std::string tempReg();   // 分配下一个 $tN（简单轮转）
    void emit(const std::string& instr);
    void emitLabel(const std::string& label);
    void emitComment(const std::string& comment);

    // ── 代码段生成 ────────────────────────────────────────────────────────
    void genPrologue(const std::string& procLabel, int frameSize);
    void genEpilogue(const std::string& procLabel);

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

    // 表达式：结果放入目标寄存器，返回寄存器名
    std::string genExp(ASTNode* node, const std::string& dest = "");
    std::string genRelExp(ASTNode* node, const std::string& dest = "");

    // 变量地址计算：返回 "offset($base)" 形式的内存地址字符串
    std::string genVarAddr(ASTNode* node);

    // 数据段（全局/静态分配）
    std::ostringstream data_;
    void emitData(const std::string& line);
};

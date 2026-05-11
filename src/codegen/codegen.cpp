#include "../include/codegen.h"
#include <stdexcept>
#include <cassert>

// ─── 构造 ──────────────────────────────────────────────────────────────────────
CodeGen::CodeGen(Scope* globalScope) : global_(globalScope), current_(globalScope) {}

// ─── 工具 ──────────────────────────────────────────────────────────────────────
std::string CodeGen::newLabel(const std::string& hint) {
    return hint + "_" + std::to_string(labelCounter_++);
}

std::string CodeGen::tempReg() {
    std::string r = "$t" + std::to_string(tempReg_ % 10);
    tempReg_++;
    return r;
}

void CodeGen::emit(const std::string& instr) {
    out_ << "    " << instr << "\n";
}

void CodeGen::emitLabel(const std::string& label) {
    out_ << label << ":\n";
}

void CodeGen::emitComment(const std::string& comment) {
    out_ << "    # " << comment << "\n";
}

void CodeGen::emitData(const std::string& line) {
    data_ << line << "\n";
}

// ─── 主入口 ───────────────────────────────────────────────────────────────────
std::string CodeGen::generate(ASTNode* root) {
    // 数据段
    emitData(".data");
    emitData("__newline: .asciiz \"\\n\"");

    // 代码段
    out_ << ".text\n";
    out_ << ".globl main\n";

    genProgram(root);

    // 拼接 data + text
    return data_.str() + "\n" + out_.str();
}

// ─── 程序体 ───────────────────────────────────────────────────────────────────
void CodeGen::genProgram(ASTNode* node) {
    if (!node || node->children.size() < 3) return;
    // children: [ProgramHead, DeclarePart, ProgramBody]
    ASTNode* decl = node->children[1].get();

    // 先生成所有过程
    if (decl && decl->children.size() >= 3) {
        genProcDec(decl->children[2].get());
    }

    // main 入口
    emitLabel("main");
    genPrologue("main", 64); // 暂用固定栈帧，语义分析完善后动态计算

    ASTNode* body = node->children[2].get();
    genStmList(body);

    // 正常退出
    emitComment("exit");
    emit("li   $v0, 10");
    emit("syscall");

    genEpilogue("main");
}

// ─── 过程声明 ──────────────────────────────────────────────────────────────────
void CodeGen::genProcDec(ASTNode* node) {
    if (!node) return;
    for (auto& proc : node->children) {
        std::string label = proc->name;
        emitLabel(label);
        genPrologue(label, 128); // 固定帧大小，待语义完善后从符号表取

        // 过程体的语句（children[2]）
        if (proc->children.size() > 2) {
            genStmList(proc->children[2].get());
        }

        genEpilogue(label);
    }
}

// ─── 栈帧管理 ──────────────────────────────────────────────────────────────────
//  进入时：
//    addiu $sp, $sp, -frameSize
//    sw    $ra, (frameSize-4)($sp)
//    sw    $fp, (frameSize-8)($sp)
//    addiu $fp, $sp, frameSize-4
void CodeGen::genPrologue(const std::string& procLabel, int frameSize) {
    emitComment("prologue: " + procLabel);
    emit("addiu $sp, $sp, -" + std::to_string(frameSize));
    emit("sw    $ra, " + std::to_string(frameSize - 4) + "($sp)");
    emit("sw    $fp, " + std::to_string(frameSize - 8) + "($sp)");
    emit("addiu $fp, $sp, " + std::to_string(frameSize - 4));
}

void CodeGen::genEpilogue(const std::string& procLabel) {
    emitComment("epilogue: " + procLabel);
    std::string label = "__ret_" + procLabel;
    emitLabel(label);
    // frameSize 需要和 prologue 一致，暂用 64/128
    // 真实实现：从符号表取 currentFrame.size
    int frameSize = (procLabel == "main") ? 64 : 128;
    emit("lw    $ra, " + std::to_string(frameSize - 4) + "($sp)");
    emit("lw    $fp, " + std::to_string(frameSize - 8) + "($sp)");
    emit("addiu $sp, $sp, " + std::to_string(frameSize));
    emit("jr    $ra");
    out_ << "\n";
}

// ─── 语句 ──────────────────────────────────────────────────────────────────────
void CodeGen::genStmList(ASTNode* node) {
    if (!node) return;
    for (auto& stm : node->children) {
        genStm(stm.get());
    }
}

void CodeGen::genStm(ASTNode* node) {
    if (!node) return;
    switch (node->kind) {
        case NodeKind::ASSIGN_STM:
            if (node->name != "__block__") genAssign(node);
            else genStmList(node); // block 容器
            break;
        case NodeKind::CALL_STM:   genCall(node);  break;
        case NodeKind::IF_STM:     genIf(node);    break;
        case NodeKind::WHILE_STM:  genWhile(node); break;
        case NodeKind::READ_STM:   genRead(node);  break;
        case NodeKind::WRITE_STM:  genWrite(node); break;
        case NodeKind::RETURN_STM: genReturn(node);break;
        default:
            // block 容器或其他
            for (auto& c : node->children) genStm(c.get());
            break;
    }
}

// ─── 赋值 ──────────────────────────────────────────────────────────────────────
void CodeGen::genAssign(ASTNode* node) {
    // children[0] = LHS 变量, children[1] = RHS 表达式
    if (node->children.size() < 2) return;
    emitComment("assign");
    std::string val = genExp(node->children[1].get(), "$t0");
    std::string addr = genVarAddr(node->children[0].get());
    emit("sw    " + val + ", " + addr);
}

// ─── 过程调用 ──────────────────────────────────────────────────────────────────
void CodeGen::genCall(ASTNode* node) {
    emitComment("call " + node->name);
    // 前 4 个参数放 $a0-$a3
    static const char* argRegs[] = {"$a0","$a1","$a2","$a3"};
    int i = 0;
    for (auto& arg : node->children) {
        std::string dest = (i < 4) ? argRegs[i] : "$t0";
        std::string val = genExp(arg.get(), dest);
        if (i < 4 && val != dest) emit("move  " + std::string(argRegs[i]) + ", " + val);
        if (i >= 4) {
            // 超出 4 个参数：压栈（待实现）
            emit("addiu $sp, $sp, -4");
            emit("sw    " + val + ", 0($sp)");
        }
        ++i;
    }
    emit("jal   " + node->name);
}

// ─── if ────────────────────────────────────────────────────────────────────────
void CodeGen::genIf(ASTNode* node) {
    // children: [condition, thenBlock, elseBlock]
    std::string labelElse = newLabel("else");
    std::string labelEnd  = newLabel("fi");

    emitComment("if");
    std::string cond = genRelExp(node->children[0].get(), "$t0");
    emit("beqz  " + cond + ", " + labelElse);

    genStmList(node->children[1].get()); // then
    emit("j     " + labelEnd);

    emitLabel(labelElse);
    genStmList(node->children[2].get()); // else

    emitLabel(labelEnd);
}

// ─── while ─────────────────────────────────────────────────────────────────────
void CodeGen::genWhile(ASTNode* node) {
    std::string labelTop = newLabel("while");
    std::string labelEnd = newLabel("endwh");

    emitLabel(labelTop);
    emitComment("while condition");
    std::string cond = genRelExp(node->children[0].get(), "$t0");
    emit("beqz  " + cond + ", " + labelEnd);

    genStmList(node->children[1].get());
    emit("j     " + labelTop);
    emitLabel(labelEnd);
}

// ─── read / write ──────────────────────────────────────────────────────────────
void CodeGen::genRead(ASTNode* node) {
    // syscall 5：读整数到 $v0
    emitComment("read " + node->name);
    emit("li    $v0, 5");
    emit("syscall");
    // 把结果存入变量
    // TODO：查符号表获取偏移，暂用占位
    emit("# store $v0 -> " + node->name + " (TODO: resolve offset)");
}

void CodeGen::genWrite(ASTNode* node) {
    // syscall 1：打印整数（$a0）
    emitComment("write");
    std::string val = genExp(node->children[0].get(), "$a0");
    if (val != "$a0") emit("move  $a0, " + val);
    emit("li    $v0, 1");
    emit("syscall");
    // 打印换行
    emit("la    $a0, __newline");
    emit("li    $v0, 4");
    emit("syscall");
}

void CodeGen::genReturn(ASTNode* node) {
    emitComment("return");
    if (!node->children.empty()) {
        std::string val = genExp(node->children[0].get(), "$v0");
        if (val != "$v0") emit("move  $v0, " + val);
    }
    // 跳到当前过程的 epilogue（TODO：知道当前过程名）
    emit("j     __ret_main  # TODO: use current proc name");
}

// ─── 表达式 ───────────────────────────────────────────────────────────────────
std::string CodeGen::genExp(ASTNode* node, const std::string& dest) {
    if (!node) return "$zero";
    std::string d = dest.empty() ? tempReg() : dest;

    switch (node->kind) {
        case NodeKind::INT_LITERAL:
            emit("li    " + d + ", " + std::to_string(node->ival));
            return d;

        case NodeKind::BINARY_EXP: {
            std::string l = genExp(node->children[0].get(), d);
            std::string r = genExp(node->children[1].get(), "$t9"); // 临时
            if (node->name == "+")      emit("add   " + d + ", " + l + ", " + r);
            else if (node->name == "-") emit("sub   " + d + ", " + l + ", " + r);
            else if (node->name == "*") emit("mul   " + d + ", " + l + ", " + r);
            else if (node->name == "/") {
                emit("div   " + l + ", " + r);
                emit("mflo  " + d);
            }
            return d;
        }

        case NodeKind::VAR_EXP:
        case NodeKind::SIMPLE_VAR: {
            std::string addr = genVarAddr(node->children.empty() ? node : node->children[0].get());
            emit("lw    " + d + ", " + addr);
            return d;
        }

        default:
            emit("li    " + d + ", 0  # unknown exp");
            return d;
    }
}

// RelExp：生成比较结果（0 或 1）放入 dest
std::string CodeGen::genRelExp(ASTNode* node, const std::string& dest) {
    if (!node || node->kind != NodeKind::BINARY_EXP) return "$zero";
    std::string d = dest.empty() ? tempReg() : dest;
    std::string l = genExp(node->children[0].get(), d);
    std::string r = genExp(node->children[1].get(), "$t9");
    if (node->name == "<")      emit("slt   " + d + ", " + l + ", " + r);
    else if (node->name == "=") emit("seq   " + d + ", " + l + ", " + r);
    else                        emit("li    " + d + ", 0  # unknown cmp");
    return d;
}

// 变量地址：返回 "offset($fp)" 字符串
// 真实实现需查符号表；此处占位等语义分析完善后接入
std::string CodeGen::genVarAddr(ASTNode* node) {
    if (!node) return "0($fp)";
    if (node->kind == NodeKind::SIMPLE_VAR) {
        // TODO：current_->lookup(node->name)->offset
        return "0($fp)  # var:" + node->name;
    }
    if (node->kind == NodeKind::INDEX_VAR) {
        // TODO：base addr + (index - low) * elemSize
        return "0($fp)  # index var TODO";
    }
    if (node->kind == NodeKind::FIELD_VAR) {
        // TODO：base addr + field offset
        return "0($fp)  # field var TODO";
    }
    return "0($fp)";
}

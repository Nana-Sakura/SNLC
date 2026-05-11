#include "../include/codegen.h"
#include <stdexcept>
#include <cassert>

// ═══════════════════════════════════════════════════════════════════════════════
// 工具
// ═══════════════════════════════════════════════════════════════════════════════

CodeGen::CodeGen(Scope* globalScope) : global_(globalScope), current_(globalScope) {}

std::string CodeGen::newLabel(const std::string& hint) {
    return hint + "_" + std::to_string(labelCounter_++);
}

// 简单轮转临时寄存器 $t0-$t7（$t8/$t9 保留给内部计算用）
std::string CodeGen::allocTemp() {
    std::string r = "$t" + std::to_string(tempReg_ % 8);
    tempReg_ = (tempReg_ + 1) % 8;
    return r;
}

void CodeGen::emit(const std::string& instr)          { out_ << "    " << instr << "\n"; }
void CodeGen::emitLabel(const std::string& label)     { out_ << label << ":\n"; }
void CodeGen::emitComment(const std::string& comment) { out_ << "    # " << comment << "\n"; }
void CodeGen::emitData(const std::string& line)       { data_ << line << "\n"; }

// ── 计算帧大小（8 字节对齐）─────────────────────────────────────────────────
// 固定开销 8 字节（保存 $ra + $fp），局部变量按 scope->localSize
static int frameSize(int localBytes) {
    int total = 8 + localBytes; // $ra(4) + $fp(4) + locals
    return (total + 7) & ~7;   // 8 字节对齐
}

// ═══════════════════════════════════════════════════════════════════════════════
// 主入口
// ═══════════════════════════════════════════════════════════════════════════════

std::string CodeGen::generate(ASTNode* root) {
    emitData(".data");
    emitData("__newline: .asciiz \"\\n\"");
    out_ << ".text\n";
    out_ << ".globl main\n\n";
    genProgram(root);
    return data_.str() + "\n" + out_.str();
}

// ═══════════════════════════════════════════════════════════════════════════════
// 程序 & 过程
// ═══════════════════════════════════════════════════════════════════════════════

void CodeGen::genProgram(ASTNode* node) {
    if (!node || node->children.size() < 3) return;
    ASTNode* decl = node->children[1].get();

    // 先生成所有过程（嵌套在 DeclarePart->ProcDec 里）
    if (decl && decl->children.size() >= 3)
        genProcDec(decl->children[2].get());

    // main 的帧大小来自全局作用域的 localSize
    int fsize = frameSize(global_->localSize);
    currentFrameSize_ = fsize;
    currentProcLabel_ = "main";

    emitLabel("main");
    genPrologue(fsize);
    genStmList(node->children[2].get());

    emitComment("normal exit");
    emit("li    $v0, 10");
    emit("syscall");
    genEpilogue(fsize);
    out_ << "\n";
}

void CodeGen::genProcDec(ASTNode* node) {
    if (!node) return;
    for (auto& proc : node->children) {
        // 切换到过程的作用域
        Symbol* procSym = current_->lookup(proc->name);
        // procSym->offset 被语义分析用来暂存 localSize
        int localBytes = procSym ? procSym->offset : 0;
        int fsize = frameSize(localBytes);

        // 找到过程的子作用域：遍历 scopes_ 找 parent == current_ 且含有过程的局部变量
        // 简化：推进 current_ 到下一个子作用域（深度优先顺序与语义分析一致）
        // 这里用一个小技巧：保存 current_ 然后从全局重新找
        // 更实用的方法：把 Scope* 存入 Symbol，语义分析阶段填充

        // 生成过程标签和帧
        std::string savedLabel = currentProcLabel_;
        int savedFrame = currentFrameSize_;
        currentProcLabel_ = proc->name;
        currentFrameSize_ = fsize;

        emitLabel(proc->name);
        genPrologue(fsize);

        // 切换到过程作用域
        Scope* procScope = nullptr;
        {
            auto it2 = procScopeMap_.find(proc->name);
            if (it2 != procScopeMap_.end()) procScope = it2->second;
        }
        Scope* savedScope = current_;
        if (procScope) current_ = procScope;

        // ── 按 AST 声明顺序建立参数列表，并把 $a0-$a3 保存到栈帧 ────────
        if (procScope && !proc->children.empty()) {
            static const char* argRegs[] = {"$a0","$a1","$a2","$a3"};
            int ai = 0;
            std::vector<Symbol*> orderedParams;
            ASTNode* paramList = proc->children[0].get();
            for (auto& paramGroup : paramList->children) {
                for (size_t k = 1; k < paramGroup->children.size(); ++k) {
                    auto* id = paramGroup->children[k].get();
                    if (id->kind != NodeKind::SIMPLE_VAR) continue;
                    Symbol* sym = procScope->lookupLocal(id->name);
                    if (!sym) continue;
                    orderedParams.push_back(sym);
                    if (ai < 4) {
                        emitComment("save arg " + id->name);
                        emit("sw    " + std::string(argRegs[ai]) + ", "
                             + std::to_string(sym->offset) + "($fp)");
                    }
                    ++ai;
                }
            }
            // 记录声明顺序，供 genCall 使用
            procParamOrder_[proc->name] = orderedParams;
        }

        if (proc->children.size() > 2)
            genStmList(proc->children[2].get());

        current_ = savedScope;
        genEpilogue(fsize);
        out_ << "\n";

        currentProcLabel_ = savedLabel;
        currentFrameSize_ = savedFrame;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 栈帧
// ═══════════════════════════════════════════════════════════════════════════════
//
//  栈帧布局（frameSize = F 字节）：
//
//   $sp+F-4  <- $fp 指向这里（= 进入时 old $sp - 4）
//   $sp+F-4  : old $ra
//   $sp+F-8  : old $fp
//   $sp+F-12 : 第一个局部变量（offset = -4 相对于 $fp）
//   $sp+F-16 : 第二个局部变量（offset = -8）
//   ...
//   $sp      <- 当前栈顶
//
void CodeGen::genPrologue(int fsize) {
    emitComment("prologue  frame=" + std::to_string(fsize));
    emit("addiu $sp, $sp, -" + std::to_string(fsize));
    emit("sw    $ra, " + std::to_string(fsize - 4) + "($sp)");
    emit("sw    $fp, " + std::to_string(fsize - 8) + "($sp)");
    emit("addiu $fp, $sp, " + std::to_string(fsize - 4));
}

void CodeGen::genEpilogue(int fsize) {
    emitComment("epilogue  frame=" + std::to_string(fsize));
    emitLabel("__ret_" + currentProcLabel_);
    emit("lw    $ra, " + std::to_string(fsize - 4) + "($sp)");
    emit("lw    $fp, " + std::to_string(fsize - 8) + "($sp)");
    emit("addiu $sp, $sp, " + std::to_string(fsize));
    emit("jr    $ra");
}

// ═══════════════════════════════════════════════════════════════════════════════
// 语句
// ═══════════════════════════════════════════════════════════════════════════════

void CodeGen::genStmList(ASTNode* node) {
    if (!node) return;
    for (auto& stm : node->children)
        genStm(stm.get());
}

void CodeGen::genStm(ASTNode* node) {
    if (!node) return;
    if (node->name == "__block__") { genStmList(node); return; }
    switch (node->kind) {
        case NodeKind::ASSIGN_STM: genAssign(node); break;
        case NodeKind::CALL_STM:   genCall(node);   break;
        case NodeKind::IF_STM:     genIf(node);     break;
        case NodeKind::WHILE_STM:  genWhile(node);  break;
        case NodeKind::READ_STM:   genRead(node);   break;
        case NodeKind::WRITE_STM:  genWrite(node);  break;
        case NodeKind::RETURN_STM: genReturn(node); break;
        default:
            for (auto& c : node->children) genStm(c.get());
            break;
    }
}

// ── 赋值 ─────────────────────────────────────────────────────────────────────
void CodeGen::genAssign(ASTNode* node) {
    if (node->children.size() < 2) return;
    emitComment("assign");
    // 先算 RHS 到 $t8，再写地址
    std::string val = genExp(node->children[1].get(), "$t8");
    genStore(node->children[0].get(), val);
}

// ── 过程调用 ─────────────────────────────────────────────────────────────────
void CodeGen::genCall(ASTNode* node) {
    emitComment("call " + node->name);
    // 找过程的参数信息（判断哪些是 VAR 参数）
    Symbol* procSym = current_->lookup(node->name);
    Scope* procScope = nullptr;
    auto it = procScopeMap_.find(node->name);
    if (it != procScopeMap_.end()) procScope = it->second;

    static const char* argRegs[] = {"$a0","$a1","$a2","$a3"};
    // 按声明顺序取参数列表（已在 genProcDec 建好）
    std::vector<Symbol*> paramSyms;
    {
        auto it2 = procParamOrder_.find(node->name);
        if (it2 != procParamOrder_.end()) paramSyms = it2->second;
    }

    int i = 0;
    for (auto& arg : node->children) {
        bool isVarArg = (i < (int)paramSyms.size() && paramSyms[i]->isVarParam);
        std::string dest = (i < 4) ? argRegs[i] : "$t8";

        if (isVarArg) {
            // VAR 参数：传地址（lea）
            genLoadAddr(arg.get(), dest);
        } else {
            std::string val = genExp(arg.get(), dest);
            if (i < 4 && val != dest) emit("move  " + std::string(argRegs[i]) + ", " + val);
        }

        if (i >= 4) {
            emit("addiu $sp, $sp, -4");
            emit("sw    " + dest + ", 0($sp)");
        }
        ++i;
    }
    emit("jal   " + node->name);
    (void)procSym;
}

// ── if ───────────────────────────────────────────────────────────────────────
void CodeGen::genIf(ASTNode* node) {
    if (node->children.size() < 3) return;
    std::string lElse = newLabel("else");
    std::string lEnd  = newLabel("fi");
    emitComment("if");
    std::string cond = genRelExp(node->children[0].get(), "$t8");
    emit("beqz  " + cond + ", " + lElse);
    genStmList(node->children[1].get());
    emit("j     " + lEnd);
    emitLabel(lElse);
    genStmList(node->children[2].get());
    emitLabel(lEnd);
}

// ── while ────────────────────────────────────────────────────────────────────
void CodeGen::genWhile(ASTNode* node) {
    if (node->children.size() < 2) return;
    std::string lTop = newLabel("while");
    std::string lEnd = newLabel("endwh");
    emitLabel(lTop);
    emitComment("while cond");
    std::string cond = genRelExp(node->children[0].get(), "$t8");
    emit("beqz  " + cond + ", " + lEnd);
    genStmList(node->children[1].get());
    emit("j     " + lTop);
    emitLabel(lEnd);
}

// ── read ─────────────────────────────────────────────────────────────────────
void CodeGen::genRead(ASTNode* node) {
    emitComment("read " + node->name);
    emit("li    $v0, 5");   // syscall 5 = read integer
    emit("syscall");
    // 把 $v0 存入变量（构造一个假的 SIMPLE_VAR 节点不现实，直接查符号表）
    Symbol* sym = current_->lookup(node->name);
    if (sym) {
        std::string addr = std::to_string(sym->offset) + "($fp)";
        emit("sw    $v0, " + addr);
    } else {
        emit("# read: unknown var " + node->name);
    }
}

// ── write ────────────────────────────────────────────────────────────────────
void CodeGen::genWrite(ASTNode* node) {
    emitComment("write");
    if (node->children.empty()) return;
    std::string val = genExp(node->children[0].get(), "$a0");
    if (val != "$a0") emit("move  $a0, " + val);
    emit("li    $v0, 1");   // syscall 1 = print integer
    emit("syscall");
    emit("la    $a0, __newline");
    emit("li    $v0, 4");   // syscall 4 = print string
    emit("syscall");
}

// ── return ───────────────────────────────────────────────────────────────────
void CodeGen::genReturn(ASTNode* node) {
    emitComment("return");
    if (!node->children.empty()) {
        std::string val = genExp(node->children[0].get(), "$v0");
        if (val != "$v0") emit("move  $v0, " + val);
    }
    emit("j     __ret_" + currentProcLabel_);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 工具：把寄存器 push/pop 到栈（用于表达式中间值保存）
// ═══════════════════════════════════════════════════════════════════════════════
void CodeGen::pushReg(const std::string& r) {
    emit("addiu $sp, $sp, -4");
    emit("sw    " + r + ", 0($sp)");
}
void CodeGen::popReg(const std::string& r) {
    emit("lw    " + r + ", 0($sp)");
    emit("addiu $sp, $sp, 4");
}

// ═══════════════════════════════════════════════════════════════════════════════
// 表达式
// ═══════════════════════════════════════════════════════════════════════════════
//
// 寄存器约定：
//   dest ($t0-$t7) ：当前节点结果
//   $t8             ：数组/字段寻址专用 addrReg（不用于表达式中间值）
//   $t9             ：数组/字段寻址专用 index（不用于表达式中间值）
//   栈              ：BINARY_EXP 左子树结果暂存，避免右子树计算覆盖
//
std::string CodeGen::genExp(ASTNode* node, const std::string& dest) {
    if (!node) return "$zero";
    std::string d = dest.empty() ? allocTemp() : dest;

    switch (node->kind) {
        // ── 整型常量 ──────────────────────────────────────────────────────
        case NodeKind::INT_LITERAL:
            emit("li    " + d + ", " + std::to_string(node->ival));
            return d;

        // ── 二元运算（含比较）────────────────────────────────────────────
        case NodeKind::BINARY_EXP: {
            if (node->children.size() < 2) { emit("li    " + d + ", 0"); return d; }
            // 策略：左子树 → 压栈，右子树 → $t9，弹栈 → $t8，运算 $t8 op $t9 → d
            // 这样三个寄存器各司其职，不互相覆盖
            genExp(node->children[0].get(), d); // 左值先算到 d（复用 d 减少 move）
            pushReg(d);                          // 左值压栈
            genExp(node->children[1].get(), "$t9"); // 右值 → $t9（固定，不用 d）
            popReg("$t8");                       // 左值弹到 $t8
            // 现在：$t8 = 左值，$t9 = 右值，d = 结果目标
            const std::string& op = node->name;
            if      (op == "+") emit("add   " + d + ", $t8, $t9");
            else if (op == "-") emit("sub   " + d + ", $t8, $t9");
            else if (op == "*") emit("mul   " + d + ", $t8, $t9");
            else if (op == "/") { emit("div   $t8, $t9"); emit("mflo  " + d); }
            else if (op == "<") emit("slt   " + d + ", $t8, $t9");
            else if (op == "=") {
                emit("xor   " + d + ", $t8, $t9");
                emit("sltiu " + d + ", " + d + ", 1");
            }
            return d;
        }

        // ── 变量表达式包装节点 ────────────────────────────────────────────
        case NodeKind::VAR_EXP:
            if (!node->children.empty())
                return genExp(node->children[0].get(), d);
            return d;

        // ── 简单变量 ──────────────────────────────────────────────────────
        case NodeKind::SIMPLE_VAR: {
            Symbol* sym = current_->lookup(node->name);
            if (!sym) { emit("li    " + d + ", 0  # undef:" + node->name); return d; }
            if (sym->isVarParam) {
                // VAR 参数传指针：先 lw 指针到 $t8，再 lw 值
                emit("lw    $t8, " + std::to_string(sym->offset) + "($fp)");
                emit("lw    " + d + ", 0($t8)");
            } else {
                emit("lw    " + d + ", " + std::to_string(sym->offset) + "($fp)");
            }
            return d;
        }

        // ── 数组访问 ──────────────────────────────────────────────────────
        case NodeKind::INDEX_VAR: {
            genIndexAddr(node);   // 地址 → $t8
            emit("lw    " + d + ", 0($t8)");
            return d;
        }

        // ── record 字段访问 ───────────────────────────────────────────────
        case NodeKind::FIELD_VAR: {
            genFieldAddr(node);   // 地址 → $t8
            emit("lw    " + d + ", 0($t8)");
            return d;
        }

        default:
            emit("li    " + d + ", 0  # unknown exp");
            return d;
    }
}

// RelExp：生成比较结果（0/1）→ dest（RelExp 已统一到 BINARY_EXP 处理）
std::string CodeGen::genRelExp(ASTNode* node, const std::string& dest) {
    if (!node) return "$zero";
    std::string d = dest.empty() ? allocTemp() : dest;
    return genExp(node, d);  // BINARY_EXP 分支已处理 < 和 =
}

// ═══════════════════════════════════════════════════════════════════════════════
// 变量地址计算
// ═══════════════════════════════════════════════════════════════════════════════

// genVarAddr：简单变量 → "offset($fp)"，复杂变量 → 地址入 $t8 后返回 "0($t8)"
std::string CodeGen::genVarAddr(ASTNode* node) {
    if (!node) return "0($fp)";
    if (node->kind == NodeKind::SIMPLE_VAR) {
        Symbol* sym = current_->lookup(node->name);
        if (!sym) return "0($fp)  # undef:" + node->name;
        if (sym->isVarParam) {
            emit("lw    $t8, " + std::to_string(sym->offset) + "($fp)");
            return "0($t8)";
        }
        return std::to_string(sym->offset) + "($fp)";
    }
    if (node->kind == NodeKind::INDEX_VAR) { genIndexAddr(node); return "0($t8)"; }
    if (node->kind == NodeKind::FIELD_VAR) { genFieldAddr(node); return "0($t8)"; }
    return "0($fp)";
}

// genStore：把 val 写回 LHS 变量（处理 VAR 参数、数组、字段）
void CodeGen::genStore(ASTNode* lhs, const std::string& val) {
    if (!lhs) return;
    // VAR_EXP 包装层
    ASTNode* inner = (lhs->kind == NodeKind::VAR_EXP && !lhs->children.empty())
                     ? lhs->children[0].get() : lhs;
    if (inner->kind == NodeKind::SIMPLE_VAR) {
        Symbol* sym = current_->lookup(inner->name);
        if (!sym) { emit("# store: undef " + inner->name); return; }
        if (sym->isVarParam) {
            // val 可能在 $t8，先保存
            if (val == "$t8") {
                emit("addiu $sp, $sp, -4");
                emit("sw    $t8, 0($sp)");
                emit("lw    $t8, " + std::to_string(sym->offset) + "($fp)");
                emit("lw    $t9, 0($sp)");
                emit("addiu $sp, $sp, 4");
                emit("sw    $t9, 0($t8)");
            } else {
                emit("lw    $t8, " + std::to_string(sym->offset) + "($fp)");
                emit("sw    " + val + ", 0($t8)");
            }
        } else {
            emit("sw    " + val + ", " + std::to_string(sym->offset) + "($fp)");
        }
    } else if (inner->kind == NodeKind::INDEX_VAR) {
        // val 在 $t8 时需要先压栈
        bool valIsT8 = (val == "$t8");
        if (valIsT8) pushReg("$t8");
        genIndexAddr(inner);   // → $t8
        if (valIsT8) {
            popReg("$t9");
            emit("sw    $t9, 0($t8)");
        } else {
            emit("sw    " + val + ", 0($t8)");
        }
    } else if (inner->kind == NodeKind::FIELD_VAR) {
        bool valIsT8 = (val == "$t8");
        if (valIsT8) pushReg("$t8");
        genFieldAddr(inner);
        if (valIsT8) {
            popReg("$t9");
            emit("sw    $t9, 0($t8)");
        } else {
            emit("sw    " + val + ", 0($t8)");
        }
    }
}

// genLoadAddr：取变量地址到 dest（VAR 参数传递用）
void CodeGen::genLoadAddr(ASTNode* node, const std::string& dest) {
    if (!node) return;
    ASTNode* inner = (node->kind == NodeKind::VAR_EXP && !node->children.empty())
                     ? node->children[0].get() : node;
    if (inner->kind == NodeKind::SIMPLE_VAR) {
        Symbol* sym = current_->lookup(inner->name);
        if (!sym) return;
        if (sym->isVarParam) {
            emit("lw    " + dest + ", " + std::to_string(sym->offset) + "($fp)");
        } else {
            emit("addiu " + dest + ", $fp, " + std::to_string(sym->offset));
        }
    } else if (inner->kind == NodeKind::INDEX_VAR) {
        genIndexAddr(inner);
        if (dest != "$t8") emit("move  " + dest + ", $t8");
    } else if (inner->kind == NodeKind::FIELD_VAR) {
        genFieldAddr(inner);
        if (dest != "$t8") emit("move  " + dest + ", $t8");
    }
}

// ── 数组寻址 → $t8 ────────────────────────────────────────────────────────────
//  公式：$t8 = &a[0] + (index - low) * 4
//  $t9 用作临时 index 计算
void CodeGen::genIndexAddr(ASTNode* node) {
    if (node->children.size() < 2) return;
    ASTNode* base  = node->children[0].get();
    ASTNode* index = node->children[1].get();

    // 1. 求 index → push 到栈（因为后面求 base 地址会用 $t8/$t9）
    genExp(index, "$t9");
    pushReg("$t9");          // 栈：[index]

    // 2. 取 base 地址 → $t8
    {
        ASTNode* b = (base->kind == NodeKind::VAR_EXP && !base->children.empty())
                     ? base->children[0].get() : base;
        Symbol* sym = current_->lookup(b->name);
        if (!sym) { emit("li    $t8, 0  # undef arr"); popReg("$t9"); return; }
        if (sym->isVarParam) {
            emit("lw    $t8, " + std::to_string(sym->offset) + "($fp)");
        } else {
            emit("addiu $t8, $fp, " + std::to_string(sym->offset));
        }

        // 3. 弹回 index → $t9
        popReg("$t9");

        // 4. index - low
        if (sym->type && sym->type->kind == TypeKind::ARRAY && sym->type->low != 0)
            emit("addiu $t9, $t9, -" + std::to_string(sym->type->low));

        // 5. byte offset = index * 4
        emit("sll   $t9, $t9, 2");

        // 6. $t8 = base + offset
        emit("add   $t8, $t8, $t9");
    }
}

// ── record 字段寻址 → $t8 ────────────────────────────────────────────────────
void CodeGen::genFieldAddr(ASTNode* node) {
    if (node->children.empty()) return;
    ASTNode* base = node->children[0].get();
    ASTNode* b    = (base->kind == NodeKind::VAR_EXP && !base->children.empty())
                    ? base->children[0].get() : base;
    Symbol* sym = current_->lookup(b->name);
    if (!sym) { emit("li    $t8, 0  # undef rec"); return; }

    // 字段在 record 中的字节偏移
    int fieldOff = 0;
    if (sym->type && sym->type->kind == TypeKind::RECORD) {
        for (auto& f : sym->type->fields) {
            if (f.name == node->name) break;
            fieldOff += 4; // 每字段 4 字节（word 对齐）
        }
    }

    int totalOff = sym->offset + fieldOff;
    if (sym->isVarParam) {
        emit("lw    $t8, " + std::to_string(sym->offset) + "($fp)");
        emit("addiu $t8, $t8, " + std::to_string(fieldOff));
    } else {
        emit("addiu $t8, $fp, " + std::to_string(totalOff));
    }

    // FieldVarMore：字段本身是数组，加下标偏移
    if (node->children.size() > 1) {
        genExp(node->children[1].get(), "$t9");
        emit("sll   $t9, $t9, 2");
        emit("add   $t8, $t8, $t9");
    }
}

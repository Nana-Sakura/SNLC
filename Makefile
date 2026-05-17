CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined
INCLUDES := -Iinclude

TARGET := snlc

SRCS := $(shell find src -name "*.cpp")

OBJS := $(SRCS:.cpp=.o)

SIM := python3 tools/mips_sim.py

.PHONY: all clean test release format compiletest outputtest

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ── 回归测试 ──────────────────────────────────────────────────────────────────
PASS=0
FAIL=0

define run_test
	@printf "  %-28s" "$(1):"
	@./$(TARGET) $(2) -o /tmp/snl_test.asm 2>/dev/null && \
	  result=$$($(SIM) /tmp/snl_test.asm $(4) 2>/dev/null | tr '\n' ' ' | sed 's/ $$//') && \
	  if [ "$$result" = "$(3)" ]; then \
	    echo "PASS  (got: $$result)"; \
	  else \
	    echo "FAIL  (expected: $(3), got: $$result)"; \
	  fi
endef

test: $(TARGET)
	@echo "============================================"
	@echo "  SNL 编译器回归测试"
	@echo "============================================"
	$(call run_test,simple(10+20),tests/simple.snl,30,)
	$(call run_test,array(sum 1..5 *10),tests/array_test.snl,150,)
	$(call run_test,record(3²+4²),tests/record_test.snl,25,)
	$(call run_test,proc(swap+fib10),tests/proc_test.snl,7 3 55,)
	$(call run_test,stack_balance(5+1),tests/stack_balance.snl,15 20,)
	$(call run_test,right_assoc(18 12),tests/right_assoc.snl,18 -3,)
	$(call run_test,record_array_alias(7 12 19),tests/record_array_alias.snl,7 12 19,)
	$(call run_test,stack_varparam5(2 3 4 5 1),tests/stack_varparam5.snl,2 3 4 5 1,)
	$(call run_test,fibsum(n=8),tests/fibsum.snl,33,8)
	$(call run_test,fibsum(n=10),tests/fibsum.snl,88,10)
	@echo "============================================"
	@echo "  错误检测测试："
	@printf "  %-28s" "undefined variable:"
	@./$(TARGET) tests/error_test.snl -o /dev/null 2>&1 | grep -q "未定义的变量" && echo "PASS" || echo "FAIL"
	@echo "============================================"
	@echo "  图着色寄存器分配路径（--opt-regalloc）："
	@echo "============================================"
	$(call run_test,[RA]simple,--opt-regalloc tests/simple.snl,30,)
	$(call run_test,[RA]array,--opt-regalloc tests/array_test.snl,150,)
	$(call run_test,[RA]record,--opt-regalloc tests/record_test.snl,25,)
	$(call run_test,[RA]proc,--opt-regalloc tests/proc_test.snl,7 3 55,)
	$(call run_test,[RA]stack_balance,--opt-regalloc tests/stack_balance.snl,15 20,)
	$(call run_test,[RA]right_assoc,--opt-regalloc tests/right_assoc.snl,18 -3,)
	$(call run_test,[RA]record_array_alias,--opt-regalloc tests/record_array_alias.snl,7 12 19,)
	$(call run_test,[RA]stack_varparam5,--opt-regalloc tests/stack_varparam5.snl,2 3 4 5 1,)
	$(call run_test,[RA]fibsum(n=8),--opt-regalloc tests/fibsum.snl,33,8)
	@echo "============================================"
	@echo "  LICM 循环不变式外提测试："
	@echo "============================================"
	$(call run_test,licm(10 10 10 0),tests/licm_test.snl,10 10 10 0,)
	@echo "============================================"

# 词法分析单独验证
lex-test: $(TARGET)
	@echo "=== 词法分析 ==="
	./$(TARGET) --lex-only tests/simple.snl

# AST 打印
ast-test: $(TARGET)
	@echo "=== AST ==="
	./$(TARGET) --parse-only tests/simple.snl

# 发布版（无 sanitizer，O2 优化）
release: CXXFLAGS = -std=c++17 -O2 -Wall
release: clean $(TARGET)
	@echo "Release build: $(TARGET)"

format:
	@echo "Formatting source files with clang-format..."
	@./autoformat.sh

compiletest:
	./CompileTest.sh

outputtest:
	./OutputTest.sh

clean:
	rm -f $(OBJS) $(TARGET) tests/*.asm /tmp/snl_test.asm

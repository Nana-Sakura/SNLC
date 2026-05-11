CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -fsanitize=address,undefined
INCLUDES := -Iinclude

TARGET   := snlc
SRCDIR   := src

SRCS := $(SRCDIR)/main.cpp \
        $(SRCDIR)/lexer/token.cpp \
        $(SRCDIR)/lexer/lexer.cpp \
        $(SRCDIR)/parser/ast.cpp \
        $(SRCDIR)/parser/parser.cpp \
        $(SRCDIR)/semantic/semantic.cpp \
        $(SRCDIR)/codegen/codegen.cpp

OBJS := $(SRCS:.cpp=.o)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# 运行测试
test: $(TARGET)
	@echo "=== 词法测试 ==="
	./$(TARGET) --lex-only tests/hello.snl
	@echo ""
	@echo "=== 语法测试（打印 AST）==="
	./$(TARGET) --parse-only tests/hello.snl
	@echo ""
	@echo "=== 完整编译 ==="
	./$(TARGET) -v tests/hello.snl -o tests/hello.asm

clean:
	rm -f $(OBJS) $(TARGET) tests/*.asm

# 发布版（去掉 sanitizer，开启优化）
release: CXXFLAGS = -std=c++17 -O2 -Wall
release: clean $(TARGET)

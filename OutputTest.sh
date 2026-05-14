#!/usr/bin/env zsh
set -uo pipefail

# ===== 配置 =====
MARS_JAR="./tools/Mars.jar"
SIM_PY="./tools/mips_sim.py"
TMP_DIR="./tmp"
TEST_DIR="./tests"

# ===== 检查 =====
if [ ! -f "$MARS_JAR" ]; then
    echo "ERROR: Cannot find $MARS_JAR"
    exit 1
fi

if [ ! -d "$TMP_DIR" ]; then
    echo "ERROR: Cannot find $TMP_DIR"
    exit 1
fi

if [ ! -f "$SIM_PY" ]; then
    echo "ERROR: Cannot find $SIM_PY"
    exit 1
fi

mkdir -p "$TMP_DIR"

# ===== 统计 =====
TOTAL=0
PASS=0
FAIL=0

# ===== 遍历所有 asm =====
for asm in "$TMP_DIR"/*.asm; do
    # 若没有任何 asm 文件，glob 会保持原样
    [ -e "$asm" ] || break

    name=$(basename "$asm" .asm)

    input="$TEST_DIR/$name.in"
    expected="$TEST_DIR/$name.out"
    actual="$TMP_DIR/$name.actual"
    actual_sim="$TMP_DIR/$name.sim.actual"
    log="$TMP_DIR/$name.log"
    sim_log="$TMP_DIR/$name.sim.log"

    TOTAL=$((TOTAL + 1))

    echo "========================================"
    echo "Testing: $name"
    echo "ASM: $asm"

    # ===== 运行 MARS =====
    if [ -f "$input" ]; then
        java -jar "$MARS_JAR" nc "$asm" < "$input" > "$actual" 2> "$log"
    else
        java -jar "$MARS_JAR" nc "$asm" > "$actual" 2> "$log"
    fi

    ret_code=$?

    # ===== 检查运行是否成功 =====
    if [ $ret_code -ne 0 ]; then
        echo "❌ RUNTIME ERROR (exit code $ret_code)"
        echo "See: $log"
        FAIL=$((FAIL + 1))
        continue
    fi

    # ===== 运行 Python 模拟器（difftest） =====
    if [ -f "$input" ]; then
        # 模拟器只接受数字参数，按空白切分传入
        vals=($(<"$input"))
        python3 "$SIM_PY" "$asm" "${vals[@]}" > "$actual_sim" 2> "$sim_log"
    else
        python3 "$SIM_PY" "$asm" > "$actual_sim" 2> "$sim_log"
    fi

    sim_code=$?
    if [ $sim_code -ne 0 ]; then
        echo "❌ SIM ERROR (exit code $sim_code)"
        echo "See: $sim_log"
        FAIL=$((FAIL + 1))
        continue
    fi

    if ! diff -u "$actual" "$actual_sim" > "$TMP_DIR/$name.sim.diff"; then
        echo "❌ DIFFTEST MISMATCH (MARS vs SIM)"
        echo "Diff saved to: $TMP_DIR/$name.sim.diff"
        FAIL=$((FAIL + 1))
        continue
    else
        rm -f "$TMP_DIR/$name.sim.diff"
    fi

    # ===== 若存在标准输出，则比较 =====
    if [ -f "$expected" ]; then
        if diff -u "$expected" "$actual" > "$TMP_DIR/$name.diff"; then
            echo "✅ PASS"
            PASS=$((PASS + 1))
            rm -f "$TMP_DIR/$name.diff"
        else
            echo "❌ WRONG ANSWER"
            echo "Diff saved to: $TMP_DIR/$name.diff"
            FAIL=$((FAIL + 1))
        fi
    else
        echo "⚠️  No expected output: $expected"
        echo "Output saved to: $actual"
        PASS=$((PASS + 1))
    fi
done

echo "========================================"
echo "Summary:"
echo "  Total : $TOTAL"
echo "  Pass  : $PASS"
echo "  Fail  : $FAIL"

if [ $FAIL -eq 0 ]; then
    echo "🎉 All tests passed!"
    exit 0
else
    echo "💥 Some tests failed."
    exit 1
fi
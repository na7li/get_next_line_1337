#!/usr/bin/env bash
# Full get_next_line check: norminette + every BUFFER_SIZE + bonus + valgrind.
#
#   tests/run.sh                 test the project this script lives in
#   tests/run.sh /path/to/repo   test someone else's repo (peer evaluation)
#
# Nothing is written inside the tested repo: binaries go to a temp folder.

set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "${1:-$HERE/..}" && pwd)"
MAIN="$HERE/main.c"
BIN="$(mktemp -d /tmp/gnl_bin_XXXXXX)"
trap 'rm -rf "$BIN"' EXIT

FLAGS="-Wall -Wextra -Werror"
SIZES="1 2 3 4 5 7 8 10 16 32 42 64 100 1000 1024 4096 9999 10000 1000000 10000000"
BONUS_SIZES="1 2 5 42 1000 10000000"
fails=0

bold() { printf '\n\033[1m%s\033[0m\n' "$*"; }
red()  { printf '\033[31m%s\033[0m\n' "$*"; }

M_SRC=("$SRC/get_next_line.c" "$SRC/get_next_line_utils.c")
B_SRC=("$SRC/get_next_line_bonus.c" "$SRC/get_next_line_utils_bonus.c")

bold "Project: $SRC"
for f in get_next_line.c get_next_line_utils.c get_next_line.h \
         get_next_line_bonus.c get_next_line_utils_bonus.c get_next_line_bonus.h README.md; do
  if [ -f "$SRC/$f" ]; then echo "  found    $f"; else echo "  MISSING  $f"; fi
done

# Compile each part to objects and ask nm what the code really uses:
#  - external functions that are not defined in the student's own files
#  - global variables (static ones are fine: nm -g does not list them)
check_symbols() { # label, files...
  local label=$1; shift
  local objs=() f o
  for f in "$@"; do
    [ -f "$f" ] || return 0
    o="$BIN/$(basename "$f" .c).o"
    cc -c -D BUFFER_SIZE=42 "$f" -o "$o" 2>/dev/null || { red "  $label: $(basename "$f") does not compile"; fails=$((fails+1)); return; }
    objs+=("$o")
  done
  local strip='s/^//'; [ "$(uname)" = Darwin ] && strip='s/^_//'
  local defined used extra globals
  defined=$(nm -g "${objs[@]}" | awk 'NF==3 && $2!="U" {print $3}' | sed "$strip" | sort -u)
  used=$(nm -u "${objs[@]}" | awk 'NF && $NF !~ /:$/ {print $NF}' | sed "$strip" | sort -u)
  extra=$(comm -23 <(echo "$used") <(echo "$defined") | grep -vxE 'read|malloc|free|__stack_chk_fail|__stack_chk_guard|_GLOBAL_OFFSET_TABLE_|' )
  globals=$(nm -g "${objs[@]}" | awk 'NF==3 && $2 ~ /^[BDCSGV]$/ {print $3}' | sed "$strip")
  if [ -n "$extra" ]; then
    red "  $label: uses functions other than read/malloc/free: $(echo $extra)"; fails=$((fails+1))
  else
    echo "  $label: only read, malloc, free"
  fi
  if [ -n "$globals" ]; then
    red "  $label: GLOBAL variable(s): $(echo $globals)  (forbidden)"; fails=$((fails+1))
  else
    echo "  $label: no global variables"
  fi
}

bold "Allowed functions and global variables (checked with nm)"
check_symbols "mandatory" "${M_SRC[@]}"
[ -f "${B_SRC[0]}" ] && check_symbols "bonus" "${B_SRC[@]}"

if command -v norminette >/dev/null; then
  bold "norminette"
  norminette "$SRC"/get_next_line*.[ch] 2>&1 | grep -v '^Setting locale' | sed 's/^/  /'
  norminette "$SRC"/get_next_line*.[ch] 2>&1 | grep -q 'Error' && fails=$((fails+1))
fi

run() { # name, args..., then -- binary args
  "$@" || fails=$((fails+1))
}

bold "Mandatory - one run per BUFFER_SIZE"
for bs in $SIZES; do
  if cc $FLAGS -D BUFFER_SIZE=$bs "${M_SRC[@]}" "$MAIN" -o "$BIN/m_$bs" 2>"$BIN/err"; then
    run "$BIN/m_$bs" -q
  else
    red "  BUFFER_SIZE=$bs: compilation failed"; cat "$BIN/err"; fails=$((fails+1))
  fi
done

bold "Mandatory - compiled WITHOUT -D BUFFER_SIZE (must still compile and work)"
if cc $FLAGS "${M_SRC[@]}" "$MAIN" -o "$BIN/m_nodef" 2>"$BIN/err"; then
  run "$BIN/m_nodef" -q
else
  red "  compilation without -D BUFFER_SIZE failed"; cat "$BIN/err"; fails=$((fails+1))
fi

bold "Mandatory - BUFFER_SIZE=0 and -5 (must return NULL, not crash)"
for bs in 0 -5; do
  if cc $FLAGS -D BUFFER_SIZE=$bs "${M_SRC[@]}" "$MAIN" -o "$BIN/m_z" 2>"$BIN/err"; then
    run "$BIN/m_z" -q
  else
    red "  BUFFER_SIZE=$bs: compilation failed"; cat "$BIN/err"; fails=$((fails+1))
  fi
done

if [ -f "${B_SRC[0]}" ]; then
  bold "Bonus - multiple fds"
  for bs in $BONUS_SIZES; do
    if cc $FLAGS -D BUFFER_SIZE=$bs "${B_SRC[@]}" "$MAIN" -o "$BIN/b_$bs" 2>"$BIN/err"; then
      run "$BIN/b_$bs" -q bonus
    else
      red "  bonus BUFFER_SIZE=$bs: compilation failed"; cat "$BIN/err"; fails=$((fails+1))
    fi
  done
fi

# Build the malloc-failure binary: the student's files are compiled with
# -include fail_malloc.h so their malloc() can be made to fail on demand.
build_malloc() { # part, bs -> $BIN/mf, sets $arg
  local f o objs=()
  if [ "$1" = mandatory ]; then srcs=("${M_SRC[@]}"); arg=""; else
    [ -f "${B_SRC[0]}" ] || return 1; srcs=("${B_SRC[@]}"); arg="bonus"; fi
  for f in "${srcs[@]}"; do
    o="$BIN/mf_$(basename "$f" .c).o"
    cc $FLAGS -g -D BUFFER_SIZE=$2 -include "$HERE/fail_malloc.h" -c "$f" -o "$o" 2>"$BIN/err" || { cat "$BIN/err"; return 1; }
    objs+=("$o")
  done
  cc $FLAGS -g -D BUFFER_SIZE=$2 -D GNL_FAIL_MALLOC "$MAIN" "${objs[@]}" -o "$BIN/mf" 2>"$BIN/err" || { cat "$BIN/err"; return 1; }
}

bold "malloc failure - each malloc of a full read returns NULL once, in turn"
for part in mandatory bonus; do
  [ $part = bonus ] && [ ! -f "${B_SRC[0]}" ] && continue
  for bs in 1 42 10000000; do
    if build_malloc $part $bs; then
      printf '  %-9s ' "$part"; run "$BIN/mf" -q malloc $arg
    else
      red "  $part BUFFER_SIZE=$bs: malloc-failure build failed"; fails=$((fails+1))
    fi
  done
done

# AddressSanitizer: buffer overflows, use-after-free, double free...
# at many BUFFER_SIZEs (valgrind below only runs 1 and 42).
if echo 'int main(void){return 0;}' | cc -fsanitize=address -x c - -o "$BIN/probe" 2>/dev/null; then
  bold "AddressSanitizer - overflows, use-after-free, double free (many sizes)"
  for part in mandatory bonus; do
    if [ $part = mandatory ]; then srcs=("${M_SRC[@]}"); arg=""; else
      [ -f "${B_SRC[0]}" ] || continue; srcs=("${B_SRC[@]}"); arg="bonus"; fi
    for bs in 1 2 3 7 42 4096 10000000; do
      if cc $FLAGS -g -fsanitize=address -fno-omit-frame-pointer -D BUFFER_SIZE=$bs "${srcs[@]}" "$MAIN" -o "$BIN/asan" 2>"$BIN/err"; then
        # ASan makes every byte copy slower: BUFFER_SIZE=1 on 20000-char lines
        # needs more than the default 10 s, so ASan runs get 120 s per test.
        printf '  %-9s ' "$part"; ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 GNL_TIMEOUT=120 run "$BIN/asan" -q $arg
      else
        red "  $part BUFFER_SIZE=$bs: ASan build failed"; cat "$BIN/err"; fails=$((fails+1))
      fi
    done
  done
else
  bold "AddressSanitizer skipped: this compiler has no -fsanitize=address"
fi

# Memory check. Only real leaks count ("definitely/indirectly lost" with
# valgrind, the same thing macOS `leaks` reports). A static stash that still
# holds data when the program ends is "still reachable", not a leak.
# KO lines from this run are ignored: the runs above already report them.
mem_part() { # part, bs  -> builds $BIN/vg, sets $arg
  if [ "$1" = mandatory ]; then srcs=("${M_SRC[@]}"); arg=""; else
    [ -f "${B_SRC[0]}" ] || return 1; srcs=("${B_SRC[@]}"); arg="bonus"; fi
  cc $FLAGS -g -D BUFFER_SIZE=$2 "${srcs[@]}" "$MAIN" -o "$BIN/vg" 2>/dev/null
}

if command -v valgrind >/dev/null; then
  bold "valgrind (leaks + invalid reads/writes)"
  VG="valgrind -q --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect --child-silent-after-fork=yes"
  vg_check() { # label, binary, args...
    local label=$1 bin=$2; shift 2
    $VG "$bin" -q --no-fork "$@" >"$BIN/vg.out" 2>&1
    if grep -qE '^==[0-9]+== ' "$BIN/vg.out"; then
      red "  $label: valgrind reported problems:"
      grep -E '^==[0-9]+== ' "$BIN/vg.out" | sed 's/^/    /' | head -40
      fails=$((fails+1))
    else
      echo "  $label: no leaks, no memory errors"
    fi
  }
  for bs in 1 42; do
    for part in mandatory bonus; do
      mem_part $part $bs || continue
      vg_check "$part BUFFER_SIZE=$bs" "$BIN/vg" $arg
      build_malloc $part $bs && vg_check "$part BUFFER_SIZE=$bs, malloc failures" "$BIN/mf" malloc $arg
    done
  done
elif command -v leaks >/dev/null; then
  bold "leaks (macOS)"
  lk_check() { # label, binary, args...
    local label=$1 bin=$2; shift 2
    leaks -atExit -- "$bin" -q --no-fork "$@" >"$BIN/vg.out" 2>&1
    if grep -q ' 0 leaks for 0 total leaked bytes' "$BIN/vg.out"; then
      echo "  $label: no leaks"
    else
      red "  $label: leaks reported problems:"
      grep -E 'leaks for|Leak:|ROOT LEAK' "$BIN/vg.out" | sed 's/^/    /' | head -30
      fails=$((fails+1))
    fi
  }
  for bs in 1 42; do
    for part in mandatory bonus; do
      mem_part $part $bs || continue
      lk_check "$part BUFFER_SIZE=$bs" "$BIN/vg" $arg
      build_malloc $part $bs && lk_check "$part BUFFER_SIZE=$bs, malloc failures" "$BIN/mf" malloc $arg
    done
  done
else
  bold "Memory check skipped: neither valgrind nor leaks is installed"
fi

echo
if [ $fails -eq 0 ]; then
  printf '\033[32;1mALL GOOD\033[0m - every run passed\n'
else
  printf '\033[31;1m%d run(s) with problems\033[0m - scroll up\n' "$fails"
fi
exit $((fails > 0))

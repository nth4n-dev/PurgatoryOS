#!/usr/bin/env bash
#
# Verify the fixed/post-* branches before merging them into post-*.
#
# Builds each branch in a throwaway worktree, boots it under QEMU, and checks
# the boot output against what the corresponding article claims. Also asserts
# the structural properties of the reconstruction: the MMU fix is present on
# 6-9, chapter 8 has no scheduler, and 10/11 are byte-identical to today.
#
# Your working tree and current branch are never touched.
#
# Usage: ./verify-fixed.sh

set -uo pipefail

CROSS="${CROSS:-aarch64-elf-}"
BOOT_SECONDS="${BOOT_SECONDS:-5}"

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WT="$(mktemp -d)/wt"
LOGS="$(mktemp -d)"

pass=0
fail=0

green() { printf '\033[32m%s\033[0m' "$1"; }
red()   { printf '\033[31m%s\033[0m' "$1"; }
dim()   { printf '\033[2m%s\033[0m' "$1"; }

check() {
    local label=$1 result=$2
    if [ "$result" = "0" ]; then
        printf '    %s %s\n' "$(green ✓)" "$label"
        pass=$((pass + 1))
    else
        printf '    %s %s\n' "$(red ✗)" "$label"
        fail=$((fail + 1))
    fi
}

cleanup() {
    git -C "$REPO" worktree remove --force "$WT" >/dev/null 2>&1
    git -C "$REPO" worktree prune >/dev/null 2>&1
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Preflight
# ---------------------------------------------------------------------------

command -v "${CROSS}gcc" >/dev/null || {
    echo "$(red "✗ ${CROSS}gcc not found.") Set CROSS= to your toolchain prefix."
    exit 1
}
have_qemu=0
command -v qemu-system-aarch64 >/dev/null && have_qemu=1 || \
    echo "$(dim '! qemu-system-aarch64 not found — boot checks will be skipped')"
have_cargo=0
command -v cargo >/dev/null && have_cargo=1

for n in 6 7 8 9 10 11; do
    git -C "$REPO" rev-parse --verify -q "fixed/post-$n" >/dev/null || {
        echo "$(red "✗ branch fixed/post-$n is missing")"
        exit 1
    }
done

git -C "$REPO" worktree add --detach "$WT" HEAD >/dev/null 2>&1 || {
    echo "$(red '✗ could not create a worktree') — clear any stale .git/*.lock files first"
    exit 1
}

# ---------------------------------------------------------------------------
# Per-branch checks
# ---------------------------------------------------------------------------

boot_log() {
    # $1 = elf path, $2 = log path. Kills QEMU after BOOT_SECONDS.
    qemu-system-aarch64 -M virt -cpu cortex-a53 -nographic -kernel "$1" \
        >"$2" 2>&1 </dev/null &
    local qpid=$!
    sleep "$BOOT_SECONDS"
    kill "$qpid" 2>/dev/null
    wait "$qpid" 2>/dev/null
}

for n in 6 7 8 9 10 11; do
    branch="fixed/post-$n"
    echo
    echo "$branch  $(dim "$(git -C "$REPO" rev-parse --short "$branch")")"

    git -C "$WT" checkout -q --detach "$branch" 2>/dev/null
    git -C "$WT" clean -qfdx

    # --- build ---
    if [ "$n" = "11" ] && [ "$have_cargo" = "0" ]; then
        echo "    $(dim '· build skipped (cargo not installed)')"
    else
        if make -C "$WT" CROSS="$CROSS" >"$LOGS/build-$n.log" 2>&1; then
            check "builds and links" 0
        else
            check "builds and links  → $LOGS/build-$n.log" 1
            continue
        fi
    fi

    # --- boot ---
    if [ "$have_qemu" = "1" ] && [ -f "$WT/kernel.elf" ]; then
        log="$LOGS/boot-$n.log"
        boot_log "$WT/kernel.elf" "$log"

        case "$n" in
          6)  grep -q "Hello, Kernel!"                     "$log"; check "boots: UART works with MMU on" $? ;;
          7)  grep -q "Interrupts enabled"                 "$log"; check "boots: reaches timer wait"     $?
              grep -q "\[timer\] tick"                     "$log"; check "timer interrupt fires"         $? ;;
          8)  grep -q "Heap: ready"                        "$log"; check "boots: heap initialised"       $?
              grep -q "should reuse b slot"                "$log"; check "free-list reuses freed block"  $?
              grep -q "freed all"                          "$log"; check "coalescing pass runs"          $?
              grep -q "Waiting for timer"                  "$log"; check "ends in the chapter-8 wfi loop" $?
              ! grep -qi "task A"                          "$log"; check "no scheduler output (ch.9 leak)" $? ;;
          9)  grep -q "Starting scheduler"                 "$log"; check "boots: scheduler starts"       $?
              grep -q "\[task A\]"                         "$log"; check "task A runs"                   $?
              grep -q "\[task B\]"                         "$log"; check "task B runs (context switch)"  $? ;;
          10) grep -q "Syscalls: ready"                    "$log"; check "boots: syscalls registered"    $?
              grep -q "\[task"                             "$log"; check "tasks run"                     $? ;;
          11) grep -q "Syscalls: ready"                    "$log"; check "boots: syscalls registered"    $? ;;
        esac
    fi

    # --- MMU fix present on the branches that needed it ---
    if [ "$n" -le 9 ]; then
        grep -q "l2_dev_table" "$WT/src/kernel/mmu.c"; check "mmu: peripheral L2 table exists" $?
        grep -q "BLOCK_DEVICE" "$WT/src/kernel/mmu.c"; check "mmu: BLOCK_DEVICE is used"       $?
        ! grep -q "l1_table\[0\] = 0x00000000UL | BLOCK_NORMAL" "$WT/src/kernel/mmu.c"
        check "mmu: old Normal-memory UART mapping is gone" $?
    fi

    # --- chapter 8 must not contain chapter 9 ---
    if [ "$n" = "8" ]; then
        [ ! -e "$WT/src/kernel/scheduler.c" ]     ; check "no scheduler.c"       $?
        [ ! -e "$WT/include/kernel/scheduler.h" ] ; check "no scheduler.h"       $?
        [ ! -e "$WT/src/arch/context_switch.S" ]  ; check "no context_switch.S"  $?
        ! grep -q "scheduler_tick" "$WT/src/kernel/timer.c"
        check "timer.c has no scheduler hook" $?
        if [ -f "$WT/kernel.elf" ]; then
            ! "${CROSS}nm" "$WT/kernel.elf" | grep -qiE "scheduler|context_switch"
            check "linked ELF has no scheduler symbols" $?
        fi
    fi

    # --- 10 and 11 must be content-identical to what is published today ---
    if [ "$n" -ge 10 ]; then
        [ "$(git -C "$REPO" rev-parse "post-$n^{tree}")" = \
          "$(git -C "$REPO" rev-parse "$branch^{tree}")" ]
        check "tree unchanged vs current post-$n" $?
    fi
done

# ---------------------------------------------------------------------------
# Chain shape
# ---------------------------------------------------------------------------

echo
echo "chain"
prev=""
for n in 8 9 10 11; do
    cur="fixed/post-$n"
    if [ -n "$prev" ]; then
        git -C "$REPO" merge-base --is-ancestor "$prev" "$cur"
        check "fixed/post-$n descends from fixed/post-$((n - 1))" $?
    fi
    prev="$cur"
done
git -C "$REPO" log -1 --format=%s fixed/post-9 | grep -q "Post 9"
check "a real 'Post 9' commit now exists" $?

echo
echo "─────────────────────────────────────────"
if [ "$fail" -eq 0 ]; then
    echo "$(green "✓ $pass checks passed")"
    echo
    echo "Safe to adopt:"
    echo "  for n in 6 7 8 9 10 11; do git branch -f post-\$n fixed/post-\$n; done"
    echo "  git push --force-with-lease origin post-6 post-7 post-8 post-9 post-10 post-11"
else
    echo "$(red "✗ $fail failed") / $pass passed   logs: $LOGS"
    exit 1
fi

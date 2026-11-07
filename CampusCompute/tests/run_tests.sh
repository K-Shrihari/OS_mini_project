#!/usr/bin/env bash
# Smoke tests for CampusCompute. Run from the project root:  make test
BIN=./campuscompute
pass=0; fail=0

check() {   # check "description" expected_rc "grep pattern" command...
    local desc="$1" want_rc="$2" pat="$3"; shift 3
    local out rc
    out=$("$@" 2>&1); rc=$?
    if [ "$rc" -eq "$want_rc" ] && echo "$out" | grep -q -- "$pat"; then
        echo "[PASS] $desc"; pass=$((pass+1))
    else
        echo "[FAIL] $desc (rc=$rc, wanted $want_rc, pattern '$pat')"; fail=$((fail+1))
    fi
}

[ -x "$BIN" ] || { echo "Build first: make"; exit 1; }

check "help prints usage"            0 "Usage:"                         $BIN help
check "FCFS average waiting = 8.20"  0 "Average waiting time    : 8.20"  $BIN sched fcfs
check "RR q=3 average waiting 9.60"  0 "Average waiting time    : 9.60"  $BIN sched rr -q 3
check "Priority avg waiting = 6.60"  0 "Average waiting time    : 6.60"  $BIN sched prio
check "Priority tie-break P4 before P5" 0 "P4 (8-14) | P5 (14-16)"       $BIN sched prio
check "Compare table"                0 "Lowest average waiting time"    $BIN sched compare
check "Idle gap handled"             0 "IDLE (4-10)"                    $BIN sched fcfs -f tests/jobs_idle.txt
check "Malformed job file rejected"  1 "malformed field"                $BIN sched fcfs -f tests/jobs_bad.txt
check "Duplicate job ID rejected"    1 "duplicate job ID"               $BIN sched fcfs -f tests/jobs_dup.txt
check "Bad quantum rejected"         2 "time quantum must be"           $BIN sched rr -q 0
check "Unknown command rejected"     2 "unknown command"                $BIN nonsense
check "Logger no events lost"        0 "RESULT: PASS"                   $BIN logger -n 5 -b 2
check "IPC + cleanup"                0 "RESULT: PASS"                   $BIN ipc
check "Banker safe sequence"         0 "Safe sequence: P1 -> P3 -> P4 -> P0 -> P2" $BIN banker safe
check "Banker unsafe detected"       0 "UNSAFE state"                   $BIN banker unsafe
check "Worker normal"                0 "NORMAL COMPLETION"              $BIN worker normal
check "Worker simulated failure"     0 "FAILURE DETECTED"               $BIN worker fail
check "Resource limits"              0 "RESULT: PASS"                   $BIN limit all
check "Integrated demo"              0 "OVERALL: 8/8 checks passed"     $BIN demo

echo; echo "Passed: $pass  Failed: $fail"
[ "$fail" -eq 0 ]

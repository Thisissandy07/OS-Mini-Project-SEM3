#!/bin/sh
BIN=./campuscompute
pass=0
fail=0

check_output() {
    desc=$1
    expected=$2
    shift 2
    out=$("$@" 2>&1)
    if printf '%s' "$out" | grep -qF -- "$expected"; then
        pass=$((pass + 1))
        echo "PASS  $desc"
    else
        fail=$((fail + 1))
        echo "FAIL  $desc (missing: $expected)"
    fi
}

check_exit() {
    desc=$1
    want=$2
    shift 2
    "$@" >/dev/null 2>&1
    rc=$?
    if [ "$rc" -eq "$want" ]; then
        pass=$((pass + 1))
        echo "PASS  $desc"
    else
        fail=$((fail + 1))
        echo "FAIL  $desc (exit $rc, wanted $want)"
    fi
}

if [ ! -x "$BIN" ]; then
    echo "build the project first: make"
    exit 1
fi

check_exit   "help exits 0"                          0 $BIN help
check_exit   "no command is an error"                1 $BIN
check_exit   "unknown command is an error"           1 $BIN bogus

check_output "FCFS average turnaround"               "Average turnaround time: 13.00" $BIN sched fcfs tests/jobs.txt
check_output "FCFS average waiting"                  "Average waiting time   : 8.20"  $BIN sched fcfs tests/jobs.txt
check_output "Round Robin q=2 average turnaround"    "Average turnaround time: 15.40" $BIN sched rr tests/jobs.txt 2
check_output "Round Robin q=2 average waiting"       "Average waiting time   : 10.60" $BIN sched rr tests/jobs.txt 2
check_output "Priority average turnaround"           "Average turnaround time: 10.60" $BIN sched priority tests/jobs.txt
check_output "Priority tie broken by arrival"        "P2[5-8] -> P5[8-10]"            $BIN sched priority tests/jobs.txt
check_output "idle gap appears in timeline"          "idle[2-10]"                     $BIN sched fcfs tests/jobs_idle.txt
check_output "comparison picks lowest waiting"       "Lowest average waiting time   : Priority" $BIN sched compare tests/jobs.txt 2
check_exit   "invalid job file rejected"             1 $BIN sched fcfs tests/jobs_invalid.txt
check_exit   "missing job file rejected"             1 $BIN sched fcfs tests/does_not_exist.txt
check_exit   "bad quantum rejected"                  1 $BIN sched rr tests/jobs.txt 0
check_exit   "unknown policy rejected"               1 $BIN sched lottery

check_output "Banker safe sequence"                  "SAFE STATE. Safe sequence: P1 -> P3 -> P4 -> P0 -> P2" $BIN banker tests/banker_safe.txt
check_output "Banker detects unsafe state"           "UNSAFE STATE. Only 3 of 5" $BIN banker tests/banker_unsafe.txt
check_output "Banker names stuck processes"          "may deadlock: P0 P2"       $BIN banker tests/banker_unsafe.txt
check_exit   "invalid Banker state rejected"         1 $BIN banker tests/banker_invalid.txt

check_output "logger loses no entries"               "entries expected: 12, entries written: 12" $BIN logger 3 4
check_exit   "logger bad arguments rejected"         1 $BIN logger 0
check_output "IPC exchanges all messages in order"   "all 8 messages received in order" $BIN ipc
check_exit   "IPC leaves no objects behind"          0 $BIN ipc cleanup
check_output "worker normal"                         "3 started, 3 completed normally, 0 failed" $BIN worker normal
check_output "worker simulated failure"              "3 started, 1 completed normally, 2 failed" $BIN worker fail
check_exit   "worker bad argument rejected"          1 $BIN worker boom
check_output "open-file limit takes effect"          "Too many open files" $BIN limit
check_output "address-space limit takes effect"      "Cannot allocate memory" $BIN limit

check_output "integrated demo passes all stages"     "8 of 8 stages passed" $BIN demo

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]

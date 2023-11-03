#!/bin/bash

source shared.sh

all_passed=1

function run_single_test {
    if [[ $1 != *"test_tcp_pointer"* ]]; then
        # echo $1
        echo "skip test"
    else

        echo "Running test $1..."
        rerun_local_iokerneld
        if [[ $1 == *"tcp"* ]]; then
    	    rerun_mem_server
        fi
        # if run_program ./bin/$1 2>$AIFM_PATH/client.log | grep -q "Passed"; then
        if run_program ./bin/$1 1>$AIFM_PATH/$1.log| grep -q "Passed"; then
        #if run_program ./bin/$1 2>/dev/null| grep -q "Passed"; then
        #if run_program ./bin/$1 1>/dev/stderr| grep -q "Passed"; then
            say_passed
        else
            say_failed
    	    all_passed=0
        fi
    fi
}

function run_all_tests {
    TESTS=`ls bin | grep test_`
    for test in $TESTS
    do
        run_single_test $test
    done
}

function cleanup {
    kill_local_iokerneld
    kill_mem_server
}

run_all_tests
cleanup

if [[ $all_passed -eq 1 ]]; then
    exit 0
else
    exit -1
fi

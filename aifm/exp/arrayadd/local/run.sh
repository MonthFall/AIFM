#!/bin/bash

source ../../../shared.sh

object_size=(64 128 256 512 1024 1536 2048 2560 3072)

rm log.*
sudo pkill -9 main
for object_size in "${object_size[@]}" 
do
    # sed "s/constexpr uint64_t kCacheSize = .*/constexpr uint64_t kCacheSize = $local_rams \* Region::kSize;/g" main.cpp -i
    # sed "s/struct Data {int data[.*/struct Data {int data[$object_size];};/g" main.cpp -i
    sed -i "s/\(struct Data {int data\[\)\([0-9]\+\)\(\];}\)/\1$object_size\3/g" main.cpp
    make clean
    make -j
    rerun_local_iokerneld_noht
    rerun_mem_server    
    run_program_noht ./main 1>log.$object_size 2>&1 &
    ( tail -f -n0 log.$object_size & ) | grep -q "Force existing..."
    sudo pkill -9 main
done
kill_local_iokerneld
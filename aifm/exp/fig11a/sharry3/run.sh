#!/bin/bash

source ../../../shared.sh

local_rams=8192
rm log.*
rm main2
sudo pkill -9 main2

sed "s/constexpr uint64_t kCacheSize = .*/constexpr uint64_t kCacheSize = $local_rams \* Region::kSize;/g" main.cpp -i
make clean
make -j

mv main main2
run_program_noht ./main2 1>log.$local_rams 2>&1 &
( tail -f -n0 log.$local_rams & ) | grep -q "Force existing..."
sudo pkill -9 main2
#!/bin/bash

source ../../../shared.sh

local_rams=8192
rm log.*
rm main1
sudo pkill -9 main1

sed "s/constexpr uint64_t kCacheSize = .*/constexpr uint64_t kCacheSize = $local_rams \* Region::kSize;/g" main.cpp -i
make clean
make -j

mv main main1
run_program_noht ./main1 1>log.$local_rams 2>&1 &

( tail -f -n0 log.$local_rams & ) | grep -q "Force existing..."
sudo pkill -9 main1

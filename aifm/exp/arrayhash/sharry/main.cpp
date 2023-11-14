extern "C" {
#include <runtime/runtime.h>
}

#include "array.hpp"
#include "device.hpp"
#include "helpers.hpp"
#include "manager.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <chrono>
#include <unistd.h>

using namespace far_memory;
using namespace std;

constexpr static uint64_t kCacheSize = (4ULL << 30);
constexpr static uint64_t kFarMemSize = (40ULL << 30);
constexpr static uint64_t kWorkSetSize = (8ULL << 30);
constexpr static uint64_t kNumEntries = kWorkSetSize/sizeof(uint64_t);
constexpr static uint32_t kNumGCThreads = 15;
constexpr static uint32_t kNumConnections = 600;

std::unique_ptr<Array<uint64_t,kNumEntries>> array_ptr;

void set_arrays() {
  for (uint64_t i = 0  ;i < kNumEntries; i++){
    DerefScope scope;
    array_ptr.get()->at_mut(scope,i) = i;
  }
}

void bench() {
  std::vector<rt::Thread> threads;
  prev_us = microtime();
  for (uint32_t tid = 0; tid < kNumMutatorThreads; tid++) {
    threads.emplace_back(rt::Thread([&, tid]() {
      uint32_t cnt = 0;
      while (1) {
        if (unlikely(cnt++ % kPrintPerIters == 0)) {
          preempt_disable();
          print_perf();
          preempt_enable();
        }
        preempt_disable();
        auto core_num = get_core_num();
        auto req_idx =
            all_zipf_req_indices[core_num][per_core_req_idx[core_num].c];
        if (unlikely(++per_core_req_idx[core_num].c == kReqSeqLen)) {
          per_core_req_idx[core_num].c = 0;
        }
        preempt_enable();

        auto &req = all_gen_reqs[req_idx];
        Key key;
        memcpy(key.data, req.data, kReqLen);
        auto start = rdtsc();
        uint32_t array_index = 0;
        {
          DerefScope scope;
          for (uint32_t i = 0; i < kNumKeysPerRequest; i++) {
            append_uint32_to_char_array(i, kLog10NumKeysPerRequest,
                                        key.data + kReqLen);
            Value value;
            uint16_t value_len;
            hopscotch->get(scope, kKeyLen, (const uint8_t *)key.data,
                            &value_len, (uint8_t *)value.data);
            array_index += value.num;
          }
        }
        {
          array_index %= kNumArrayEntries;
          DerefScope scope;
          const auto &array_entry =
              array->at</* NT = */ true>(scope, array_index);
          preempt_disable();
          consume_array_entry(array_entry);
          preempt_enable();
        }
        auto end = rdtsc();
        preempt_disable();
        core_num = get_core_num();
        lats[core_num][(lats_idx[core_num].c++) % kLatsWinSize] = end - start;
        preempt_enable();
        ACCESS_ONCE(req_cnts[tid].c)++;
      }
    }));
  }
  for (auto &thread : threads) {
    thread.Join();
  }
}

void do_work(FarMemManager *manager) {
  auto start = chrono::steady_clock::now();
  array_ptr.reset(manager->allocate_array_heap<uint64_t,kNumEntries>());

  set_arrays();
  printf("setup finished\n");
  bench();

  auto end = chrono::steady_clock::now();
  cout << "Elapsed time in microseconds : "
       << chrono::duration_cast<chrono::microseconds>(end - start).count()
       << " µs" << endl;
  

  std::cout << "Force existing..." << std::endl;
  exit(0);
}

int argc;
void _main(void *arg) {
  char **argv = static_cast<char **>(arg);
  std::string ip_addr_port(argv[1]);
  auto raddr = helpers::str_to_netaddr(ip_addr_port);
  std::unique_ptr<FarMemManager> manager =
      std::unique_ptr<FarMemManager>(FarMemManagerFactory::build(
          kCacheSize, kNumGCThreads,
          new TCPDevice(raddr, kNumConnections, kFarMemSize)));
  do_work(manager.get());
}

int main(int _argc, char *argv[]) {
  int ret;

  if (_argc < 3) {
    std::cerr << "usage: [cfg_file] [ip_addr:port]" << std::endl;
    return -EINVAL;
  }

  char conf_path[strlen(argv[1]) + 1];
  strcpy(conf_path, argv[1]);
  for (int i = 2; i < _argc; i++) {
    argv[i - 1] = argv[i];
  }
  argc = _argc - 1;

  ret = runtime_init(conf_path, _main, argv);
  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }

  return 0;
}

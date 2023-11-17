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
constexpr static uint64_t kFarMemSize = (30ULL << 30);
constexpr static uint64_t kWorkSetSize = (1ULL << 30);
struct Data {int data[1536];};
constexpr static uint32_t kNumEntries = kWorkSetSize/sizeof(Data);
constexpr static uint32_t kLength = sizeof(Data)/sizeof(int);
constexpr static uint32_t kArrayNum = 8;
constexpr static uint32_t kNumGCThreads = 15;
constexpr static uint32_t kNumConnections = 600;

std::unique_ptr<Array<Data, kNumEntries>> fm_array_ptrs[kArrayNum];
    
void set_arrays() {
  for (uint32_t i = 0; i < kArrayNum; i++){
    for (uint32_t j = 0; j < kNumEntries; j++) {
      DerefScope scope;
      for(uint32_t k = 0 ;k < kLength; k++){
        fm_array_ptrs[i]->at_mut(scope, j).data[k] = j;
      }
    }
  }
}

void add_arrays() {
  for (uint32_t i = 0; i < kArrayNum; i++){
    for (uint32_t j = 0; j < kNumEntries; j++) {
      DerefScope scope;
      auto *ptra =  fm_array_ptrs[i]->at_mut(scope, j).data;
      auto *ptrb =  fm_array_ptrs[kArrayNum-1-i]->at(scope, j).data;
      auto *ptrc =  fm_array_ptrs[0]->at(scope, j).data;
      for(uint32_t k = 0 ;k < kLength; k++){
        ptra[k] = ptrb[k] + ptrc[k];
        // fm_array_ptrs[i]->at_mut(scope, j).data[k] = fm_array_ptrs[kArrayNum-1-i]->at(scope, j).data[k]+fm_array_ptrs[0]->at(scope, j).data[k];
      }
    }
    printf("add finished %d\n",i);
  }
}

void do_work(FarMemManager *manager) {
  auto start = chrono::steady_clock::now();
  for (uint32_t i = 0; i < kArrayNum; i++) {
    fm_array_ptrs[i].reset(manager->allocate_array_heap<Data,kNumEntries>());                    
  }

  set_arrays();
  printf("setup finished\n");
  add_arrays();

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
extern "C" {
#include <runtime/runtime.h>
}

#include "deref_scope.hpp"
#include "device.hpp"
#include "manager.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>
#include <unistd.h>

using namespace far_memory;
using namespace std;

constexpr uint64_t kCacheSize = 256 * Region::kSize;  //1<<28
constexpr uint64_t kFarMemSize = (1ULL << 33); // 8 GB.
constexpr uint64_t kWorkSetSize = 1 << 27;
constexpr uint64_t kNumGCThreads = 1;

struct Data4096 {
  // char data[Object::kMaxObjectDataSize];
  uint32_t data[(Object::kMaxObjectDataSize/4)];
};

using Data_t = struct Data4096;

constexpr uint64_t kNumEntries = kWorkSetSize / sizeof(Data_t)+793;

void do_work(FarMemManager *manager) {
  std::vector<UniquePtr<Data_t>> vec;
  cout << "Running " << __FILE__ "..." << endl;

  for (uint32_t i = 0; i < kNumEntries; i++) {
    auto far_mem_ptr = manager->allocate_unique_ptr<Data_t>();
    {
      DerefScope scope;
      auto raw_mut_ptr = far_mem_ptr.deref_mut(scope);
      // memset(raw_mut_ptr->data, static_cast<char>(i), sizeof(Data_t));
      // memset(raw_mut_ptr->data, i, sizeof(Data_t));
      for(int j =0;j<Object::kMaxObjectDataSize/4;j++){raw_mut_ptr->data[j]=i;};
      printf("%d th value:%d\n",i,raw_mut_ptr->data[i]);
    }
    vec.emplace_back(std::move(far_mem_ptr));
  }

  for (uint32_t i = 0; i < kNumEntries; i++) {
    {
      usleep(10);
      DerefScope scope;
      const auto raw_const_ptr = vec[i].deref(scope);
      for (uint32_t j = 0; j < sizeof(Data_t)/4; j++) {
        // if (raw_const_ptr->data[j] != static_cast<char>(i)) {
        if (raw_const_ptr->data[j] != i){
          printf("%d=  %d",raw_const_ptr->data[j],i);
          goto fail;
        }
      }
    }
  }

  cout << "Passed" << endl;
  return;

fail:
  cout << "Failed" << endl;
  return;
}

void _main(void *arg) {
  auto manager = std::unique_ptr<FarMemManager>(FarMemManagerFactory::build(
      kCacheSize, kNumGCThreads, new FakeDevice(kFarMemSize)));
  do_work(manager.get());
}

int main(int argc, char *argv[]) {
  int ret;

  if (argc < 2) {
    std::cerr << "usage: [cfg_file]" << std::endl;
    return -EINVAL;
  }

  ret = runtime_init(argv[1], _main, NULL);
  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }

  return 0;
}

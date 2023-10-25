extern "C" {
#include <runtime/rcu.h>
#include <runtime/runtime.h>
#include <runtime/storage.h>
#include <runtime/thread.h>
#include <runtime/timer.h>
}
#include "sync.h"
#include "thread.h"
#define __user
#include "ksched.h"

#include "deref_scope.hpp"
#include "remote_manager.hpp"
#include "internal/ds_info.hpp"
#include "pointer.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <optional>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <utility>
#include <vector>

namespace far_memory {
    RemoteManager::RemoteManager(uint64_t far_mem_size):far_mem_region_manager_(far_mem_size, false){
        BUG_ON(far_mem_size >= (1ULL << FarMemPtrMeta::kObjectIDBitSize));
        ksched_fd_ = open("/dev/ksched", O_RDWR);
        if (ksched_fd_ < 0) {
            LOG_PRINTF("%s\n", "Warn: fail to open /dev/ksched.");
        for (uint8_t ds_id = std::numeric_limits<decltype(available_ds_ids_)::value_type>::min();
            ds_id <std::numeric_limits<decltype(available_ds_ids_)::value_type>::max();ds_id++) {
                if (ds_id != kVanillaPtrDSID) {
                    available_ds_ids_.push(ds_id);
                }
            }       
        }
    }

    RemoteManager::~RemoteManager(){
        while (ACCESS_ONCE(pending_gcs_)) {
            thread_yield();
        }
    }

    uint64_t RemoteManager::allocate_remote_object(uint16_t object_size){
        preempt_disable();
        auto guard = helpers::finally([&]() { preempt_enable(); });
        std::optional<uint64_t> optional_remote_addr;
    retry_allocate_far_mem:
        auto &free_remote_region = far_mem_region_manager_.core_local_free_region(false);
        optional_remote_addr = free_remote_region.allocate_object(object_size);
        if (unlikely(!optional_remote_addr)) {
            bool success = far_mem_region_manager_.try_refill_core_local_free_region(false, &free_remote_region);
            if (unlikely(!success)) {
                preempt_enable();
                mutator_wait_for_gc_far_mem();
                preempt_disable();
            }
            goto retry_allocate_far_mem;
        }
        return *optional_remote_addr;
    }

    void RemoteManager::mutator_wait_for_gc_far_mem() {
        LOG_PRINTF("%s\n", "Warn: GCing far mem has not been implemented yet.");
    }

    uint8_t RemoteManager::allocate_ds_id() {
        auto ds_id = available_ds_ids_.front();
        available_ds_ids_.pop();
        return ds_id;
    }

    void RemoteManager::free_ds_id(uint8_t ds_id) { available_ds_ids_.push(ds_id); }
}
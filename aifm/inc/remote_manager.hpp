#pragma once

#include "manager.hpp"

namespace far_memory {

    class RemoteManager {
    private:
        FarMemManager::RegionManager far_mem_region_manager_;
        std::queue<uint8_t> available_ds_ids_;   

        std::atomic<uint32_t> pending_gcs_{0};
        int ksched_fd_;
        // static ObjLocker obj_locker_;
                
    public:
        RemoteManager(uint64_t far_mem_size);
        ~RemoteManager();
        
        uint8_t allocate_ds_id();
        void free_ds_id(uint8_t ds_id);
        uint64_t allocate_remote_object(uint16_t object_size);
        void mutator_wait_for_gc_far_mem();
    };
}
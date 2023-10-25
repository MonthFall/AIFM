#pragma once

#include "sync.h"

#include "cb.hpp"
#include "region.hpp"

#include <optional>

namespace far_memory {
    class RegionManager {
    private:
        constexpr static double kPickRegionMaxRetryTimes = 3;

        std::unique_ptr<uint8_t> local_cache_ptr_;
        CircularBuffer<Region, false> free_regions_;
        CircularBuffer<Region, false> used_regions_;
        CircularBuffer<Region, false> nt_used_regions_;
        rt::Spin region_spin_;
        Region core_local_free_regions_[helpers::kNumCPUs];
        Region core_local_free_nt_regions_[helpers::kNumCPUs];
        friend class FarMemTest;

    public:
        RegionManager(uint64_t size, bool is_local);
        void push_free_region(Region &region);
        std::optional<Region> pop_used_region();
        bool try_refill_core_local_free_region(bool nt, Region *full_region);
        Region &core_local_free_region(bool nt);
        double get_free_region_ratio() const;
        uint32_t get_num_regions() const;
    };
}

#include "internal/region_manager.ipp"
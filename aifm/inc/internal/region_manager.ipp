#pragma once

namespace far_memory {
    FORCE_INLINE Region &
    RegionManager::core_local_free_region(bool nt) {
        assert(!preempt_enabled());
        auto core_num = get_core_num();
        return nt ? core_local_free_nt_regions_[core_num]
                    : core_local_free_regions_[core_num];
    }

    FORCE_INLINE double
    RegionManager::get_free_region_ratio() const {
        return static_cast<double>(free_regions_.size()) / get_num_regions();
    }

    FORCE_INLINE uint32_t RegionManager::get_num_regions() const {
        return free_regions_.capacity();
    }
}
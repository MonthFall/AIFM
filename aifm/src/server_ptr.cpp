extern "C" {
#include <base/assert.h>
#include <base/compiler.h>
#include <base/limits.h>
#include <base/stddef.h>
}

#include "object.hpp"
#include "server_ptr.hpp"

namespace far_memory {

ServerPtr::ServerPtr(uint32_t param_len, uint8_t *params): far_mem_region_manager_( *(reinterpret_cast<uint64_t *>(params)),false){
  uint64_t size;
  BUG_ON(param_len != sizeof(decltype(size)));
  size = *(reinterpret_cast<decltype(size) *>(params));
  buf_.reset(reinterpret_cast<uint8_t *>(malloc(size)));
}

ServerPtr::~ServerPtr() {}

uint64_t ServerPtr::allocate_object(uint64_t obj_id,uint16_t object_size){
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

void ServerPtr::read_object(uint8_t obj_id_len, const uint8_t *obj_id,
                            uint16_t *data_len, uint8_t *data_buf) {
  const uint64_t &object_id = *(reinterpret_cast<const uint64_t *>(obj_id));
  assert(obj_id_len == sizeof(decltype(object_id)));
  auto remote_object_addr = reinterpret_cast<uint64_t>(buf_.get()) + object_id;
  Object remote_object(remote_object_addr);
  *data_len = remote_object.get_data_len();
  memcpy(data_buf, reinterpret_cast<uint8_t *>(remote_object.get_data_addr()),
         *data_len);
}

void ServerPtr::write_object(uint8_t obj_id_len, const uint8_t *obj_id,
                             uint16_t data_len, const uint8_t *data_buf) {
  const uint64_t &object_id = *(reinterpret_cast<const uint64_t *>(obj_id));
  assert(obj_id_len == sizeof(decltype(object_id)));
  auto remote_object_addr = reinterpret_cast<uint64_t>(buf_.get()) + object_id;
  Object remote_object(remote_object_addr);
  memcpy(reinterpret_cast<uint8_t *>(remote_object.get_data_addr()), data_buf,
         data_len);
  remote_object.set_data_len(data_len);
  remote_object.set_obj_id_len(obj_id_len);
}

bool ServerPtr::remove_object(uint8_t obj_id_len, const uint8_t *obj_id) {
  BUG();
}

void ServerPtr::compute(uint8_t opcode, uint16_t input_len,
                        const uint8_t *input_buf, uint16_t *output_len,
                        uint8_t *output_buf) {
  BUG();
}

void ServerPtr::mutator_wait_for_gc_far_mem() {
    LOG_PRINTF("%s\n", "Warn: GCing far mem has not been implemented yet.");
    Region region;
    for(int i = 0;i<1000;i++){
      region = *(far_mem_region_manager_.pop_used_region());
      far_mem_region_manager_.push_free_region(region);
    }
}

ServerDS *ServerPtrFactory::build(uint32_t param_len, uint8_t *params) {
  return new ServerPtr(param_len, params);
}

}; // namespace far_memory

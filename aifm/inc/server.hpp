#pragma once

#include "internal/ds_info.hpp"
#include "server_ds.hpp"
#include "remote_manager.hpp"

#include <functional>
#include <memory>

namespace far_memory {
class Server {
private:
  ServerDSFactory *registered_server_ds_factorys_[kMaxNumDSTypes];
  static std::unique_ptr<ServerDS> server_ds_ptrs_[kMaxNumDSIDs];

  constexpr static uint64_t FarMemSize =  (1ULL << 33);
  std::unique_ptr<RemoteManager> manager;

public:
  Server();
  void register_ds(uint8_t ds_type, ServerDSFactory *factory);
  void construct(uint8_t ds_type, uint8_t ds_id, uint8_t param_len,
                 uint8_t *params);
  void destruct(uint8_t ds_id);
  void read_object(uint8_t ds_id, uint8_t obj_id_len, const uint8_t *obj_id,
                   uint16_t *data_len, uint8_t *data_buf);
  uint64_t allocate_object(uint16_t data_len);
  void write_object(uint8_t ds_id, uint8_t obj_id_len, const uint8_t *obj_id,
                    uint16_t data_len, const uint8_t *data_buf);
  bool remove_object(uint64_t ds_id, uint8_t obj_id_len, const uint8_t *obj_id);
  void compute(uint8_t ds_id, uint8_t opcode, uint16_t input_len,
               const uint8_t *input_buf, uint16_t *output_len,
               uint8_t *output_buf);
  uint8_t allocate_ds_id();
  void free_ds_id(uint8_t);
  static ServerDS *get_server_ds(uint8_t ds_id);
};
} // namespace far_memory

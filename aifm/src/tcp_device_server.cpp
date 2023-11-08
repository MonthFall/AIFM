extern "C" {
#include <net/ip.h>
#include <runtime/runtime.h>
#include <runtime/tcp.h>
#include <runtime/thread.h>
}
#include "thread.h"

#include "device.hpp"
#include "helpers.hpp"
#include "object.hpp"
#include "server.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

using namespace far_memory;

std::vector<rt::Thread> slave_threads;
std::unique_ptr<uint8_t> far_mem;

std::atomic<bool> has_shutdown{true};

std::atomic<int> alloc_occupied_int=0;
std::atomic_flag alloc_occupied = ATOMIC_FLAG_INIT;
std::atomic<int> req_ready_int=0;
std::atomic<uint16_t> requested_size_uint16=0;
std::atomic<uint8_t> requested_ds_id_uint8=0;
std::atomic<uint64_t> requested_obj_id_uint64=0;
std::atomic<uint64_t> allocated_addr_uint64=0;

// std::atomic<int> free_done_int=0;
// std::atomic<int> free_ready_int=0;
// std::atomic<uint64_t> free_ptr_ull=0;

rt::Thread master_thread;
rt::Thread alloc_thread;
Server server;

// std::vector<rt::Thread> alloc_threads[kMaxNumDSIDs];

uint64_t alloc_fn(uint16_t object_size){
    // ----------------- allocate start -------------------------- //
  while(alloc_occupied.test_and_set()){
    // check is there any thread is using the allocator
    // alloc_occupied == 1 means: allocator is occupied
    // if alloc_occupied == 0 means: the allocator is free, set the alloc_occupied = 1
    thread_yield();
  }

  // --------------------------- area ------------------- //
  requested_size_uint16.store(object_size);
  req_ready_int.store(1);
  
  while(req_ready_int.load()){
    // check is my allocation finished
    // req_ready_int == 0 means: allocation finished
    // req_ready_int == 1 means: allocation doing
    // thread_yield();
  }
  uint64_t addr = allocated_addr_uint64.load();
  // --------------------------- area ------------------- //
  alloc_occupied.clear();
  // ----------------- allocate end -------------------------- //
  return addr;
}

// Request:
//     |OpCode = Init (1B)|Far Mem Size (8B)|
// Response:
//     |Ack (1B)|
void process_init(tcpconn_t *c) {
  uint64_t *far_mem_size;
  uint8_t req[sizeof(decltype(*far_mem_size))];
  helpers::tcp_read_until(c, req, sizeof(req));

  far_mem_size = reinterpret_cast<uint64_t *>(req);
  *far_mem_size = ((*far_mem_size - 1) / helpers::kHugepageSize + 1) *
                  helpers::kHugepageSize;
  auto far_mem_ptr =
      static_cast<uint8_t *>(helpers::allocate_hugepage(*far_mem_size));
  BUG_ON(far_mem_ptr == nullptr);
  far_mem.reset(far_mem_ptr);

  barrier();
  uint8_t ack;
  helpers::tcp_write_until(c, &ack, sizeof(ack));
}

// Request:
//     |Opcode = Shutdown (1B)|
// Response:
//     |Ack (1B)|
void process_shutdown(tcpconn_t *c) {
  far_mem.reset();

  uint8_t ack;
  helpers::tcp_write_until(c, &ack, sizeof(ack));

  for (auto &thread : slave_threads) {
    thread.Join();
  }
  slave_threads.clear();
}

// Request:
// |Opcode = KOpReadObject(1B) | ds_id(1B) | obj_id_len(1B) | obj_id |
// Response:
// |data_len(2B)|data_buf(data_len B)|
void process_read_object(tcpconn_t *c) {
  uint8_t
      req[Object::kDSIDSize + Object::kIDLenSize + Object::kMaxObjectIDSize];
  uint8_t resp[Object::kDataLenSize + Object::kMaxObjectDataSize];

  helpers::tcp_read_until(c, req, Object::kDSIDSize + Object::kIDLenSize);
  auto ds_id = *const_cast<uint8_t *>(&req[0]);
  auto object_id_len = *const_cast<uint8_t *>(&req[Object::kDSIDSize]);
  auto *object_id = &req[Object::kDSIDSize + Object::kIDLenSize];
  helpers::tcp_read_until(c, object_id, object_id_len);

  auto *data_len = reinterpret_cast<uint16_t *>(&resp);
  auto *data_buf = &resp[Object::kDataLenSize];
  server.read_object(ds_id, object_id_len, object_id, data_len, data_buf);

  helpers::tcp_write_until(c, resp, Object::kDataLenSize + *data_len);
}

// Request:
// |Opcode = KOpWriteObject (1B)|ds_id(1B)|obj_id_len(1B)|data_len(2B)|
// |obj_id(obj_id_len B)|data_buf(data_len)|
// Response:
// |Ack (1B)|
void process_write_object(tcpconn_t *c) {
  uint8_t req[Object::kDSIDSize + Object::kIDLenSize + Object::kDataLenSize +
              Object::kMaxObjectIDSize + Object::kMaxObjectDataSize];

  helpers::tcp_read_until(
      c, req, Object::kDSIDSize + Object::kIDLenSize + Object::kDataLenSize);

  auto ds_id = *const_cast<uint8_t *>(&req[0]);
  auto object_id_len = *const_cast<uint8_t *>(&req[Object::kDSIDSize]);
  auto data_len = *reinterpret_cast<uint16_t *>(
      &req[Object::kDSIDSize + Object::kIDLenSize]);

  helpers::tcp_read_until(
      c, &req[Object::kDSIDSize + Object::kIDLenSize + Object::kDataLenSize],
      object_id_len + data_len);

  auto *object_id = const_cast<uint8_t *>(
      &req[Object::kDSIDSize + Object::kIDLenSize + Object::kDataLenSize]);
  auto *data_buf =
      const_cast<uint8_t *>(&req[Object::kDSIDSize + Object::kIDLenSize +
                                 Object::kDataLenSize + object_id_len]);

  server.write_object(ds_id, object_id_len, object_id, data_len, data_buf);

  uint8_t ack;
  helpers::tcp_write_until(c, &ack, sizeof(ack));
}

// Request:
// |Opcode = KOpWriteObject (1B)|ds_id(1B)|obj_id_len(1B)|data_len(2B)|
// |obj_id(obj_id_len B)|data_buf(data_len)|
// Response:
// |Ack (1B)|obj_id_len(1B)|obj_id(obj_id_len B)|
void process_write_object_rt_objectid(tcpconn_t *c) {
  uint8_t req[Object::kDSIDSize + Object::kIDLenSize + Object::kDataLenSize +
              Object::kMaxObjectIDSize + Object::kMaxObjectDataSize];
  uint8_t resp[Object::kIDLenSize+Object::kMaxObjectIDSize];

  helpers::tcp_read_until(
      c, req, Object::kDSIDSize + Object::kIDLenSize + Object::kDataLenSize);

  auto ds_id = *const_cast<uint8_t *>(&req[0]);
  auto object_id_len = *const_cast<uint8_t *>(&req[Object::kDSIDSize]);
  auto data_len = *reinterpret_cast<uint16_t *>(
      &req[Object::kDSIDSize + Object::kIDLenSize]);

  helpers::tcp_read_until(
      c, &req[Object::kDSIDSize + Object::kIDLenSize + Object::kDataLenSize],
      object_id_len + data_len);

  auto *object_id = const_cast<uint8_t *>(
      &req[Object::kDSIDSize + Object::kIDLenSize + Object::kDataLenSize]);
  auto *data_buf =
      const_cast<uint8_t *>(&req[Object::kDSIDSize + Object::kIDLenSize +
                                 Object::kDataLenSize + object_id_len]);
  
  uint16_t object_len =  Object::kHeaderSize + data_len + kVanillaPtrObjectIDSize;
  // uint64_t addr = server.allocate_object(object_len); 
  // uint64_t addr = alloc_fn(object_len);

  // ----------------- allocate start -------------------------- //
  while(alloc_occupied.test_and_set()){
    // check is there any thread is using the allocator
    // alloc_occupied == 1 means: allocator is occupied
    // if alloc_occupied == 0 means: the allocator is free, set the alloc_occupied = 1
    thread_yield();
  }

  // --------------------------- area ------------------- //
  requested_size_uint16.store(object_len);
  requested_ds_id_uint8.store(ds_id);
  requested_obj_id_uint64.store(*(reinterpret_cast<uint64_t*>(object_id)));
  req_ready_int.store(1);
  
  while(req_ready_int.load()){
    // check is my allocation finished
    // req_ready_int == 0 means: allocation finished
    // req_ready_int == 1 means: allocation doing
    thread_yield();
  }
  uint64_t addr = allocated_addr_uint64.load();
  // --------------------------- area ------------------- //
  alloc_occupied.clear();
  // ----------------- allocate end -------------------------- //

  uint8_t addr_len = static_cast<uint8_t>(sizeof(addr));
  // printf("addr = %lu, object_len = %hu, data_len = %hu \n",addr,object_len,data_len);

  // server.write_object(ds_id, object_id_len, object_id, data_len, data_buf);
  auto *addr_ptr = reinterpret_cast<const uint8_t *>(&addr);
  server.write_object(ds_id, addr_len, addr_ptr, data_len, data_buf);

  uint8_t ack;
  helpers::tcp_write_until(c, &ack, sizeof(ack));
  
  memcpy(&resp[0],&addr_len,Object::kIDLenSize);
  memcpy(&resp[Object::kIDLenSize],&addr,addr_len);
  helpers::tcp_write_until(c, &resp, Object::kIDLenSize+addr_len);
}

// Request:
// |Opcode = kOpRemoveObject (1B)|ds_id(1B)|obj_id_len(1B)|obj_id(obj_id_len B)|
// Response:
// |exists (1B)|
void process_remove_object(tcpconn_t *c) {
  uint8_t
      req[Object::kDSIDSize + Object::kIDLenSize + Object::kMaxObjectIDSize];

  helpers::tcp_read_until(c, req, Object::kDSIDSize + Object::kIDLenSize);
  auto ds_id = *const_cast<uint8_t *>(&req[0]);
  auto obj_id_len = *const_cast<uint8_t *>(&req[Object::kDSIDSize]);

  helpers::tcp_read_until(c, &req[Object::kDSIDSize + Object::kIDLenSize],
                          obj_id_len);

  auto *obj_id =
      const_cast<uint8_t *>(&req[Object::kDSIDSize + Object::kIDLenSize]);
  bool exists = server.remove_object(ds_id, obj_id_len, obj_id);

  helpers::tcp_write_until(c, &exists, sizeof(exists));
}

// Request:
// |Opcode = kOpConstruct (1B)|ds_type(1B)|ds_id(1B)|
// |param_len(1B)|params(param_len B)|
// Response:
// |Ack (1B)|
void process_construct(tcpconn_t *c) {
  uint8_t ds_type;
  uint8_t ds_id;
  uint8_t param_len;
  uint8_t *params;
  uint8_t req[sizeof(ds_type) + Object::kDSIDSize + sizeof(param_len) +
              std::numeric_limits<decltype(param_len)>::max()];

  helpers::tcp_read_until(
      c, req, sizeof(ds_type) + Object::kDSIDSize + sizeof(param_len));
  ds_type = *const_cast<uint8_t *>(&req[0]);
  ds_id = *const_cast<uint8_t *>(&req[sizeof(ds_type)]);
  param_len = *const_cast<uint8_t *>(&req[sizeof(ds_type) + Object::kDSIDSize]);
  helpers::tcp_read_until(
      c, &req[sizeof(ds_type) + Object::kDSIDSize + sizeof(param_len)],
      param_len);
  params = const_cast<uint8_t *>(
      &req[sizeof(ds_type) + Object::kDSIDSize + sizeof(param_len)]);

  server.construct(ds_type, ds_id, param_len, params);

  uint8_t ack;
  helpers::tcp_write_until(c, &ack, sizeof(ack));
}

// Request:
// |Opcode = kOpDeconstruct (1B)|ds_id(1B)|
// Response:
// |Ack (1B)|
void process_destruct(tcpconn_t *c) {
  uint8_t ds_id;

  helpers::tcp_read_until(c, &ds_id, Object::kDSIDSize);

  server.destruct(ds_id);

  uint8_t ack;
  helpers::tcp_write_until(c, &ack, sizeof(ack));
}

// Request:
// |Opcode = kOpCompute(1B)|ds_id(1B)|opcode(1B)|input_len(2B)|
// |input_buf(input_len)|
// Response:
// |output_len(2B)|output_buf(output_len B)|
void process_compute(tcpconn_t *c) {
  uint8_t opcode;
  uint16_t input_len;
  uint8_t req[Object::kDSIDSize + sizeof(opcode) + sizeof(input_len) +
              TCPDevice::kMaxComputeDataLen];

  helpers::tcp_read_until(
      c, req, Object::kDSIDSize + sizeof(opcode) + sizeof(input_len));

  auto ds_id = *reinterpret_cast<uint8_t *>(&req[0]);
  opcode = *reinterpret_cast<uint8_t *>(&req[Object::kDSIDSize]);
  input_len =
      *reinterpret_cast<uint16_t *>(&req[Object::kDSIDSize + sizeof(opcode)]);
  assert(input_len <= TCPDevice::kMaxComputeDataLen);

  if (input_len) {
    helpers::tcp_read_until(
        c, &req[Object::kDSIDSize + sizeof(opcode) + sizeof(input_len)],
        input_len);
  }

  auto *input_buf = const_cast<uint8_t *>(
      &req[Object::kDSIDSize + sizeof(opcode) + sizeof(input_len)]);

  uint16_t *output_len;
  uint8_t resp[sizeof(*output_len) + TCPDevice::kMaxComputeDataLen];
  output_len = reinterpret_cast<uint16_t *>(&resp[0]);
  uint8_t *output_buf = &resp[sizeof(*output_len)];
  server.compute(ds_id, opcode, input_len, input_buf, output_len, output_buf);

  helpers::tcp_write_until(c, resp, sizeof(*output_len) + *output_len);
}

// Request:
// |Opcode = KOpAllocateDSID(1B) |
// Response:
// | ds_id(1B) |
void process_allocate_ds_id(tcpconn_t *c) {
  uint8_t resp[Object::kDSIDSize];
  uint8_t ds_id = server.allocate_ds_id();

  uint8_t ack;
  helpers::tcp_write_until(c, &ack, sizeof(ack));

  memcpy(&resp[0],&ds_id,Object::kDSIDSize);
  helpers::tcp_write_until(c, &resp, sizeof(resp));
}

// Request:
// |Opcode = KOpFreeDSID(1B) | ds_id(1B) |
// Response:
// |Ack (1B)|
void process_free_ds_id(tcpconn_t *c){
  uint8_t req[Object::kDSIDSize];
  helpers::tcp_read_until(c, req, Object::kDSIDSize);

  auto ds_id = *const_cast<uint8_t *>(&req[0]);
  server.free_ds_id(ds_id);

  uint8_t ack;
  helpers::tcp_write_until(c, &ack, sizeof(ack));
}

void slave_fn(tcpconn_t *c) {
  // Run event loop.
  uint8_t opcode;
  int ret;
  while ((ret = tcp_read(c, &opcode, TCPDevice::kOpcodeSize)) > 0) {
    BUG_ON(ret != TCPDevice::kOpcodeSize);
    switch (opcode) {
    case TCPDevice::kOpReadObject:
      process_read_object(c);
      break;
    case TCPDevice::kOpWriteObject:
      //process_write_object(c);
      process_write_object_rt_objectid(c);
      break;
    case TCPDevice::kOpRemoveObject:
      process_remove_object(c);
      break;
    case TCPDevice::kOpConstruct:
      process_construct(c);
      break;
    case TCPDevice::kOpDeconstruct:
      process_destruct(c);
      break;
    case TCPDevice::kOpCompute:
      process_compute(c);
      break;
    case TCPDevice::kOpAllocateDSID:
      process_allocate_ds_id(c);
      break;
    case TCPDevice::kOpFreeDSID:
      process_free_ds_id(c);
      break;
    default:
      BUG();
    }
  }
  tcp_close(c);
}

void master_fn(tcpconn_t *c) {
  uint8_t opcode;
  helpers::tcp_read_until(c, &opcode, TCPDevice::kOpcodeSize);
  BUG_ON(opcode != TCPDevice::kOpInit);
  process_init(c);

  helpers::tcp_read_until(c, &opcode, TCPDevice::kOpcodeSize);
  BUG_ON(opcode != TCPDevice::kOpShutdown);
  process_shutdown(c);
  tcp_close(c);
  has_shutdown = true;
}

void nextgen_alloc_begin(){
  while (1)
    // check is any thread occupied allocator
    if (req_ready_int.load()) { 
      // if occupied, doing allocation
      // uint16_t size = requested_size_uint16.load();

      uint64_t addr = server.allocate_object(requested_ds_id_uint8.load(),requested_obj_id_uint64.load(),requested_size_uint16.load());
      allocated_addr_uint64.store(addr);
      req_ready_int.store(0);
      // allocation finished 
    }
}

void do_work(uint16_t port) {
  tcpqueue_t *q;
  struct netaddr server_addr = {.ip = 0, .port = port};
  tcp_listen(server_addr, 1, &q);

  tcpconn_t *c;
  while (tcp_accept(q, &c) == 0) {
    if (has_shutdown) {
      master_thread = rt::Thread([c]() { master_fn(c); });
      alloc_thread = rt::Thread([]() { nextgen_alloc_begin(); });
      has_shutdown = false;
    } else {
      slave_threads.emplace_back(rt::Thread([c]() { slave_fn(c); }));
    }
  }
}

int argc;

void my_main(void *arg) {
  char **argv = static_cast<char **>(arg);
  int port = atoi(argv[1]);
  do_work(port);
}

int main(int _argc, char *argv[]) {
  int ret;

  if (_argc < 3) {
    std::cerr << "usage: [cfg_file] [port]" << std::endl;
    return -EINVAL;
  }

  char conf_path[strlen(argv[1]) + 1];
  strcpy(conf_path, argv[1]);
  for (int i = 2; i < _argc; i++) {
    argv[i - 1] = argv[i];
  }
  argc = _argc - 1;

  ret = runtime_init(conf_path, my_main, argv);
  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }

  return 0;
}

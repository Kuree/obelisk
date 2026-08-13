//===- SimulationRuntimeABILowering.cpp - Native runtime declarations -===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinTypes.h"

using namespace mlir;

namespace obelisk::detail {

void declareNativeRuntimeABI(ModuleOp module) {
  MLIRContext *context = module.getContext();
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_signal",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_signal_transition",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 64), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_static_transition",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_real_transition",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 32), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_static_specialization_guard",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32),
       IntegerType::get(context, 32), IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_static_nba_specialization_guard",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_event", LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_event_create",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_event_after",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 32), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_event_triggered",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_handle_offset",
      IntegerType::get(context, 64),
      {IntegerType::get(context, 64), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_nba", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_static_nba",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_static_nba_claim", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_static_nba_packed",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_static_nba_stage_wide",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_string_nba",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_managed_nba",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_scheduler_fail",
      LLVM::LLVMVoidType::get(context),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_alloc", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_alloc_with_roots",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_alloc_with_typed_roots",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_retain",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64)});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_native_state_release",
                           IntegerType::get(context, 32),
                           {LLVM::LLVMPointerType::get(context),
                            IntegerType::get(context, 64),
                            IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_load_plane",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 32),
       IntegerType::get(context, 32), LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_store_plane",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 32),
       LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_state_store_continuous_plane",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 32),
       LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_override", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 32), IntegerType::get(context, 32),
       LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_native_release_override",
      IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 64),
       IntegerType::get(context, 32), IntegerType::get(context, 32)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_argument_ref_load", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 32), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 32),
       IntegerType::get(context, 32), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_argument_ref_store", IntegerType::get(context, 32),
      {LLVM::LLVMPointerType::get(context), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       LLVM::LLVMPointerType::get(context), IntegerType::get(context, 64),
       IntegerType::get(context, 32), IntegerType::get(context, 64),
       IntegerType::get(context, 64), IntegerType::get(context, 32),
       IntegerType::get(context, 32), LLVM::LLVMPointerType::get(context),
       LLVM::LLVMPointerType::get(context)});
  Type managedPointer = LLVM::LLVMPointerType::get(context);
  Type managedI32 = IntegerType::get(context, 32);
  Type managedI64 = IntegerType::get(context, 64);
  Type managedF64 = Float64Type::get(context);
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_current_lane",
                           managedPointer, {managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_root_push", managedI32,
                           {managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_root_pop", managedI32,
                           {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_gc_root_range_push", managedI32,
      {managedPointer, managedPointer, managedPointer, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_root_range_pop",
                           managedI32, {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_gc_managed_root_range_push", managedI32,
      {managedPointer, managedPointer, managedPointer, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_managed_root_range_pop",
                           managedI32, {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_candidate_root",
                           managedI64,
                           {managedPointer, managedI64, managedI32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_gc_safepoint", managedI32,
                           {managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_object_allocate", managedI32,
                           {managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_reference_path_index_create", managedI32,
      {managedPointer, managedPointer, managedI64, managedPointer, managedI64,
       managedI32, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_container_size", managedI64,
                           {managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_container_create_like",
                           managedI32,
                           {managedPointer, managedPointer, managedPointer,
                            managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_container_create_typed", managedI32,
      {managedPointer, managedI32, managedI64, managedI32, managedI32,
       managedI64, managedI64, managedI64, managedPointer, managedI64,
       managedI64, managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_mailbox_create_typed", managedI32,
      {managedPointer, managedI64, managedI32, managedI32, managedI64,
       managedI64, managedI64, managedPointer, managedI64, managedI64,
       managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_container_clone", managedI32,
                           {managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_container_delete", managedI32,
                           {managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_queue_delete_index",
                           managedI32, {managedPointer, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_queue_insert", managedI32,
                           {managedPointer, managedPointer, managedI64,
                            managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_mailbox_try_put", managedI32,
                           {managedPointer, managedPointer, managedPointer,
                            managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_mailbox_num", managedI32,
                           {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_mailbox_try_peek", managedI32,
      {managedPointer, managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_mailbox_try_get", managedI32,
      {managedPointer, managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_semaphore_create", managedI32,
                           {managedPointer, managedI32, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_semaphore_put", managedI32,
                           {managedPointer, managedI32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_semaphore_try_get",
                           managedI32,
                           {managedPointer, managedI32, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_random_bounded", managedI32,
                           {managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_random_distribution",
                           managedI32,
                           {managedPointer, managedI32, managedI32, managedI32,
                            managedI32, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_random_cycle_next", managedI32,
      {managedI64, managedI64, managedI32, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_random_next", managedI32,
                           {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_random_seed", managedI32,
                           {managedPointer, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_sampled_read", managedI32,
      {managedPointer, managedI64, managedI64, managedPointer,
       managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_sampled_history", managedI32,
      {managedPointer, managedI64, managedI64, managedI64, managedI32,
       managedI32, managedPointer, managedPointer, managedPointer,
       managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_clocked_sample_update", managedI32,
      {managedPointer, managedI64, managedI64, managedI64, managedI32,
       managedI32, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_clocked_sample_read", managedI32,
      {managedPointer, managedI64, managedI64, managedI64, managedI64,
       managedI32, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_random_solve_modes_state", managedI32,
      {managedPointer, managedPointer, managedI64, managedI64, managedI64,
       managedI64, managedI64, managedI64, managedI64, managedPointer,
       managedI64, managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_random_solve_wide_modes_state", managedI32,
      {managedPointer, managedPointer, managedI64, managedPointer,
       managedPointer, managedI64, managedI64, managedI64, managedI64,
       managedI64, managedPointer, managedI64, managedPointer, managedI64,
       managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_covergroup_create", managedI32,
      {managedPointer, managedI64, managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_covergroup_set_enabled",
                           managedI32,
                           {managedPointer, managedI64, managedI32});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_covergroup_sample_enabled",
                           managedI32,
                           {managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_covergroup_bin_hit", managedI32,
      {managedPointer, managedI64, managedI32, managedI32});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_covergroup_sample", managedI32,
      {managedPointer, managedI64, managedPointer, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_covergroup_instance_query",
                           managedI32,
                           {managedPointer, managedI64, managedPointer,
                            managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_covergroup_type_query", managedI32,
      {managedPointer, managedI64, managedPointer, managedI64, managedPointer,
       managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_container_read", managedI32,
      {managedPointer, managedI64, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_container_write", managedI32,
                           {managedPointer, managedPointer, managedI64,
                            managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_assoc_create_typed", managedI32,
      {managedPointer, managedI64, managedI32, managedI32, managedI64,
       managedI64, managedI64, managedPointer, managedI64, managedI32,
       managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_assoc_read_checked", managedI32,
      {managedPointer, managedPointer, managedPointer, managedI64,
       managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_assoc_write_checked", managedI32,
      {managedPointer, managedPointer, managedPointer, managedPointer,
       managedI64, managedPointer, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_assoc_exists", managedI32,
                           {managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_assoc_delete", managedI32,
                           {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_assoc_set_default_checked",
                           managedI32,
                           {managedPointer, managedPointer, managedPointer,
                            managedI64, managedPointer, managedI64});
  for (StringRef name :
       {"obelisk_rt_v1_assoc_first", "obelisk_rt_v1_assoc_last",
        "obelisk_rt_v1_assoc_next", "obelisk_rt_v1_assoc_prev"})
    getOrDeclareLLVMFunction(
        module, name, managedI32,
        {managedPointer, managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_reference_path_assoc_create", managedI32,
      {managedPointer, managedPointer, managedPointer, managedPointer,
       managedI64, managedI32, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_object_shallow_copy", managedI32,
      {managedPointer, managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_object_read", managedI32,
      {managedPointer, managedI64, managedPointer, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_object_write", managedI32,
      {managedPointer, managedI64, managedPointer, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_object_read_planes", managedI32,
      {managedPointer, managedI64, managedPointer, managedPointer, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_object_write_planes", managedI32,
      {managedPointer, managedI64, managedPointer, managedPointer, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_object_field_load",
                           managedI32,
                           {managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_object_field_store",
                           managedI32,
                           {managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_object_is_instance",
                           managedI32, {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_object_id", managedI64,
                           {managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_managed_watch", managedI64,
                           {managedPointer, managedI32, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_object_cast", managedI32,
                           {managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_method_invoke", managedI32,
                           {managedPointer, managedPointer, managedI64,
                            managedI64, managedPointer, managedI32,
                            managedPointer, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_interface_method_invoke", managedI32,
      {managedPointer, managedPointer, managedI64, managedI64, managedI64,
       managedPointer, managedI32, managedPointer, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_method_task_activate", managedI32,
      {managedPointer, managedPointer, managedI64, managedI64, managedPointer,
       managedI32, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_interface_method_task_activate", managedI32,
      {managedPointer, managedPointer, managedI64, managedI64, managedI64,
       managedPointer, managedI32, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_weak_create", managedI32,
      {managedPointer, managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_weak_get", managedI32,
                           {managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_weak_clear", managedI32,
                           {managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_create", managedI32,
      {managedPointer, managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_from_packed",
                           managedI32,
                           {managedPointer, managedPointer, managedPointer,
                            managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_to_packed", managedI32,
      {managedI64, managedPointer, managedPointer, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_concat_many", managedI32,
      {managedPointer, managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_repeat", managedI32,
      {managedPointer, managedI64, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_length", managedI64,
                           {managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_getc", managedI32,
                           {managedI64, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_putc", managedI32,
      {managedPointer, managedI64, managedI64, managedI32, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_substr", managedI32,
      {managedPointer, managedI64, managedI64, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_compare", managedI32,
                           {managedI64, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_compare_insensitive",
                           managedI32, {managedI64, managedI64});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_case_convert", managedI32,
      {managedPointer, managedI64, managedI32, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_parse_integer",
                           managedI32,
                           {managedI64, managedI32, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_parse_real",
                           managedI32, {managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_format_integer", managedI32,
      {managedPointer, managedI64, managedI32, managedI32, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_string_format_real",
                           managedI32,
                           {managedPointer, managedF64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_file_open_string_mcd",
                           managedI32,
                           {managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_file_open_string", managedI32,
      {managedPointer, managedI64, managedI64, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_dump_open_string",
                           managedI32, {managedPointer, managedI64});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_file_getline_string",
                           managedI32,
                           {managedPointer, managedPointer, managedI32,
                            managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_file_scan_field", managedI32,
                           {managedPointer, managedPointer, managedI32,
                            managedI32, managedPointer, managedI64, managedI32,
                            managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_file_error_string",
                           managedI32,
                           {managedPointer, managedPointer, managedI32,
                            managedPointer, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_string_scan_field", managedI32,
      {managedPointer, managedI64, managedI32, managedPointer, managedI64,
       managedI32, managedPointer, managedPointer, managedPointer});
  getOrDeclareLLVMFunction(module, "obelisk_rt_v1_plusarg_test", managedI32,
                           {managedPointer, managedI64, managedPointer});
  getOrDeclareLLVMFunction(
      module, "obelisk_rt_v1_plusarg_value", managedI32,
      {managedPointer, managedPointer, managedI64, managedPointer,
       managedPointer});
}

} // namespace obelisk::detail

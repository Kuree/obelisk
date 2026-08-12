//===- DesignBytecodeRoots.cpp - Bytecode managed roots -----------------===//

#include "DesignBytecodeRoots.h"

#include <algorithm>
#include <cstring>

namespace {

using namespace obelisk::designbytecode;

void visitObjectWord(const uint8_t *address, ManagedRootVisit visit,
                     void *visitorEnvironment) {
  // Byte-backed interpreter and automatic-state storage does not promise
  // pointer alignment. The collector is non-moving, so an aligned temporary
  // is sufficient for precise marking and avoids undefined typed loads.
  obelisk_rt_object_v1 *object = nullptr;
  std::memcpy(&object, address, sizeof(object));
  visit(visitorEnvironment, &object);
}

void visitManagedWord(const uint8_t *address, ManagedRootVisit visit,
                      void *visitorEnvironment) {
  obelisk_rt_managed_word_v1 word = 0;
  std::memcpy(&word, address, sizeof(word));
  if (word == 0 || (word & 3) != 0)
    return;
  obelisk_rt_object_v1 *object =
      reinterpret_cast<obelisk_rt_object_v1 *>(static_cast<uintptr_t>(word));
  visit(visitorEnvironment, &object);
}

} // namespace

namespace obelisk::designbytecode {

void ScopedBytecodeFrameRoots::enumerate(void *environment,
                                         ManagedRootVisit visit,
                                         void *visitorEnvironment) {
  auto *roots = static_cast<ScopedBytecodeFrameRoots::Roots *>(environment);
  if (!roots || !roots->image || !roots->frame || !visit)
    return;
  for (uint32_t index = 0; index != roots->frame->function.layoutCount;
       ++index) {
    Layout layout = layoutAt(*roots->image, roots->frame->function, index);
    if (layout.kind == OBELISK_RT_DBREG_STRING) {
      visitManagedWord(roots->frame->data + layout.offset, visit,
                       visitorEnvironment);
      continue;
    }
    if (layout.kind != OBELISK_RT_DBREG_MANAGED &&
        layout.kind != OBELISK_RT_DBREG_MANAGED_REF &&
        layout.kind != OBELISK_RT_DBREG_ARGUMENT_REF)
      continue;
    visitObjectWord(roots->frame->data + layout.offset, visit,
                    visitorEnvironment);
  }
}

ScopedBytecodeFrameRoots::ScopedBytecodeFrameRoots(const Image &image,
                                                   Frame &frame,
                                                   obelisk_rt_context *context)
    : roots{&image, &frame} {
  bool hasManaged = false;
  for (uint32_t index = 0; index != frame.function.layoutCount; ++index) {
    uint8_t kind = layoutAt(image, frame.function, index).kind;
    hasManaged |= kind == OBELISK_RT_DBREG_MANAGED ||
                  kind == OBELISK_RT_DBREG_MANAGED_REF ||
                  kind == OBELISK_RT_DBREG_ARGUMENT_REF ||
                  kind == OBELISK_RT_DBREG_STRING;
  }
  if (!hasManaged)
    return;
  lane = obelisk_rt_v1_gc_current_lane(context);
  status =
      lane ? obelisk_rt_managed_roots_push(lane, &provider, enumerate, &roots)
           : OBELISK_RT_INVALID_LIFECYCLE;
  pushed = status == OBELISK_RT_OK;
}

ScopedBytecodeFrameRoots::~ScopedBytecodeFrameRoots() {
  if (pushed)
    (void)obelisk_rt_managed_roots_pop(lane, &provider);
}

} // namespace obelisk::designbytecode

using namespace obelisk::designbytecode;

void obelisk_rt_enumerate_design_managed_roots(
    obelisk_rt_context *context, ManagedRootVisit visit,
    void *visitorEnvironment) noexcept {
  if (!context || !visit)
    return;
  try {
    bool hasBytecode =
        context->execution &&
        (context->execution->flags & OBELISK_RT_EXECUTION_HAS_BYTECODE) != 0;
    Image image;
    if (hasBytecode) {
      obelisk_rt_design_bytecode_entry_v1 entry{context->execution, 0, 0};
      hasBytecode = loadValidatedImage(entry, context, image);
    }
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    for (auto &[id, state] : context->nativeAutomaticStates) {
      (void)id;
      if (state.managedRootRegistered)
        visit(visitorEnvironment, &state.managedValue);
      for (uint64_t offset : state.managedRootByteOffsets) {
        if (offset > state.value.size() ||
            sizeof(obelisk_rt_object_v1 *) > state.value.size() - offset)
          continue;
        visitObjectWord(state.value.data() + offset, visit, visitorEnvironment);
      }
      for (const NativeAutomaticState::CandidateRoot &root :
           state.candidateRootByteOffsets) {
        if (root.byteOffset > state.value.size() ||
            sizeof(obelisk_rt_managed_word_v1) >
                state.value.size() - root.byteOffset)
          continue;
        obelisk_rt_managed_word_v1 word = 0;
        std::memcpy(&word, state.value.data() + root.byteOffset, sizeof(word));
        word = obelisk_rt_v1_gc_candidate_root(context, word,
                                               root.allowedKinds);
        if (word != 0 && (word & UINT64_C(3)) == 0) {
          obelisk_rt_object_v1 *object =
              reinterpret_cast<obelisk_rt_object_v1 *>(
                  static_cast<uintptr_t>(word));
          visit(visitorEnvironment, &object);
        }
      }
    }
    for (obelisk_rt_process_instance_v1 *instance :
         context->managedRootProcesses) {
      if (!instance || !instance->descriptor ||
          !instance->descriptor->frame_layout || !instance->frame)
        continue;
      const obelisk_rt_frame_layout_v1 &layout =
          *instance->descriptor->frame_layout;
      for (uint32_t index = 0; index != layout.field_count; ++index) {
        const obelisk_rt_frame_field_v1 &field = layout.fields[index];
        if (field.flags != OBELISK_RT_FRAME_MANAGED_ROOT &&
            field.flags != OBELISK_RT_FRAME_CANDIDATE_ROOT)
          continue;
        uint8_t *address =
            static_cast<uint8_t *>(instance->frame) + field.offset;
        if (field.flags == OBELISK_RT_FRAME_MANAGED_ROOT) {
          auto **slot = reinterpret_cast<obelisk_rt_object_v1 **>(address);
          visit(visitorEnvironment, slot);
        } else {
          obelisk_rt_managed_word_v1 word = 0;
          std::memcpy(&word, address, sizeof(word));
          word = obelisk_rt_v1_gc_candidate_root(context, word,
                                                 field.reserved);
          if (word != 0 && (word & UINT64_C(3)) == 0) {
            obelisk_rt_object_v1 *object = reinterpret_cast<obelisk_rt_object_v1 *>(
                static_cast<uintptr_t>(word));
            visit(visitorEnvironment, &object);
          }
        }
      }
    }
    for (ScheduledNBA &update : context->scheduledNBAs)
      if (update.managedValue)
        visit(visitorEnvironment, &update.rootedManaged);
      else if (update.stringValue)
        visitManagedWord(
            reinterpret_cast<const uint8_t *>(&update.rootedString), visit,
            visitorEnvironment);
    for (ScheduledDesignNBA &update : context->scheduledDesignNBAs)
      if (update.stringValue)
        visitManagedWord(
            reinterpret_cast<const uint8_t *>(&update.rootedString), visit,
            visitorEnvironment);
    if (!hasBytecode)
      return;
    for (ScheduledDesignTask &task : context->scheduledDesignTasks) {
      if (task.terminated)
        continue;
      auto visitActivation = [&](uint32_t functionIndex,
                                 const std::vector<uint8_t> &frame,
                                 uint64_t scratchOffset) {
        if (functionIndex >= image.functionCount)
          return;
        Function function = functionAt(image, functionIndex);
        uint64_t canonicalSize =
            std::min<uint64_t>(scratchOffset, frame.size());
        auto visitOffset = [&](uint64_t offset) {
          if (offset > canonicalSize ||
              sizeof(obelisk_rt_object_v1 *) > canonicalSize - offset)
            return;
          visitObjectWord(frame.data() + offset, visit, visitorEnvironment);
        };
        for (uint64_t index = 0; index != image.stateDescriptorCount; ++index) {
          CaptureRecord capture = captureAt(image, index);
          if (capture.function != functionIndex ||
              capture.argument >= function.layoutCount ||
              capture.valueOffset == UINT64_MAX)
            continue;
          uint8_t kind = layoutAt(image, function, capture.argument).kind;
          if (kind == OBELISK_RT_DBREG_STRING) {
            if (capture.valueOffset <= canonicalSize &&
                sizeof(obelisk_rt_managed_word_v1) <=
                    canonicalSize - capture.valueOffset)
              visitManagedWord(frame.data() + capture.valueOffset, visit,
                               visitorEnvironment);
          } else if (kind == OBELISK_RT_DBREG_MANAGED ||
                     kind == OBELISK_RT_DBREG_MANAGED_REF ||
                     kind == OBELISK_RT_DBREG_ARGUMENT_REF)
            visitOffset(capture.valueOffset);
        }
        uint64_t end = function.firstInstruction + function.instructionCount;
        for (uint64_t pc = function.firstInstruction; pc != end; ++pc) {
          Instruction instruction = instructionAt(image, pc);
          if (instruction.opcode == OBELISK_RT_DB_FRAME_ROOT ||
              instruction.opcode == OBELISK_RT_DB_CLEAR_FRAME_ROOT) {
            if (instruction.immediate <= canonicalSize &&
                sizeof(obelisk_rt_managed_word_v1) <=
                    canonicalSize - instruction.immediate) {
              const uint8_t *address = frame.data() + instruction.immediate;
              if (instruction.flags == 0) {
                visitManagedWord(address, visit, visitorEnvironment);
              } else {
                obelisk_rt_managed_word_v1 word = 0;
                std::memcpy(&word, address, sizeof(word));
                word = obelisk_rt_v1_gc_candidate_root(
                    context, word, instruction.destination);
                if (word != 0 && (word & UINT64_C(3)) == 0) {
                  obelisk_rt_object_v1 *object =
                      reinterpret_cast<obelisk_rt_object_v1 *>(
                          static_cast<uintptr_t>(word));
                  visit(visitorEnvironment, &object);
                }
              }
            }
            continue;
          }
          if (instruction.opcode != OBELISK_RT_DB_STORE_FRAME ||
              instruction.source0 >= function.layoutCount)
            continue;
          uint8_t kind = layoutAt(image, function, instruction.source0).kind;
          if (kind == OBELISK_RT_DBREG_STRING) {
            if (instruction.immediate <= canonicalSize &&
                sizeof(obelisk_rt_managed_word_v1) <=
                    canonicalSize - instruction.immediate)
              visitManagedWord(frame.data() + instruction.immediate, visit,
                               visitorEnvironment);
          } else if (kind == OBELISK_RT_DBREG_MANAGED ||
                     kind == OBELISK_RT_DBREG_MANAGED_REF ||
                     kind == OBELISK_RT_DBREG_ARGUMENT_REF)
            visitOffset(instruction.immediate);
        }
      };
      visitActivation(task.function, task.frame, task.scratchOffset);
      for (const DesignActivation &caller : task.callers)
        visitActivation(caller.function, caller.frame, caller.scratchOffset);
    }
  } catch (...) {
    // The image was validated when the context was created. A collector cannot
    // report through the C ABI, so malformed or concurrently destroyed state
    // is conservatively ignored here and is diagnosed by its owning entry.
  }
}

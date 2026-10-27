//===- SimulationTypes.h - Executable simulation types ---------*- C++ -*-===//

#ifndef TALOS_DIALECT_SIMULATION_SIMULATIONTYPES_H
#define TALOS_DIALECT_SIMULATION_SIMULATIONTYPES_H

#include "talos/Dialect/Simulation/SimulationEnums.h"

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/Types.h"
#include "mlir/Interfaces/MemorySlotInterfaces.h"

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/TypeSwitch.h"

#define GET_TYPEDEF_CLASSES
#include "talos/Dialect/Simulation/SimulationTypes.h.inc"

namespace talos::sim {

/// Stable runtime storage descriptor ID for a normalized simulation type.
uint64_t getStableStorageTypeID(::mlir::Type type,
                                uint32_t descriptorFlags = 0);

/// Bit width of a normalized packed simulation value, which is either a
/// signless builtin integer or an exact four-state `!simulation.logic`.
/// Returns `std::nullopt` for every other type.
std::optional<unsigned> getPackedWidth(::mlir::Type type);

/// Scalar representation used by packed-value operators. Packed aggregates
/// map to an integer or logic value of the same width and state domain;
/// unpacked values have no scalar representation.
::mlir::Type getPackedScalarType(::mlir::Type type);

/// Whether two normalized types can share an argument-reference alias. Exact
/// types always can. Distinct packed types can when source signedness was
/// checked before normalization and the remaining IEEE 1800-2017 6.22.2(c)
/// requirements -- total width and state domain -- match.
bool haveCompatibleArgumentRefLayout(::mlir::Type lhs, ::mlir::Type rhs);

/// Whether `type` is one of the fixed first-class aggregate types.
bool isAggregateType(::mlir::Type type);

/// Number and element type of declaration-order aggregate subelements.
unsigned getAggregateNumElements(::mlir::Type type);
::mlir::Type getAggregateElementType(::mlir::Type type, unsigned index);

/// Convert a source array index into its zero-based declaration ordinal.
std::optional<unsigned> getArrayElementOrdinal(::mlir::Type type,
                                               int64_t sourceIndex);

/// Structural span used only by descriptor provenance analysis. Unlike packed
/// width, this assigns ABI-stable, naturally aligned intervals to unpacked
/// struct/array children. Managed handles occupy one 64-bit aligned word.
std::optional<uint64_t> getProvenanceSpan(::mlir::Type type);

/// Natural bit alignment used by structural provenance layout.
std::optional<uint64_t> getProvenanceAlignment(::mlir::Type type);

/// Whether the immediate fixed provenance representation owns an unknown
/// plane. Managed-container handles are leaves here; their element type does
/// not make the handle word four-state.
bool containsFourStateLeaf(::mlir::Type type);

/// Structural offset/span for one declaration-order child. Union children all
/// overlap at offset zero.
std::optional<std::pair<uint64_t, uint64_t>>
getAggregateProvenanceSubelement(::mlir::Type type, unsigned index);

/// Distance in canonical storage bits between successive fixed array elements.
/// Unlike an element's value width, an unpacked stride includes tail padding.
std::optional<uint64_t> getArrayElementStorageStride(::mlir::Type type);

/// Build the compact, versioned runtime plan for one legal fixed aggregate
/// bit-stream source. Array extents are represented by REPEAT records.
std::optional<::llvm::SmallVector<uint64_t>>
getFixedBitStreamPlan(::mlir::Type type);

/// Whether values of `type` have a fixed native bit image: a packed value, or
/// an unpacked array or struct with a fixed bit-stream plan.
bool hasFixedBitValue(::mlir::Type type);

/// Build the inverse fixed-target plan. Copy records retain whether each
/// destination leaf is two- or four-state so final X/Z coercion is exact.
std::optional<::llvm::SmallVector<uint64_t>>
getFixedBitStreamImportPlan(::mlir::Type type);

/// Build explicit formatting metadata for a fixed unpacked-array element.
/// The plan records every array extent and byte stride plus the singular leaf
/// representation; an empty plan means the type is not such an array.
std::optional<::llvm::SmallVector<uint64_t>>
getFixedArrayPatternPlan(::mlir::Type type);

/// Build equivalent plans against structural provenance storage. These are
/// reserved for DPI aggregate marshalling, where naturally aligned managed
/// handles and padding must not change language bit-stream cast semantics.
std::optional<::llvm::SmallVector<uint64_t>>
getDPIAggregateBitStreamPlan(::mlir::Type type);
std::optional<::llvm::SmallVector<uint64_t>>
getDPIAggregateBitStreamImportPlan(::mlir::Type type);

/// Build the compact preorder plan for a bit-stream source containing at
/// least one dynamically sized member. Fixed arrays and structures are
/// represented structurally, while dynamic arrays, queues, typed associative
/// arrays, and strings become runtime traversal nodes. Class roots can extend
/// this grammar with the reserved OBJECT node without changing the packing
/// ABI.
std::optional<::llvm::SmallVector<uint64_t>>
getRecursiveBitStreamPlan(::mlir::Type type);

/// Build the version-two recursive plan for a source that may contain class
/// handles.  The resolver supplies the trusted nonzero dispatch-group ID for
/// each OBJECT record; `directRoot` is true only for a direct class-handle
/// source, never for a handle reached through an aggregate or container.
using ClassBitStreamGroupResolver =
    ::llvm::function_ref<std::optional<uint64_t>(ClassHandleType,
                                                 bool directRoot)>;
std::optional<::llvm::SmallVector<uint64_t>>
getRecursiveBitStreamPlan(::mlir::Type type,
                          ClassBitStreamGroupResolver resolver,
                          bool requireDynamic = true);

/// Target-independent identifier for one static class source and root
/// visibility mode.  Materialization rejects the vanishingly unlikely hash
/// collision before this ID becomes runtime authority.
uint64_t getClassBitStreamGroupID(ClassHandleType type, bool allowHiddenRoot);

/// Runtime-managed categories that can occupy a source value word. These are
/// bit flags because an overlapping union slot may legally represent more
/// than one category.
enum class ManagedHandleKind : uint32_t {
  Class = 1u << 0,
  String = 1u << 1,
  Container = 1u << 2,
  ReferencePath = 1u << 3,
};

/// Runtime managed values use an ABI-stable 64-bit tagged word even when the
/// target has 32-bit pointers.
constexpr unsigned managedHandleBitWidth = 64;
constexpr unsigned managedHandleByteWidth = managedHandleBitWidth / 8;
constexpr unsigned managedHandleByteAlignment = managedHandleByteWidth;

/// Runtime trace-layout encoding shared by native class and container
/// descriptors. Exact slots use the runtime typed slot enumerators;
/// candidate slots carry this flag plus a ManagedHandleKind mask.
constexpr uint32_t managedHandleCandidateFlag = uint32_t{1} << 31;

enum class ManagedBuiltinKind : uint32_t { None, Mailbox, Semaphore };

struct ManagedHandleSlot {
  uint64_t bitOffset;
  uint32_t kindMask;
  /// An overlapping source union can also contain ordinary bits. Such a slot
  /// is a root only when its current word names a live object of an allowed
  /// kind; exact slots must always contain a well-typed managed word.
  bool conditional;
  ManagedBuiltinKind builtinKind = ManagedBuiltinKind::None;

  bool operator==(const ManagedHandleSlot &other) const {
    return bitOffset == other.bitOffset && kindMask == other.kindMask &&
           conditional == other.conditional && builtinKind == other.builtinKind;
  }
};

/// Encode a structured managed slot for the public runtime trace ABI.
std::optional<uint32_t>
getManagedHandleTraceKind(const ManagedHandleSlot &slot);

/// Append every managed-handle word in `type`, including its allowed runtime
/// categories and whether tracing is conditional on the current word.
bool getManagedHandleSlots(::mlir::Type type,
                           ::llvm::SmallVectorImpl<ManagedHandleSlot> &slots);

/// Compatibility projection used by analyses that only need root positions.
/// Append every managed-handle word in `type`, as a bit offset from the start
/// of its structural provenance representation.
bool getManagedHandleOffsets(::mlir::Type type,
                             ::llvm::SmallVectorImpl<uint64_t> &offsets);

/// True for a nullable one-word value owned by the precise managed heap.
bool isManagedHandleType(::mlir::Type type);

/// The runtime element descriptor (`talos_rt_element_type_v1`) of a value of
/// `type` held in a managed container, with its trace slots as byte offsets
/// and managed slot kinds.
struct ElementDescriptor {
  uint64_t typeID;
  ElementKind kind;
  ElementFlags flags;
  uint64_t valueSize;
  uint64_t alignment;
  uint64_t bitWidth;
  ::llvm::SmallVector<int64_t, 2> traceOffsets;
  ::llvm::SmallVector<int32_t, 2> traceKinds;
};

/// Describe `type` as a container element, or return std::nullopt when it has
/// no canonical element layout.
std::optional<ElementDescriptor> getElementDescriptor(::mlir::Type type);

/// Build the runtime equality plan (TALOS_RT_ASSOC_KEY_PLAN_*) of an
/// associative-array index type that is none of the string, class, process,
/// wildcard and integral key kinds (IEEE 1800-2023 7.8.5). Returns
/// std::nullopt when part of the type has no such equality, such as a real.
std::optional<::llvm::SmallVector<uint64_t>> getAssocKeyPlan(::mlir::Type type);

/// How the runtime represents the index of an associative array type: its key
/// kind and the width an `talos_rt_assoc_key_v1` of it carries.
struct AssocKeyLayout {
  AssocKeyKind kind;
  uint64_t width;
};

/// The key layout of `type`, or std::nullopt when its index type has none.
std::optional<AssocKeyLayout> getAssocKeyLayout(AssocArrayType type);

/// True for a frame-relative simulator identifier. These are opaque 64-bit
/// words wherever they are stored, independent of whatever they denote.
bool isSimulationHandleType(::mlir::Type type);

/// Bit width of a simulation handle word.
constexpr unsigned simulationHandleBitWidth = 64;

} // namespace talos::sim

#endif // TALOS_DIALECT_SIMULATION_SIMULATIONTYPES_H

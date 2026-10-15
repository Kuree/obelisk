//===- NativeAttrs.td - Native backend attributes -----------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_NATIVE_NATIVEATTRS_TD
#define OBELISK_DIALECT_NATIVE_NATIVEATTRS_TD

include "obelisk/Dialect/Native/NativeDialect.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/BuiltinAttributes.td"

class NativeAttr<string name, string attrMnemonic, list<Trait> traits = []>
    : AttrDef<NativeDialect, name, traits> {
  let mnemonic = attrMnemonic;
}

def NativePartitionAttr : NativeAttr<"Partition", "partition"> {
  let summary = "one native partition: its symbols and cross-partition edges";
  let description = [{
    `members` are the symbols the partition defines; `imports` and `exports`
    are the symbols it references in, and provides to, other partitions;
    `dependencies` are the IDs of the partitions it imports from. Semantic
    manifests also name the code units that `owners` the partition.
  }];
  let parameters = (ins "::mlir::StringAttr":$id,
      OptionalParameter<"::mlir::ArrayAttr">:$owners,
      "::mlir::ArrayAttr":$members, "::mlir::ArrayAttr":$imports,
      "::mlir::ArrayAttr":$exports, "::mlir::ArrayAttr":$dependencies);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def NativeElementDefaultAttr
    : NativeAttr<"ElementDefault", "element_default"> {
  let summary = "the default value of one container element type";
  let description = [{
    Element type `typeId` defaults to `image` rather than to the type default
    of IEEE 1800-2023 Table 6-7, as an unpacked struct with member
    initializers does (7.2.2). `image` is the element's value plane followed,
    for a four-state element, by its unknown plane, in the runtime's
    `default_image` layout.
  }];
  let parameters = (ins "uint64_t":$typeId,
      "::mlir::DenseI8ArrayAttr":$image);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def NativeDesignPartitionsAttr
    : NativeAttr<"DesignPartitions", "design_partitions"> {
  let summary = "the semantic partition manifest of one erased design";
  let parameters = (ins "::mlir::StringAttr":$design,
      "::mlir::ArrayAttr":$partitions);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def NativeFrameFieldAttr : NativeAttr<"FrameField", "frame_field"> {
  let summary = "one field of a coroutine frame descriptor";
  let parameters = (ins "uint32_t":$kind, "uint32_t":$flags,
      "uint64_t":$offset, "uint64_t":$size, "uint32_t":$alignment,
      "uint32_t":$reserved);
  let assemblyFormat = "`<` struct(params) `>`";
}

def NativeFrameAttr : NativeAttr<"Frame", "frame"> {
  let summary = "layout of the coroutine frame a ramp function allocates";
  let parameters = (ins "uint64_t":$size, "uint64_t":$alignment,
      "uint64_t":$checksum, "::mlir::DenseI32ArrayAttr":$continuations,
      // `#native.frame_field` entries in offset order.
      "::mlir::ArrayAttr":$fields);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def NativeFrameSiteAttr : NativeAttr<"FrameSite", "frame_site"> {
  let summary = "a suspension site of a replayed frame";
  let description = [{
    `identity` is the continuation ID, the continuation block index, and the
    wait offset and size; `types` are the continuation argument types.
  }];
  let parameters = (ins "::mlir::DenseI64ArrayAttr":$identity,
      "::mlir::ArrayAttr":$types);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def NativeFrameInputsAttr : NativeAttr<"FrameInputs", "frame_inputs"> {
  let summary = "canonical inputs a replayed frame analysis must reproduce";
  let description = [{
    Frame replay recomputes these from the function and rejects any change.
    `identity` is the code unit, entry kind and home region; `abi` is the
    frame size, alignment, checksum and bytecode read-only bit; `captures`
    are the function argument attributes, when present.
  }];
  let parameters = (ins "::mlir::FunctionType":$signature,
      "::mlir::DenseI64ArrayAttr":$identity, "::mlir::DenseI64ArrayAttr":$abi,
      // `#native.frame_site` entries in suspension order.
      "::mlir::ArrayAttr":$continuations,
      OptionalParameter<"::mlir::ArrayAttr">:$captures);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def NativeFrameCheckpointAttr
    : NativeAttr<"FrameCheckpoint", "frame_checkpoint"> {
  let summary = "module inventory of functions whose frames may be replayed";
  let parameters = (ins OptionalParameter<"::mlir::Attribute">:$layout,
      // Symbol references to the replayable functions.
      "::mlir::ArrayAttr":$functions);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

#endif // OBELISK_DIALECT_NATIVE_NATIVEATTRS_TD

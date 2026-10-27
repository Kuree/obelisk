//===- TalosOps.td - Talos semantic operations ----------*- tablegen -*-===//

#ifndef TALOS_DIALECT_TALOS_TALOSOPS_TD
#define TALOS_DIALECT_TALOS_TALOSOPS_TD

include "talos/Dialect/Talos/TalosDialect.td"
include "talos/Dialect/Talos/TalosEnums.td"
include "talos/Dialect/Talos/TalosTypes.td"
include "mlir/IR/RegionKindInterface.td"
include "mlir/IR/SymbolInterfaces.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

class TalosOp<string mnemonic, list<Trait> traits = []>
    : Op<TalosDialect, mnemonic, traits> {
  // Every operation has a declarative textual form.  Individual operations
  // override this baseline when domain-specific keywords improve readability.
  let assemblyFormat =
      "operands attr-dict `:` functional-type(operands, results)";
}

class RefElementTypeMatches<string reference, string value>
    : TypesMatchWith<
        "reference element type must match value type",
        reference, value,
        "::llvm::cast<::talos::ir::RefType>($_self).getElementType()">;

class NetElementTypeMatches<string net, string value>
    : TypesMatchWith<
        "net element type must match value type",
        net, value,
        "::llvm::cast<::talos::ir::NetType>($_self).getElementType()">;

class AssocKeyTypeMatches<string array, string key>
    : TypesMatchWith<
        "associative array key type must match key operand type",
        array, key,
        "::llvm::cast<::talos::ir::AssocArrayType>($_self).getKeyType()">;

class DynArrayReferenceTypeMatches<string array, string reference>
    : TypesMatchWith<
        "dynamic array element type must match reference element type",
        array, reference,
        "::talos::ir::RefType::get($_self.getContext(), "
        "::llvm::cast<::talos::ir::DynArrayType>($_self).getElementType())">;

class AssocReferenceTypeMatches<string array, string reference>
    : TypesMatchWith<
        "associative array element type must match reference element type",
        array, reference,
        "::talos::ir::RefType::get($_self.getContext(), "
        "::llvm::cast<::talos::ir::AssocArrayType>($_self).getElementType())">;

class MailboxElementTypeMatches<string mailbox, string value>
    : TypesMatchWith<
        "mailbox element type must match value type",
        mailbox, value,
        "::llvm::cast<::talos::ir::MailboxType>($_self).getElementType()">;

class RandCElementTypeMatches<string state, string value>
    : TypesMatchWith<
        "randc element type must match result type",
        state, value,
        "::llvm::cast<::talos::ir::RandCStateType>($_self).getElementType()">;

//===----------------------------------------------------------------------===//
// Exact four-state values
//===----------------------------------------------------------------------===//

def LogicConstantOp : TalosOp<"logic.constant", [Pure, ConstantLike]> {
  let summary = "exact four-state constant";
  let description = [{
    Creates `!talos.logic<W>` from two W-bit integer attributes.  Unknown
    bits use `value=0` for X and `value=1` for Z.  Known bits use `value` as the
    ordinary zero/one plane.
  }];
  let arguments = (ins APIntAttr:$value, APIntAttr:$unknown);
  let results = (outs LogicType:$result);
  let assemblyFormat =
      "$value `,` $unknown attr-dict `:` qualified(type($result))";
  let hasVerifier = 1;
}

def LogicUnaryOp
    : TalosOp<"logic.unary", [Pure, AllTypesMatch<["input", "result"]>]> {
  let summary = "IEEE four-state unary operation";
  let arguments = (ins LogicUnaryKind:$kind, LogicType:$input);
  let results = (outs LogicType:$result);
  let assemblyFormat = [{
    $kind $input attr-dict `:` qualified(type($input)) `->`
    qualified(type($result))
  }];
}

def LogicBinaryOp
    : TalosOp<"logic.binary",
        [Pure, AllTypesMatch<["lhs", "rhs", "result"]>]> {
  let summary = "IEEE four-state arithmetic, bitwise, or logical operation";
  let arguments =
      (ins LogicBinaryKind:$kind, LogicType:$lhs, LogicType:$rhs);
  let results = (outs LogicType:$result);
  let assemblyFormat = [{
    $kind $lhs `,` $rhs attr-dict `:` functional-type(operands, results)
  }];
}

def LogicCompareOp
    : TalosOp<"logic.compare", [Pure, AllTypesMatch<["lhs", "rhs"]>]> {
  let summary = "logical, case, wildcard, or relational comparison";
  let arguments =
      (ins LogicCompareKind:$kind, LogicType:$lhs, LogicType:$rhs);
  let results = (outs AnyType:$result);
  let assemblyFormat = [{
    $kind $lhs `,` $rhs attr-dict `:` functional-type(operands, results)
  }];
  let hasVerifier = 1;
}

def LogicReduceOp : TalosOp<"logic.reduce", [Pure]> {
  let summary = "four-state reduction";
  let arguments = (ins LogicReduceKind:$kind, LogicType:$input);
  let results = (outs Logic1Type:$result);
  let assemblyFormat = [{
    $kind $input attr-dict `:` qualified(type($input)) `->`
    qualified(type($result))
  }];
}

def LogicConcatOp : TalosOp<"logic.concat", [Pure]> {
  let summary = "packed concatenation preserving X and Z";
  let arguments = (ins Variadic<LogicType>:$inputs);
  let results = (outs LogicType:$result);
  let assemblyFormat = [{
    $inputs attr-dict `:` functional-type($inputs, $result)
  }];
  let hasVerifier = 1;
}

def LogicExtractOp : TalosOp<"logic.extract", [Pure]> {
  let summary = "constant packed part-select";
  let arguments = (ins LogicType:$input, I64Attr:$lowBit);
  let results = (outs LogicType:$result);
  let assemblyFormat = [{
    $input `from` $lowBit attr-dict `:` qualified(type($input)) `->`
    qualified(type($result))
  }];
  let hasVerifier = 1;
}

def LogicDynExtractOp : TalosOp<"logic.dyn_extract", [Pure]> {
  let summary = "dynamic packed part-select with SystemVerilog bounds behavior";
  let arguments = (ins LogicType:$input, AnyType:$lowBit);
  let results = (outs LogicType:$result);
  let assemblyFormat = [{
    $input `from` $lowBit attr-dict `:` functional-type(operands, results)
  }];
}

def LogicShiftOp : TalosOp<"logic.shift", [Pure]> {
  let summary = "four-state shift with independently typed shift amount";
  let arguments = (ins
    LogicBinaryKind:$kind,
    LogicType:$input,
    AnyType:$amount
  );
  let results = (outs LogicType:$result);
  let assemblyFormat = [{
    $kind $input `by` $amount attr-dict `:`
    functional-type(operands, results)
  }];
  let hasVerifier = 1;
}

def LogicBoolCastOp : TalosOp<"logic.bool_cast", [Pure]> {
  let summary = "SystemVerilog truth conversion preserving unknown";
  let arguments = (ins LogicType:$input);
  let results = (outs Logic1Type:$result);
  let assemblyFormat = [{
    $input attr-dict `:` qualified(type($input)) `->`
    qualified(type($result))
  }];
}

def LogicReplicateOp : TalosOp<"logic.replicate", [Pure]> {
  let summary = "packed replication preserving X and Z";
  let arguments = (ins LogicType:$input);
  let results = (outs LogicType:$result);
  let assemblyFormat = [{
    $input attr-dict `:` qualified(type($input)) `->`
    qualified(type($result))
  }];
  let hasVerifier = 1;
}

def LogicResizeOp : TalosOp<"logic.resize", [Pure]> {
  let summary = "truncate, zero-extend, or sign-extend four-state logic";
  let arguments = (ins LogicType:$input, BoolAttr:$isSigned);
  let results = (outs LogicType:$result);
  let assemblyFormat = [{
    $input `signed` `=` $isSigned attr-dict `:` qualified(type($input)) `->`
    qualified(type($result))
  }];
}

def LogicInsertOp
    : TalosOp<"logic.insert", [Pure, AllTypesMatch<["input", "result"]>]> {
  let summary = "constant packed part update";
  let arguments =
      (ins LogicType:$input, LogicType:$replacement, I64Attr:$lowBit);
  let results = (outs LogicType:$result);
  let assemblyFormat = [{
    $replacement `into` $input `at` $lowBit attr-dict `:`
    functional-type(operands, results)
  }];
  let hasVerifier = 1;
}

def LogicMuxOp : TalosOp<"logic.mux", [
    Pure, AllTypesMatch<["trueValue", "falseValue", "result"]>
  ]> {
  let summary = "conditional expression with IEEE unknown-condition merging";
  let arguments =
      (ins Logic1Type:$condition, LogicType:$trueValue, LogicType:$falseValue);
  let results = (outs LogicType:$result);
  let assemblyFormat = [{
    $condition `?` $trueValue `:` $falseValue attr-dict `:`
    functional-type(operands, results)
  }];
}

def LogicIsKnownOp : TalosOp<"logic.is_known", [Pure]> {
  let summary = "test whether every bit is zero or one";
  let arguments = (ins LogicType:$input);
  let results = (outs I1:$result);
  let assemblyFormat =
      "$input attr-dict `:` qualified(type($input)) `->` type($result)";
}

def LogicFromBitsOp : TalosOp<"logic.from_bits", [Pure]> {
  let summary = "inject a two-state integer into exact four-state logic";
  let arguments = (ins AnyInteger:$input);
  let results = (outs LogicType:$result);
  let assemblyFormat = [{
    $input attr-dict `:` type($input) `->` qualified(type($result))
  }];
  let hasVerifier = 1;
}

def LogicToBitsOp : TalosOp<"logic.to_bits", [Pure]> {
  let summary = "apply SystemVerilog four-state to two-state conversion";
  let description = [{
    Converts X and Z bits to zero as required when assigning a four-state value
    to a two-state packed type.  This operation is deliberately explicit so an
    optimization cannot silently discard unknowns.
  }];
  let arguments = (ins LogicType:$input);
  let results = (outs AnyInteger:$result);
  let assemblyFormat = [{
    $input attr-dict `:` qualified(type($input)) `->` type($result)
  }];
  let hasVerifier = 1;
}

def RefExtractOp : TalosOp<"ref.extract", [Pure]> {
  let summary = "reference to a constant subelement or part-select";
  let arguments = (ins RefType:$input, I32Attr:$lowBit);
  let results = (outs RefType:$result);
  let assemblyFormat = [{
    $input `from` $lowBit attr-dict `:` qualified(type($input)) `->`
    qualified(type($result))
  }];
  let hasVerifier = 1;
}

def RefDynExtractOp : TalosOp<"ref.dyn_extract", [Pure]> {
  let summary = "reference to a dynamically selected subelement";
  let arguments = (ins RefType:$input, AnyType:$lowBit);
  let results = (outs RefType:$result);
  let assemblyFormat = [{
    $input `from` $lowBit attr-dict `:`
    functional-type(operands, results)
  }];
  let hasVerifier = 1;
}

def RefConcatOp : TalosOp<"ref.concat", [Pure]> {
  let summary = "composite lvalue reference formed by concatenation";
  let arguments = (ins Variadic<RefType>:$inputs);
  let results = (outs RefType:$result);
  let assemblyFormat = [{
    $inputs attr-dict `:` functional-type($inputs, $result)
  }];
  let hasVerifier = 1;
}

def NetExtractOp : TalosOp<"net.extract", [Pure]> {
  let summary = "net handle to a constant subelement or part-select";
  let arguments = (ins NetType:$input, I32Attr:$lowBit);
  let results = (outs NetType:$result);
  let assemblyFormat = [{
    $input `from` $lowBit attr-dict `:` qualified(type($input)) `->`
    qualified(type($result))
  }];
  let hasVerifier = 1;
}

def NetDynExtractOp : TalosOp<"net.dyn_extract", [Pure]> {
  let summary = "net handle to a dynamically selected subelement";
  let arguments = (ins NetType:$input, AnyType:$lowBit);
  let results = (outs NetType:$result);
  let assemblyFormat = [{
    $input `from` $lowBit attr-dict `:`
    functional-type(operands, results)
  }];
  let hasVerifier = 1;
}

def NetConcatOp : TalosOp<"net.concat", [Pure]> {
  let summary = "composite net lvalue formed by concatenation";
  let arguments = (ins Variadic<NetType>:$inputs);
  let results = (outs NetType:$result);
  let assemblyFormat = [{
    $inputs attr-dict `:` functional-type($inputs, $result)
  }];
  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//
// Storage, nets, force/release, and nonblocking assignment
//===----------------------------------------------------------------------===//

def GlobalOp : TalosOp<"global", [Symbol, IsolatedFromAbove]> {
  let summary = "design-lifetime mutable storage";
  let arguments = (ins SymbolNameAttr:$sym_name, TypeAttr:$type);
  let regions = (region AnyRegion:$initializer);
  let assemblyFormat =
      "$sym_name `:` $type attr-dict-with-keyword $initializer";
}

def GlobalReturnOp : TalosOp<"global.return", [Terminator]> {
  let summary = "return a global initializer value";
  let arguments = (ins Optional<AnyType>:$value);
  let assemblyFormat = "($value^ `:` type($value))? attr-dict";
}

def GetGlobalOp : TalosOp<"get_global", [Pure]> {
  let summary = "obtain a reference to global storage";
  let arguments = (ins FlatSymbolRefAttr:$name);
  let results = (outs RefType:$result);
  let assemblyFormat =
      "$name attr-dict `:` qualified(type($result))";
}

def VarAllocOp : TalosOp<"var.alloc"> {
  let summary = "allocate process, automatic, or scope storage";
  let arguments = (ins Optional<AnyType>:$initialValue);
  let results = (outs RefType:$result);
  let assemblyFormat = [{
    (`=` $initialValue^ `:` type($initialValue))? attr-dict `:`
    qualified(type($result))
  }];
}

def LoadOp : TalosOp<"load", [
    RefElementTypeMatches<"reference", "result">
  ]> {
  let summary = "read mutable variable or field storage";
  let arguments = (ins RefType:$reference);
  let results = (outs AnyType:$result);
  let assemblyFormat = [{
    $reference attr-dict `:` qualified(type($reference)) `->` type($result)
  }];
}

def StoreOp : TalosOp<"store", [
    RefElementTypeMatches<"reference", "value">
  ]> {
  let summary = "blocking update of mutable variable or field storage";
  let arguments = (ins AnyType:$value, RefType:$reference);
  let assemblyFormat = [{
    $value `to` $reference attr-dict `:` type($value) `,`
    qualified(type($reference))
  }];
}

def NetAllocOp : TalosOp<"net.alloc"> {
  let summary = "allocate a resolved net";
  let arguments = (ins NetKind:$kind);
  let results = (outs NetType:$result);
  let assemblyFormat = "$kind attr-dict `:` qualified(type($result))";
}

def NetReadOp : TalosOp<"net.read", [
    NetElementTypeMatches<"net", "result">
  ]> {
  let summary = "read the currently resolved net value";
  let arguments = (ins NetType:$net);
  let results = (outs AnyType:$result);
  let assemblyFormat = [{
    $net attr-dict `:` qualified(type($net)) `->` type($result)
  }];
}

def NetDriveOp : TalosOp<"net.drive", [
    NetElementTypeMatches<"net", "value">
  ]> {
  let summary = "update one persistent driver of a resolved net";
  let arguments = (ins
    NetType:$net,
    AnyType:$value,
    I64Attr:$driver,
    Optional<TimeType>:$delay
  );
  let assemblyFormat = [{
    $net `[` $driver `]` `=` $value (`after` $delay^)?
    attr-dict `:` functional-type(operands, results)
  }];
}

def ForceOp : TalosOp<"force"> {
  let summary = "override a variable or net until release";
  let arguments = (ins AnyType:$target, AnyType:$value);
  let assemblyFormat = [{
    $target `=` $value attr-dict `:` type($target) `,` type($value)
  }];
}

def ReleaseOp : TalosOp<"release"> {
  let summary = "remove a force and restore ordinary update behavior";
  let arguments = (ins AnyType:$target);
  let assemblyFormat = "$target attr-dict `:` type($target)";
}

def NBAEnqueueOp : TalosOp<"nba.enqueue", [
    RefElementTypeMatches<"destination", "value">
  ]> {
  let summary = "evaluate-now, update-later nonblocking assignment";
  let arguments = (ins
    AnyType:$value,
    RefType:$destination,
    Optional<TimeType>:$delay
  );
  let assemblyFormat = [{
    $value `to` $destination (`after` $delay^)? attr-dict `:`
    functional-type(operands, results)
  }];
}

//===----------------------------------------------------------------------===//
// Time, scheduler regions, processes, and suspension
//===----------------------------------------------------------------------===//

def TimeConstantOp : TalosOp<"time.constant", [Pure, ConstantLike]> {
  let summary = "simulation time constant in design precision units";
  let arguments = (ins APIntAttr:$value);
  let results = (outs TimeType:$result);
  let assemblyFormat = "$value attr-dict";
}

def TimeAddOp
    : TalosOp<"time.add",
        [Pure, AllTypesMatch<["lhs", "rhs", "result"]>]> {
  let summary = "add scaled simulation times with defined overflow handling";
  let arguments = (ins TimeType:$lhs, TimeType:$rhs);
  let results = (outs TimeType:$result);
  let assemblyFormat = "$lhs `,` $rhs attr-dict";
}

def ProcessOp : TalosOp<"process", [Symbol, IsolatedFromAbove]> {
  let summary = "suspendable simulation process body";
  let arguments = (ins
    SymbolNameAttr:$sym_name,
    ProcessKind:$kind,
    EventRegion:$entryRegion
  );
  let regions = (region AnyRegion:$body);
  let assemblyFormat = [{
    $sym_name $kind `in` $entryRegion attr-dict-with-keyword $body
  }];
}

def ProcessReturnOp : TalosOp<"process.return", [Terminator]> {
  let summary = "complete a process activation";
  let arguments = (ins Variadic<AnyType>:$values);
  let assemblyFormat = "($values^ `:` type($values))? attr-dict";
}

def ProcessSpawnOp : TalosOp<"process.spawn"> {
  let summary = "create and enqueue a child process";
  let arguments = (ins
    FlatSymbolRefAttr:$callee,
    EventRegion:$region,
    Variadic<AnyType>:$captures
  );
  let results = (outs ProcessType:$process);
  let assemblyFormat = [{
    $callee `(` $captures `)` `in` $region attr-dict `:`
    functional-type($captures, $process)
  }];
}

def ProcessJoinOp : TalosOp<"process.join"> {
  let summary = "wait according to join, join_any, or join_none semantics";
  let arguments = (ins JoinKind:$kind, Variadic<ProcessType>:$processes);
  let assemblyFormat = [{
    $kind $processes attr-dict (`:` qualified(type($processes))^)?
  }];
}

def ProcessKillOp : TalosOp<"process.kill"> {
  let summary = "disable or kill a process and its eligible descendants";
  let arguments = (ins ProcessType:$process);
  let assemblyFormat = "$process attr-dict";
}

def RegionEnqueueOp : TalosOp<"region.enqueue"> {
  let summary = "enqueue a process into an explicit IEEE event region";
  let arguments =
      (ins ProcessType:$process, EventRegion:$region, Optional<TimeType>:$delay);
  let assemblyFormat = [{
    $process `in` $region (`after` $delay^)? attr-dict
  }];
}

def SuspendDelayOp : TalosOp<"suspend.delay"> {
  let summary = "suspend the current process for simulation time";
  let arguments = (ins TimeType:$delay, EventRegion:$resumeRegion);
  let assemblyFormat = "$delay `resume` $resumeRegion attr-dict";
}

def SuspendEventOp : TalosOp<"suspend.event"> {
  let summary = "suspend until a named event is triggered";
  let arguments = (ins EventType:$event, EventRegion:$resumeRegion);
  let assemblyFormat = "$event `resume` $resumeRegion attr-dict";
}

def SuspendEdgeOp : TalosOp<"suspend.edge"> {
  let summary = "suspend until an IEEE four-state edge transition";
  let arguments = (ins
    RefType:$observed,
    EdgeKind:$edge,
    EventRegion:$resumeRegion
  );
  let assemblyFormat = [{
    $edge $observed `resume` $resumeRegion attr-dict `:`
    qualified(type($observed))
  }];
}

def SuspendConditionOp : TalosOp<"suspend.condition"> {
  let summary = "suspend until a wait expression becomes true";
  let arguments = (ins Logic1Type:$condition, EventRegion:$resumeRegion);
  let assemblyFormat = [{
    $condition `resume` $resumeRegion attr-dict `:`
    qualified(type($condition))
  }];
}

def EventCreateOp : TalosOp<"event.create"> {
  let summary = "create a named event object";
  let results = (outs EventType:$event);
  let assemblyFormat = "attr-dict";
}

def EventTriggerOp : TalosOp<"event.trigger"> {
  let summary = "trigger a named event immediately or nonblockingly";
  let arguments =
      (ins EventType:$event, EventRegion:$region, BoolAttr:$nonblocking);
  let assemblyFormat =
      "$event `in` $region `nonblocking` `=` $nonblocking attr-dict";
}

//===----------------------------------------------------------------------===//
// Classes, dynamic dispatch, and ownership
//===----------------------------------------------------------------------===//

def ClassOp : TalosOp<"class", [Symbol, SymbolTable, IsolatedFromAbove]> {
  let summary = "elaborated class descriptor";
  let arguments = (ins
    SymbolNameAttr:$sym_name,
    OptionalAttr<FlatSymbolRefAttr>:$base,
    OptionalAttr<ArrayAttr>:$interfaces
  );
  let regions = (region AnyRegion:$body);
  let assemblyFormat = [{
    $sym_name (`extends` $base^)? (`implements` $interfaces^)?
    attr-dict-with-keyword $body
  }];
}

def ClassEndOp : TalosOp<"class.end", [Terminator]> {
  let summary = "terminate a class descriptor body";
  let assemblyFormat = "attr-dict";
}

def ClassFieldOp : TalosOp<"class.field", [Symbol]> {
  let summary = "class field layout entry";
  let arguments =
      (ins SymbolNameAttr:$sym_name, TypeAttr:$type, I64Attr:$ordinal);
  let assemblyFormat =
      "$sym_name `at` $ordinal `:` $type attr-dict";
}

def ClassMethodOp : TalosOp<"class.method", [Symbol, IsolatedFromAbove]> {
  let summary = "class method body and dispatch slot";
  let arguments = (ins
    SymbolNameAttr:$sym_name,
    TypeAttr:$functionType,
    I64Attr:$slot,
    BoolAttr:$isVirtual
  );
  let regions = (region AnyRegion:$body);
  let assemblyFormat = [{
    $sym_name `slot` $slot `virtual` `=` $isVirtual `:` $functionType
    attr-dict-with-keyword $body
  }];
}

def ClassMethodReturnOp : TalosOp<"class.method_return", [Terminator]> {
  let summary = "return from a class method";
  let arguments = (ins Variadic<AnyType>:$values);
  let assemblyFormat = "($values^ `:` type($values))? attr-dict";
}

def ObjectNullOp : TalosOp<"object.null", [Pure, ConstantLike]> {
  let summary = "null class handle";
  let results = (outs ObjectType:$result);
  let assemblyFormat = "attr-dict";
}

def ObjectNewOp : TalosOp<"object.new"> {
  let summary = "allocate and construct a class object";
  let arguments =
      (ins FlatSymbolRefAttr:$className, Variadic<AnyType>:$arguments);
  let results = (outs ObjectType:$result);
  let assemblyFormat = [{
    $className `(` $arguments `)` attr-dict `:`
    functional-type($arguments, $result)
  }];
}

def ObjectRetainOp : TalosOp<"object.retain"> {
  let summary = "retain a class object across an ownership boundary";
  let arguments = (ins ObjectType:$object);
  let assemblyFormat = "$object attr-dict";
}

def ObjectReleaseOp : TalosOp<"object.release"> {
  let summary = "release a class object and run final ownership cleanup";
  let arguments = (ins ObjectType:$object);
  let assemblyFormat = "$object attr-dict";
}

def ObjectFieldRefOp : TalosOp<"object.field_ref"> {
  let summary = "obtain a mutable reference to an object field";
  let arguments = (ins ObjectType:$object, FlatSymbolRefAttr:$field);
  let results = (outs RefType:$result);
  let assemblyFormat = [{
    $object `[` $field `]` attr-dict `:` qualified(type($result))
  }];
}

def ObjectCallOp : TalosOp<"object.call"> {
  let summary = "call a dynamically dispatched class method";
  let arguments = (ins
    ObjectType:$object,
    FlatSymbolRefAttr:$method,
    Variadic<AnyType>:$arguments
  );
  let results = (outs Variadic<AnyType>:$results);
  let assemblyFormat = [{
    $object `[` $method `]` `(` $arguments `)` attr-dict `:`
    functional-type(operands, results)
  }];
}

def BoxOp : TalosOp<"box", [Pure]> {
  let summary = "box a statically typed value";
  let arguments = (ins AnyType:$input);
  let results = (outs BoxType:$result);
  let assemblyFormat =
      "$input attr-dict `:` type($input) `->` qualified(type($result))";
}

def UnboxOp : TalosOp<"unbox", [Pure]> {
  let summary = "checked extraction from a boxed runtime value";
  let arguments = (ins BoxType:$input);
  let results = (outs AnyType:$result);
  let assemblyFormat =
      "$input attr-dict `:` qualified(type($input)) `->` type($result)";
}

//===----------------------------------------------------------------------===//
// Dynamic containers and synchronization
//===----------------------------------------------------------------------===//

def DynArrayCreateOp : TalosOp<"dynarray.create"> {
  let summary = "allocate a dynamic array";
  let arguments = (ins I64:$size, Optional<AnyType>:$defaultValue);
  let results = (outs DynArrayType:$result);
  let assemblyFormat = [{
    $size (`default` $defaultValue^)? attr-dict `:`
    functional-type(operands, results)
  }];
}

def DynArraySizeOp : TalosOp<"dynarray.size"> {
  let summary = "read dynamic array size";
  let arguments = (ins DynArrayType:$array);
  let results = (outs I64:$result);
  let assemblyFormat = "$array attr-dict `:` qualified(type($array))";
}

def DynArrayResizeOp : TalosOp<"dynarray.resize"> {
  let summary = "resize while preserving the required prefix";
  let arguments =
      (ins DynArrayType:$array, I64:$size, Optional<AnyType>:$defaultValue);
  let assemblyFormat = [{
    $array `to` $size (`default` $defaultValue^)? attr-dict `:`
    functional-type(operands, results)
  }];
}

def DynArrayElementRefOp : TalosOp<"dynarray.element_ref", [
    DynArrayReferenceTypeMatches<"array", "result">
  ]> {
  let summary = "bounds-checked dynamic array element reference";
  let arguments = (ins DynArrayType:$array, I64:$index);
  let results = (outs RefType:$result);
  let assemblyFormat = [{
    $array `[` $index `]` attr-dict `:` functional-type(operands, results)
  }];
}

def AssocCreateOp : TalosOp<"assoc.create"> {
  let summary = "allocate an associative array";
  let results = (outs AssocArrayType:$result);
  let assemblyFormat = "attr-dict `:` qualified(type($result))";
}

def AssocElementRefOp : TalosOp<"assoc.element_ref", [
    AssocKeyTypeMatches<"array", "key">,
    AssocReferenceTypeMatches<"array", "result">
  ]> {
  let summary = "lookup or create an associative array element";
  let arguments = (ins AssocArrayType:$array, AnyType:$key);
  let results = (outs RefType:$result);
  let assemblyFormat = [{
    $array `[` $key `]` attr-dict `:` functional-type(operands, results)
  }];
}

def AssocExistsOp : TalosOp<"assoc.exists", [
    AssocKeyTypeMatches<"array", "key">
  ]> {
  let summary = "test for an associative array key";
  let arguments = (ins AssocArrayType:$array, AnyType:$key);
  let results = (outs I1:$result);
  let assemblyFormat = [{
    $array `[` $key `]` attr-dict `:` qualified(type($array)) `,` type($key)
  }];
}

def AssocDeleteOp : TalosOp<"assoc.delete"> {
  let summary = "delete one key or clear an associative array";
  let arguments = (ins AssocArrayType:$array, Optional<AnyType>:$key);
  let assemblyFormat = [{
    $array (`[` $key^ `]`)? attr-dict `:` functional-type(operands, results)
  }];
}

def MailboxCreateOp : TalosOp<"mailbox.create"> {
  let summary = "create a bounded or unbounded mailbox";
  let arguments = (ins I64Attr:$bound);
  let results = (outs MailboxType:$result);
  let assemblyFormat =
      "`bound` $bound attr-dict `:` qualified(type($result))";
}

def MailboxPutOp : TalosOp<"mailbox.put", [
    MailboxElementTypeMatches<"mailbox", "value">
  ]> {
  let summary = "blocking mailbox put";
  let arguments = (ins MailboxType:$mailbox, AnyType:$value);
  let assemblyFormat = [{
    $value `into` $mailbox attr-dict `:` type($value) `,`
    qualified(type($mailbox))
  }];
}

def MailboxGetOp : TalosOp<"mailbox.get", [
    MailboxElementTypeMatches<"mailbox", "value">
  ]> {
  let summary = "blocking mailbox get";
  let arguments = (ins MailboxType:$mailbox);
  let results = (outs AnyType:$value);
  let assemblyFormat = [{
    $mailbox attr-dict `:` qualified(type($mailbox)) `->` type($value)
  }];
}

def MailboxTryGetOp : TalosOp<"mailbox.try_get", [
    MailboxElementTypeMatches<"mailbox", "value">
  ]> {
  let summary = "nonblocking mailbox get";
  let arguments = (ins MailboxType:$mailbox);
  let results = (outs I1:$success, AnyType:$value);
  let assemblyFormat = [{
    $mailbox attr-dict `:` functional-type(operands, results)
  }];
}

def SemaphoreCreateOp : TalosOp<"semaphore.create"> {
  let summary = "create a semaphore with an initial key count";
  let arguments = (ins I64:$keys);
  let results = (outs SemaphoreType:$result);
  let assemblyFormat = "$keys attr-dict";
}

def SemaphoreGetOp : TalosOp<"semaphore.get"> {
  let summary = "blocking semaphore key acquisition";
  let arguments = (ins SemaphoreType:$semaphore, I64:$keys);
  let assemblyFormat = "$keys `from` $semaphore attr-dict";
}

def SemaphoreTryGetOp : TalosOp<"semaphore.try_get"> {
  let summary = "nonblocking semaphore key acquisition";
  let arguments = (ins SemaphoreType:$semaphore, I64:$keys);
  let results = (outs I1:$success);
  let assemblyFormat = "$keys `from` $semaphore attr-dict";
}

def SemaphorePutOp : TalosOp<"semaphore.put"> {
  let summary = "return semaphore keys";
  let arguments = (ins SemaphoreType:$semaphore, I64:$keys);
  let assemblyFormat = "$keys `to` $semaphore attr-dict";
}

//===----------------------------------------------------------------------===//
// Deterministic randomization and constraints
//===----------------------------------------------------------------------===//

def RandomStreamCreateOp : TalosOp<"random.stream_create"> {
  let summary = "create a deterministic stream from an explicit seed";
  let arguments = (ins I64:$seed);
  let results = (outs RandomStreamType:$result);
  let assemblyFormat = "$seed attr-dict";
}

def RandomStreamSplitOp : TalosOp<"random.stream_split"> {
  let summary = "derive a deterministic child stream";
  let arguments = (ins RandomStreamType:$parent, I64:$streamId);
  let results = (outs RandomStreamType:$result);
  let assemblyFormat = "$parent `by` $streamId attr-dict";
}

def RandomNextOp : TalosOp<"random.next"> {
  let summary = "draw unbiased random bits from a stream";
  let arguments = (ins RandomStreamType:$stream);
  let results = (outs AnyInteger:$result);
  let assemblyFormat = "$stream attr-dict `:` type($result)";
}

def RandomSaveOp : TalosOp<"random.save"> {
  let summary = "snapshot random stream state for get_randstate";
  let arguments = (ins RandomStreamType:$stream);
  let results = (outs RandomStateType:$result);
  let assemblyFormat = "$stream attr-dict";
}

def RandomRestoreOp : TalosOp<"random.restore"> {
  let summary = "restore random stream state for set_randstate";
  let arguments = (ins RandomStreamType:$stream, RandomStateType:$state);
  let assemblyFormat = "$stream `from` $state attr-dict";
}

def RandCCreateOp : TalosOp<"randc.create"> {
  let summary = "create a random-cyclic state machine";
  let arguments = (ins RandomStreamType:$stream);
  let results = (outs RandCStateType:$result);
  let assemblyFormat =
      "$stream attr-dict `:` qualified(type($result))";
}

def RandCNextOp : TalosOp<"randc.next", [
    RandCElementTypeMatches<"state", "result">
  ]> {
  let summary = "produce the next nonrepeating value in a randc cycle";
  let arguments = (ins RandCStateType:$state);
  let results = (outs AnyType:$result);
  let assemblyFormat = [{
    $state attr-dict `:` qualified(type($state)) `->` type($result)
  }];
}

def ConstraintSetOp
    : TalosOp<"constraint.set", [Symbol, IsolatedFromAbove]> {
  let summary = "compiled constraint graph";
  let arguments = (ins SymbolNameAttr:$sym_name);
  let regions = (region AnyRegion:$body);
  let assemblyFormat = "$sym_name attr-dict-with-keyword $body";
}

def ConstraintRequireOp : TalosOp<"constraint.require"> {
  let summary = "require a symbolic predicate in a constraint graph";
  let arguments = (ins I1:$condition);
  let assemblyFormat = "$condition attr-dict";
}

def ConstraintEndOp : TalosOp<"constraint.end", [Terminator]> {
  let summary = "terminate a constraint graph";
  let assemblyFormat = "attr-dict";
}

def ConstraintGetOp : TalosOp<"constraint.get", [Pure]> {
  let summary = "obtain a compiled constraint set";
  let arguments = (ins FlatSymbolRefAttr:$name);
  let results = (outs ConstraintSetType:$result);
  let assemblyFormat = "$name attr-dict";
}

def RandomizeOp : TalosOp<"randomize"> {
  let summary = "solve constraints and commit random variables atomically";
  let arguments = (ins
    ObjectType:$object,
    RandomStreamType:$stream,
    ConstraintSetType:$constraints
  );
  let results = (outs I1:$success);
  let assemblyFormat =
      "$object `using` $stream `with` $constraints attr-dict";
}

//===----------------------------------------------------------------------===//
// Assertions, coverage, foreign interfaces, VPI, and system effects
//===----------------------------------------------------------------------===//

def AssertImmediateOp : TalosOp<"assert.immediate"> {
  let summary = "immediate assertion with explicit unknown handling";
  let arguments =
      (ins Logic1Type:$condition, Severity:$severity, StrAttr:$message);
  let assemblyFormat = [{
    $severity $condition `,` $message attr-dict `:`
    qualified(type($condition))
  }];
}

def AssertSampleOp : TalosOp<"assert.sample"> {
  let summary = "sample a concurrent assertion automaton";
  let arguments = (ins
    FlatSymbolRefAttr:$property,
    EventRegion:$region,
    Variadic<AnyType>:$samples
  );
  let assemblyFormat = [{
    $property `in` $region `(` $samples `)` attr-dict
    (`:` type($samples)^)?
  }];
}

def CoverSampleOp : TalosOp<"cover.sample"> {
  let summary = "sample functional or assertion coverage";
  let arguments =
      (ins FlatSymbolRefAttr:$coverpoint, Variadic<AnyType>:$samples);
  let assemblyFormat = [{
    $coverpoint `(` $samples `)` attr-dict (`:` type($samples)^)?
  }];
}

def DPITaskCallOp : TalosOp<"dpi.task_call"> {
  let summary = "call a potentially time-consuming imported DPI task";
  let description = [{
    Pure/non-suspending DPI functions use the ordinary typed call path. This op
    exists for exported or context tasks which may interact with scheduler time
    and therefore cannot be modeled as an ordinary call.
  }];
  let arguments =
      (ins FlatSymbolRefAttr:$callee, Variadic<AnyType>:$arguments);
  let results = (outs Variadic<AnyType>:$results);
  let assemblyFormat = [{
    $callee `(` $arguments `)` attr-dict `:`
    functional-type($arguments, $results)
  }];
}

def VPIGetOp : TalosOp<"vpi.get"> {
  let summary = "read a reflected VPI property or value";
  let arguments = (ins VPIHandleType:$handle, I32Attr:$property);
  let results = (outs BoxType:$result);
  let assemblyFormat = "$property `from` $handle attr-dict";
}

def VPIPutOp : TalosOp<"vpi.put"> {
  let summary = "write, force, release, or deposit through VPI";
  let arguments = (ins
    VPIHandleType:$handle,
    I32Attr:$property,
    BoxType:$value,
    Optional<TimeType>:$delay
  );
  let assemblyFormat = [{
    $property `of` $handle `=` $value (`after` $delay^)? attr-dict
  }];
}

def VPICallbackOp : TalosOp<"vpi.callback"> {
  let summary = "register a callback at an explicit scheduler observation point";
  let arguments = (ins
    VPIHandleType:$handle,
    I32Attr:$reason,
    EventRegion:$region,
    FlatSymbolRefAttr:$callback
  );
  let assemblyFormat = [{
    $callback `for` $reason `on` $handle `in` $region attr-dict
  }];
}

def SystemCallOp : TalosOp<"system.call"> {
  let summary = "effectful built-in or PLI system task/function";
  let arguments = (ins StrAttr:$name, Variadic<AnyType>:$arguments);
  let results = (outs Variadic<AnyType>:$results);
  let assemblyFormat = [{
    $name `(` $arguments `)` attr-dict `:`
    functional-type($arguments, $results)
  }];
}

def DiagnosticOp : TalosOp<"diagnostic"> {
  let summary = "emit a simulator diagnostic";
  let arguments =
      (ins Severity:$severity, StrAttr:$message, Variadic<AnyType>:$arguments);
  let assemblyFormat = [{
    $severity $message `(` $arguments `)` attr-dict
    (`:` type($arguments)^)?
  }];
}

def FinishOp : TalosOp<"finish"> {
  let summary = "request orderly simulation termination";
  let arguments = (ins I32Attr:$verbosity);
  let assemblyFormat = "$verbosity attr-dict";
}

def StopOp : TalosOp<"stop"> {
  let summary = "request interactive simulation suspension";
  let arguments = (ins I32Attr:$verbosity);
  let assemblyFormat = "$verbosity attr-dict";
}

def TraceOp : TalosOp<"trace"> {
  let summary = "emit a timestamped trace value";
  let arguments =
      (ins StrAttr:$hierarchicalName, AnyType:$value, TimeType:$time);
  let assemblyFormat = [{
    $hierarchicalName `=` $value `at` $time attr-dict `:` type($value)
  }];
}

#endif // TALOS_DIALECT_TALOS_TALOSOPS_TD

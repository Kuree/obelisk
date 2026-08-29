//===- SDFAttrs.td - Exact normalized SDF attributes -------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_SDF_SDFATTRS_TD
#define OBELISK_DIALECT_SDF_SDFATTRS_TD

include "obelisk/Dialect/SDF/SDFDialect.td"
include "obelisk/Dialect/SDF/SDFEnums.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/BuiltinAttributes.td"

class SDFAttr<string name, string attrMnemonic, list<Trait> traits = []>
    : AttrDef<ObeliskSDFDialect, name, traits> {
  let mnemonic = attrMnemonic;
}

def SDFDecimalAttr : SDFAttr<"Decimal", "decimal"> {
  let summary = "one exact signed decimal spelling";
  let parameters = (ins "::mlir::StringAttr":$spelling);
  let assemblyFormat = "`<` $spelling `>`";
  let genVerifyDecl = 1;
}

def SDFTimeScaleAttr : SDFAttr<"TimeScale", "timescale"> {
  let summary = "exact SDF TIMESCALE amount and unit";
  let parameters = (ins SDFDecimalAttr:$amount,
      EnumParameter<SDFTimeUnit>:$unit);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SDFPortAttr : SDFAttr<"Port", "port"> {
  let summary = "one fixed SDF port specification";
  let parameters = (ins "::mlir::StringAttr":$name, "bool":$hasIndex,
      "int64_t":$index, EnumParameter<SDFEdge>:$edge);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SDFPortArrayAttr : TypedArrayAttrBase<SDFPortAttr, "SDF port array"> {
  let constBuilderCall = "$_builder.getArrayAttr($0)";
}

def SDFConditionTokenAttr : SDFAttr<"ConditionToken", "condition_token"> {
  let summary = "one operand or operator in a postfix SDF condition";
  let parameters = (ins EnumParameter<SDFConditionOpcode>:$opcode,
      OptionalParameter<"::obelisk::sdf::PortAttr">:$port,
      OptionalParameter<"::mlir::IntegerAttr">:$constant);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SDFConditionTokenArrayAttr
    : TypedArrayAttrBase<SDFConditionTokenAttr, "SDF condition-token array"> {
  let constBuilderCall = "$_builder.getArrayAttr($0)";
}

def SDFConditionAttr : SDFAttr<"Condition", "condition"> {
  let summary = "bounded typed postfix SDF conditional expression";
  let parameters = (ins "::mlir::ArrayAttr":$tokens, "bool":$inverted);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SDFTimingEventAttr : SDFAttr<"TimingEvent", "timing_event"> {
  let summary = "one timing-check port event and optional condition";
  let parameters = (ins SDFPortAttr:$port,
      OptionalParameter<"::obelisk::sdf::ConditionAttr">:$condition);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SDFTimingEventArrayAttr
    : TypedArrayAttrBase<SDFTimingEventAttr, "SDF timing-event array"> {
  let constBuilderCall = "$_builder.getArrayAttr($0)";
}

def SDFDelayValueAttr : SDFAttr<"DelayValue", "delay_value"> {
  let summary = "empty, scalar, or min:typ:max exact decimal delay value";
  let parameters = (ins EnumParameter<SDFDelayValueForm>:$form,
      OptionalParameter<"::obelisk::sdf::DecimalAttr">:$min,
      OptionalParameter<"::obelisk::sdf::DecimalAttr">:$typ,
      OptionalParameter<"::obelisk::sdf::DecimalAttr">:$max);
  let assemblyFormat = "`<` struct(params) `>`";
  let genVerifyDecl = 1;
}

def SDFDelayValueArrayAttr
    : TypedArrayAttrBase<SDFDelayValueAttr, "SDF delay-value array"> {
  let constBuilderCall = "$_builder.getArrayAttr($0)";
}

#endif // OBELISK_DIALECT_SDF_SDFATTRS_TD

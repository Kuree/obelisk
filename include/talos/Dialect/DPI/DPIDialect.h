//===- DPIDialect.h - SystemVerilog DPI-C boundary -------------*- C++ -*-===//

#ifndef TALOS_DIALECT_DPI_DPIDIALECT_H
#define TALOS_DIALECT_DPI_DPIDIALECT_H

#include "talos/Dialect/DiscardableAttributes.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Dialect.h"

#include "talos/Dialect/DPI/DPIDialect.h.inc"

namespace talos::dpi {

using Import = DPIDialect::ImportAttrHelper;
using Export = DPIDialect::ExportAttrHelper;
using ExportBridge = DPIDialect::ExportBridgeAttrHelper;
using ExportBody = DPIDialect::ExportBodyAttrHelper;
using Task = DPIDialect::TaskAttrHelper;
using Pure = DPIDialect::PureAttrHelper;
using Context = DPIDialect::ContextAttrHelper;
using ImportId = DPIDialect::ImportIdAttrHelper;
using ExportId = DPIDialect::ExportIdAttrHelper;
using CIdentifier = DPIDialect::CIdentifierAttrHelper;
using ScopeId = DPIDialect::ScopeIdAttrHelper;
using ABISignature = DPIDialect::AbiSignatureAttrHelper;
using AggregateLayouts = DPIDialect::AggregateLayoutsAttrHelper;
using LogicalInputs = DPIDialect::LogicalInputsAttrHelper;
using ElidedInputs = DPIDialect::ElidedInputsAttrHelper;
using ABIHash = DPIDialect::AbiHashAttrHelper;
using ExportBridgeSymbol = DPIDialect::ExportBridgeSymbolAttrHelper;
using ExportBodySymbol = DPIDialect::ExportBodySymbolAttrHelper;
using HasExports = DPIDialect::HasExportsAttrHelper;
using TransparentExports = DPIDialect::TransparentExportsAttrHelper;

using discardable::get;
using discardable::has;
using discardable::mark;
using discardable::named;
using discardable::remove;
using discardable::set;

/// Stable hash of a signature's ABI entries and logical input count. Native
/// thunks and the bytecode encoder agree on it without sharing IR.
uint64_t getSignatureHash(mlir::ArrayAttr signature, uint64_t logicalInputs);

} // namespace talos::dpi

#endif // TALOS_DIALECT_DPI_DPIDIALECT_H

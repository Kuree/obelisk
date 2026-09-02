//===- VPIObjectModelABI.cpp - generated VPI ABI checks ------------------===//

#include "VPIInternal.h"
#include "obelisk/Reflection/VPIObjectModel.h"

#define OBELISK_CHECK_VPI_VALUE(apiName, schemaValue)                          \
  static_assert(static_cast<uint32_t>(apiName) == uint32_t(schemaValue),       \
                #apiName " differs from the generated VPI schema");

OBELISK_FOR_EACH_VPI_OBJECT_KIND(OBELISK_CHECK_VPI_VALUE)
OBELISK_FOR_EACH_VPI_RELATION(OBELISK_CHECK_VPI_VALUE)

#undef OBELISK_CHECK_VPI_VALUE

using namespace obelisk::reflection;

static_assert(findVPIObjectKind(vpiModule)->role == VPIObjectRole::Concrete);
static_assert(findVPIObjectKind(vpiReturn) == nullptr);
static_assert(findVPIObjectSelector(vpiReturn)->role ==
              VPIObjectRole::RelationOnly);
static_assert(findVPIObjectSelector(vpiTypespec)->role ==
              VPIObjectRole::AbstractSelector);
static_assert(findVPIObjectSelector(vpiMemory)->role ==
              VPIObjectRole::CompatibilitySelector);
static_assert(findVPIObjectSelector(vpiFor)->families &
              vpiFamilyMask(VPIObjectFamily::Scope));
static_assert(findVPIObjectSelector(vpiModuleArray)->families &
              vpiFamilyMask(VPIObjectFamily::Array));
static_assert(!(findVPIObjectSelector(vpiModuleArray)->families &
                vpiFamilyMask(VPIObjectFamily::Scope)));

static_assert(findVPIRelation(vpiForInitStmt)->cardinality ==
              VPIRelationCardinality::OneOrMany);
static_assert(findVPIRelation(vpiForIncStmt)->cardinality ==
              VPIRelationCardinality::OneOrMany);
static_assert(findVPIRelation(vpiIndex)->cardinality ==
              VPIRelationCardinality::OneOrMany);
static_assert(findVPIRelation(vpiUse)->cardinality ==
              VPIRelationCardinality::OneOrMany);
static_assert(findVPIRelation(vpiInTerm)->cardinality ==
              VPIRelationCardinality::One);
static_assert(findVPIRelation(vpiOutTerm)->cardinality ==
              VPIRelationCardinality::One);
static_assert(findVPIRelation(vpiInterfaceDecl) == nullptr);

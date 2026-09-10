//===- DesignReflection.td - design database wire schema -----------------===//

class ReflectionEncoding<string cppName_, int width_> {
  string cppName = cppName_;
  int width = width_;
}

def Bytes8 : ReflectionEncoding<"Bytes", 8>;
def U16LE : ReflectionEncoding<"U16LE", 2>;
def U32LE : ReflectionEncoding<"U32LE", 4>;
def U64LE : ReflectionEncoding<"U64LE", 8>;
def I64LE : ReflectionEncoding<"I64LE", 8>;

class ReflectionField<string cppName_, int offset_,
                      ReflectionEncoding encoding_> {
  string cppName = cppName_;
  int offset = offset_;
  ReflectionEncoding encoding = encoding_;
}

class ReflectionLayout<string cppName_, int size_,
                       list<ReflectionField> fields_> {
  string cppName = cppName_;
  int size = size_;
  list<ReflectionField> fields = fields_;
}

class ReflectionRecordKind<string cppName_, int value_,
                           ReflectionLayout layout_, int packedWidth_ = 16> {
  string cppName = cppName_;
  int value = value_;
  ReflectionLayout layout = layout_;
  int packedWidth = packedWidth_;
}

class ReflectionTableKind<string cppName_, int value_, int packedWidth_ = 2> {
  string cppName = cppName_;
  int value = value_;
  int packedWidth = packedWidth_;
}

def ScopeTable : ReflectionTableKind<"Scope", 0>;
def ObjectTable : ReflectionTableKind<"Object", 1>;
def StatementTable : ReflectionTableKind<"Statement", 2>;
def StaticObjectTable : ReflectionTableKind<"StaticObject", 3>;

def HeaderMagic : ReflectionField<"Magic", 0, Bytes8>;
def HeaderVersion : ReflectionField<"Version", 8, U32LE>;
def HeaderReserved : ReflectionField<"Reserved", 12, U32LE>;
def HeaderProfile : ReflectionField<"Profile", 16, U32LE>;
def HeaderSize : ReflectionField<"HeaderSize", 20, U32LE>;
def HeaderImageSize : ReflectionField<"ImageSize", 24, U64LE>;
def HeaderChecksum : ReflectionField<"Checksum", 32, U64LE>;
def HeaderRoot : ReflectionField<"Root", 40, U64LE>;
def HeaderScopeOffset : ReflectionField<"ScopeOffset", 48, U64LE>;
def HeaderScopeCount : ReflectionField<"ScopeCount", 56, U64LE>;
def HeaderObjectOffset : ReflectionField<"ObjectOffset", 64, U64LE>;
def HeaderObjectCount : ReflectionField<"ObjectCount", 72, U64LE>;
def HeaderTypeOffset : ReflectionField<"TypeOffset", 80, U64LE>;
def HeaderTypeCount : ReflectionField<"TypeCount", 88, U64LE>;
def HeaderStringOffset : ReflectionField<"StringOffset", 96, U64LE>;
def HeaderStringSize : ReflectionField<"StringSize", 104, U64LE>;
def HeaderIndexOffset : ReflectionField<"IndexOffset", 112, U64LE>;
def HeaderIndexCount : ReflectionField<"IndexCount", 120, U64LE>;
def HeaderStatementOffset : ReflectionField<"StatementOffset", 128, U64LE>;
def HeaderStatementCount : ReflectionField<"StatementCount", 136, U64LE>;
def HeaderStatementSiteOffset
    : ReflectionField<"StatementSiteOffset", 144, U64LE>;
def HeaderStatementSiteCount
    : ReflectionField<"StatementSiteCount", 152, U64LE>;
def HeaderRelationOffset : ReflectionField<"RelationOffset", 160, U64LE>;
def HeaderRelationCount : ReflectionField<"RelationCount", 168, U64LE>;
def Header : ReflectionLayout<"Header", 176,
  [HeaderMagic, HeaderVersion, HeaderReserved, HeaderProfile, HeaderSize,
   HeaderImageSize, HeaderChecksum, HeaderRoot, HeaderScopeOffset,
   HeaderScopeCount, HeaderObjectOffset, HeaderObjectCount, HeaderTypeOffset,
   HeaderTypeCount, HeaderStringOffset, HeaderStringSize, HeaderIndexOffset,
   HeaderIndexCount, HeaderStatementOffset, HeaderStatementCount,
   HeaderStatementSiteOffset, HeaderStatementSiteCount, HeaderRelationOffset,
   HeaderRelationCount]>;

// The low half identifies the physical record layout. The high half carries
// the concrete VPI object kind intrinsically, including for objects with no
// outgoing relations. The packing helpers are generated from
// ReflectionRecordKind::packedWidth.
def ScopeKindField : ReflectionField<"KindAndVPIKind", 0, U32LE>;
def ScopeCaps : ReflectionField<"Caps", 4, U32LE>;
def ScopeID : ReflectionField<"ID", 8, U64LE>;
def ScopeParent : ReflectionField<"Parent", 16, U64LE>;
def ScopeFirstChild : ReflectionField<"FirstChild", 24, U64LE>;
def ScopeNextSibling : ReflectionField<"NextSibling", 32, U64LE>;
def ScopeName : ReflectionField<"Name", 40, U64LE>;
def ScopeSourceFile : ReflectionField<"SourceFile", 48, U64LE>;
def ScopeSourceLineColumn : ReflectionField<"SourceLineColumn", 56, U64LE>;
def Scope : ReflectionLayout<"Scope", 64,
  [ScopeKindField, ScopeCaps, ScopeID, ScopeParent, ScopeFirstChild,
   ScopeNextSibling, ScopeName, ScopeSourceFile, ScopeSourceLineColumn]>;

def ObjectKindField : ReflectionField<"KindAndVPIKind", 0, U32LE>;
def ObjectCaps : ReflectionField<"Caps", 4, U32LE>;
def ObjectID : ReflectionField<"ID", 8, U64LE>;
def ObjectOwner : ReflectionField<"Owner", 16, U64LE>;
def ObjectNextSibling : ReflectionField<"NextSibling", 24, U64LE>;
def ObjectSourceFile : ReflectionField<"SourceFile", 32, U64LE>;
def ObjectName : ReflectionField<"Name", 40, U64LE>;
def ObjectType : ReflectionField<"Type", 48, U64LE>;
def ObjectWidth : ReflectionField<"Width", 56, U64LE>;
def ObjectLeft : ReflectionField<"Left", 64, I64LE>;
def ObjectRight : ReflectionField<"Right", 72, I64LE>;
def ObjectStateOffset : ReflectionField<"StateOffset", 80, U64LE>;
def ObjectSourceLineColumn : ReflectionField<"SourceLineColumn", 88, U64LE>;
def Object : ReflectionLayout<"Object", 96,
  [ObjectKindField, ObjectCaps, ObjectID, ObjectOwner, ObjectNextSibling,
   ObjectSourceFile, ObjectName, ObjectType, ObjectWidth, ObjectLeft,
   ObjectRight, ObjectStateOffset, ObjectSourceLineColumn]>;

def TypeRecordKind : ReflectionField<"RecordKind", 0, U32LE>;
def TypeKindAndFlags : ReflectionField<"KindAndFlags", 4, U32LE>;
def TypeWidth : ReflectionField<"Width", 8, U64LE>;
def TypeLeft : ReflectionField<"Left", 16, I64LE>;
def TypeRight : ReflectionField<"Right", 24, I64LE>;
def TypeElement : ReflectionField<"Element", 32, U64LE>;
def TypeFirstChild : ReflectionField<"FirstChild", 40, U64LE>;
def TypeChildCount : ReflectionField<"ChildCount", 48, U64LE>;
def TypeOrdinal : ReflectionField<"Ordinal", 56, U64LE>;
def TypePackedOffset : ReflectionField<"PackedOffset", 64, U64LE>;
def TypeName : ReflectionField<"Name", 72, U64LE>;
def Type : ReflectionLayout<"Type", 80,
  [TypeRecordKind, TypeKindAndFlags, TypeWidth, TypeLeft, TypeRight,
   TypeElement, TypeFirstChild, TypeChildCount, TypeOrdinal, TypePackedOffset,
   TypeName]>;

def IndexHash : ReflectionField<"Hash", 0, U64LE>;
def IndexName : ReflectionField<"Name", 8, U64LE>;
def IndexRecord : ReflectionField<"Record", 16, U64LE>;
def Index : ReflectionLayout<"Index", 24,
  [IndexHash, IndexName, IndexRecord]>;

// Compact immutable source identities. Indices and string offsets are 32-bit
// offsets within their respective tables. UINT32_MAX denotes an absent owner
// for scope-owned statements and an absent parent.
// Executable tier/fragment/PC bindings deliberately live in a later side
// table and never alter these semantic identities.
def StatementID : ReflectionField<"ID", 0, U64LE>;
def StatementOwnerObjectIndex
    : ReflectionField<"OwnerObjectIndex", 8, U32LE>;
def StatementScopeIndex : ReflectionField<"ScopeIndex", 12, U32LE>;
def StatementParentIndex : ReflectionField<"ParentIndex", 16, U32LE>;
def StatementSourceFile : ReflectionField<"SourceFile", 20, U32LE>;
def StatementName : ReflectionField<"Name", 24, U32LE>;
def StatementSourceLine : ReflectionField<"SourceLine", 28, U32LE>;
def StatementSourceColumn : ReflectionField<"SourceColumn", 32, U32LE>;
def StatementVPIKind : ReflectionField<"VPIKind", 36, U16LE>;
def StatementFlags : ReflectionField<"Flags", 38, U16LE>;
def Statement : ReflectionLayout<"Statement", 40,
  [StatementID, StatementOwnerObjectIndex, StatementScopeIndex,
   StatementParentIndex, StatementSourceFile, StatementName,
   StatementSourceLine, StatementSourceColumn, StatementVPIKind,
   StatementFlags]>;

// Compact immutable source/semantic identities that have no executable
// storage. UINT32_MAX denotes an absent scope; zero denotes an absent source
// file or name, matching the existing relative-string convention. All
// object relationships and immutable non-universal properties remain in the
// shared relation/fixed-property tables, avoiding per-record pointer fields.
def StaticObjectID : ReflectionField<"ID", 0, U64LE>;
def StaticObjectScopeIndex : ReflectionField<"ScopeIndex", 8, U32LE>;
def StaticObjectSourceFile : ReflectionField<"SourceFile", 12, U32LE>;
def StaticObjectName : ReflectionField<"Name", 16, U32LE>;
def StaticObjectSourceLine : ReflectionField<"SourceLine", 20, U32LE>;
def StaticObjectSourceColumn : ReflectionField<"SourceColumn", 24, U32LE>;
def StaticObjectVPIKind : ReflectionField<"VPIKind", 28, U16LE>;
def StaticObjectFlags : ReflectionField<"Flags", 30, U16LE>;
def StaticObject : ReflectionLayout<"StaticObject", 32,
  [StaticObjectID, StaticObjectScopeIndex, StaticObjectSourceFile,
   StaticObjectName, StaticObjectSourceLine, StaticObjectSourceColumn,
   StaticObjectVPIKind, StaticObjectFlags]>;

def StatementSiteID : ReflectionField<"ID", 0, U64LE>;
def StatementSiteStatementIndex
    : ReflectionField<"StatementIndex", 8, U32LE>;
def StatementSitePhase : ReflectionField<"Phase", 12, U16LE>;
def StatementSiteFlags : ReflectionField<"Flags", 14, U16LE>;
def StatementSite : ReflectionLayout<"StatementSite", 16,
  [StatementSiteID, StatementSiteStatementIndex, StatementSitePhase,
   StatementSiteFlags]>;

// Compact VPI-traversal adjacency. Stable MLIR IDs are resolved to table
// indices during serialization. The source's exact VPI kind, access-mode bit,
// and two-bit table tag share SourceKindAndTable; the target table tag occupies
// the high bits of TargetIndexAndTable. Traversal legality is recovered from
// the generated VPI model instead of redundantly stored per design edge.
def RelationSourceIndex : ReflectionField<"SourceIndex", 0, U32LE>;
def RelationTargetIndexAndTable
    : ReflectionField<"TargetIndexAndTable", 4, U32LE>;
def RelationOrdinal : ReflectionField<"Ordinal", 8, U32LE>;
def RelationSelector : ReflectionField<"Selector", 12, U16LE>;
def RelationSourceKindAndTable
    : ReflectionField<"SourceKindAndTable", 14, U16LE>;
def Relation : ReflectionLayout<"Relation", 16,
  [RelationSourceIndex, RelationTargetIndexAndTable, RelationOrdinal,
   RelationSelector, RelationSourceKindAndTable]>;

// Exact source-semantic VPI types form a compact graph separate from the
// executable Type table.  The latter is also consumed by VCD and value-layout
// code and deliberately remains normalized and nonzero-width.  Semantic graph
// cursors are table indices; UINT32_MAX denotes an absent edge/object/string.
def SemanticTypeKindAndFlags : ReflectionField<"KindAndFlags", 0, U32LE>;
def SemanticTypeFirstEdge : ReflectionField<"FirstEdge", 4, U32LE>;
def SemanticTypeEdgeCount : ReflectionField<"EdgeCount", 8, U32LE>;
def SemanticTypeAliasObject
    : ReflectionField<"AliasObjectIndexAndTable", 12, U32LE>;
def SemanticTypeIdentityTarget
    : ReflectionField<"IdentityTargetIndexAndTable", 16, U32LE>;
def SemanticTypeName : ReflectionField<"Name", 20, U32LE>;
def SemanticTypeModport : ReflectionField<"Modport", 24, U32LE>;
def SemanticTypeQueueBound : ReflectionField<"QueueBound", 28, U32LE>;
def SemanticTypeLeft : ReflectionField<"Left", 32, I64LE>;
def SemanticTypeRight : ReflectionField<"Right", 40, I64LE>;
def SemanticTypeBitWidth : ReflectionField<"BitWidth", 48, U64LE>;
def SemanticTypeTagBits : ReflectionField<"TagBits", 56, U64LE>;
def SemanticType : ReflectionLayout<"SemanticType", 64,
  [SemanticTypeKindAndFlags, SemanticTypeFirstEdge, SemanticTypeEdgeCount,
   SemanticTypeAliasObject, SemanticTypeIdentityTarget, SemanticTypeName,
   SemanticTypeModport, SemanticTypeQueueBound, SemanticTypeLeft,
   SemanticTypeRight, SemanticTypeBitWidth, SemanticTypeTagBits]>;

def SemanticTypeEdgeChild : ReflectionField<"Child", 0, U32LE>;
def SemanticTypeEdgeRoleAndFlags
    : ReflectionField<"RoleAndFlags", 4, U32LE>;
def SemanticTypeEdgeOrdinal : ReflectionField<"Ordinal", 8, U32LE>;
def SemanticTypeEdgeName : ReflectionField<"Name", 12, U32LE>;
def SemanticTypeEdgePackedOffset
    : ReflectionField<"PackedOffset", 16, U64LE>;
def SemanticTypeEdge : ReflectionLayout<"SemanticTypeEdge", 24,
  [SemanticTypeEdgeChild, SemanticTypeEdgeRoleAndFlags,
   SemanticTypeEdgeOrdinal, SemanticTypeEdgeName,
   SemanticTypeEdgePackedOffset]>;

// Sparse bindings avoid a four-byte null slot on every physical or compact
// object that has no semantic type. ObjectIndexAndTable uses the same packed
// table/index representation as relation targets and is sorted for lookup.
def SemanticRootBindingObjectIndexAndTable
    : ReflectionField<"ObjectIndexAndTable", 0, U32LE>;
def SemanticRootBindingType : ReflectionField<"SemanticType", 4, U32LE>;
def SemanticRootBinding : ReflectionLayout<"SemanticRootBinding", 8,
  [SemanticRootBindingObjectIndexAndTable, SemanticRootBindingType]>;

// Immutable indexing geometry for relation-backed static arrays.  Records are
// sorted by object-table index; dense arrays reference declared ranges in
// left-to-right order, while sparse generate arrays reference exact source
// index keys.  Relation ordinals select the canonical object already stored in
// the ordinary VPI relation table, so no second object graph is materialized.
def RelationIndexObjectIndex
    : ReflectionField<"ObjectIndex", 0, U32LE>;
def RelationIndexFirstDimension
    : ReflectionField<"FirstDimension", 4, U32LE>;
def RelationIndexDimensionCount
    : ReflectionField<"DimensionCount", 8, U16LE>;
def RelationIndexFlags : ReflectionField<"Flags", 10, U16LE>;
def RelationIndexFirstKey : ReflectionField<"FirstKey", 12, U32LE>;
def RelationIndexFirstOrdinalKey
    : ReflectionField<"FirstOrdinalKey", 16, U32LE>;
def RelationIndex : ReflectionLayout<"RelationIndex", 20,
  [RelationIndexObjectIndex, RelationIndexFirstDimension,
   RelationIndexDimensionCount, RelationIndexFlags, RelationIndexFirstKey,
   RelationIndexFirstOrdinalKey]>;

def RelationIndexDimensionLeft : ReflectionField<"Left", 0, I64LE>;
def RelationIndexDimensionRight : ReflectionField<"Right", 8, I64LE>;
def RelationIndexDimension : ReflectionLayout<"RelationIndexDimension", 16,
  [RelationIndexDimensionLeft, RelationIndexDimensionRight]>;

def RelationIndexKeyIndex : ReflectionField<"Index", 0, I64LE>;
def RelationIndexKeyOrdinal : ReflectionField<"Ordinal", 8, U32LE>;
def RelationIndexKey : ReflectionLayout<"RelationIndexKey", 12,
  [RelationIndexKeyIndex, RelationIndexKeyOrdinal]>;

def RelationIndexMemberTarget
    : ReflectionField<"TargetIndexAndTable", 0, U32LE>;
def RelationIndexMemberRelationIndex
    : ReflectionField<"RelationIndex", 4, U32LE>;
def RelationIndexMemberOrdinal : ReflectionField<"Ordinal", 8, U32LE>;
def RelationIndexMember : ReflectionLayout<"RelationIndexMember", 12,
  [RelationIndexMemberTarget, RelationIndexMemberRelationIndex,
   RelationIndexMemberOrdinal]>;

// Sparse immutable VPI properties are keyed by the same compact physical
// table/index identity used by relations.  KindAndFlags stores the generated
// VPIPropertyValueKind in its low two bits; all remaining bits are reserved.
// String payloads are absolute image offsets into the canonical string pool.
def FixedPropertySource
    : ReflectionField<"SourceIndexAndTable", 0, U32LE>;
def FixedPropertySelector : ReflectionField<"Selector", 4, U16LE>;
def FixedPropertyKindAndFlags
    : ReflectionField<"KindAndFlags", 6, U16LE>;
def FixedPropertyPayload : ReflectionField<"Payload", 8, U64LE>;
def FixedProperty : ReflectionLayout<"FixedProperty", 16,
  [FixedPropertySource, FixedPropertySelector, FixedPropertyKindAndFlags,
   FixedPropertyPayload]>;

// Definition-invariant instance metadata is stored once per source design
// unit. Instance bindings use the universal packed table/index source key and
// are sorted by that key for allocation-free binary lookup. Relative string
// offsets are 32-bit offsets into the canonical image string pool.
def DefinitionVPIKind : ReflectionField<"VPIKind", 0, U16LE>;
def DefinitionFlags : ReflectionField<"Flags", 2, U16LE>;
def DefinitionName : ReflectionField<"Name", 4, U32LE>;
def DefinitionFile : ReflectionField<"File", 8, U32LE>;
def DefinitionLine : ReflectionField<"Line", 12, U32LE>;
def DefinitionFirstMember : ReflectionField<"FirstMember", 16, U32LE>;
def DefinitionMemberCount : ReflectionField<"MemberCount", 20, U32LE>;
def DefinitionFirstMemberRelation
    : ReflectionField<"FirstMemberRelation", 24, U32LE>;
def DefinitionMemberRelationCount
    : ReflectionField<"MemberRelationCount", 28, U32LE>;
def Definition : ReflectionLayout<"Definition", 32,
  [DefinitionVPIKind, DefinitionFlags, DefinitionName, DefinitionFile,
   DefinitionLine, DefinitionFirstMember, DefinitionMemberCount,
   DefinitionFirstMemberRelation, DefinitionMemberRelationCount]>;

def DefinitionBindingSource
    : ReflectionField<"SourceIndexAndTable", 0, U32LE>;
def DefinitionBindingDefinition
    : ReflectionField<"Definition", 4, U32LE>;
def DefinitionBindingSpecialization
    : ReflectionField<"Specialization", 8, U32LE>;
def DefinitionBindingFirstMemberEndpoint
    : ReflectionField<"FirstMemberEndpoint", 12, U32LE>;
def DefinitionBinding : ReflectionLayout<"DefinitionBinding", 16,
  [DefinitionBindingSource, DefinitionBindingDefinition,
   DefinitionBindingSpecialization, DefinitionBindingFirstMemberEndpoint]>;

// Definition-owned declarations are templates. A synthetic runtime cursor
// combines one of these member indices with an elaborated instance scope.
// Flags currently carry the exact low-byte vpiDirection value for IODecl.
def DefinitionMemberName : ReflectionField<"Name", 0, U32LE>;
def DefinitionMemberFile : ReflectionField<"File", 4, U32LE>;
def DefinitionMemberLine : ReflectionField<"Line", 8, U32LE>;
def DefinitionMemberColumn : ReflectionField<"Column", 12, U32LE>;
def DefinitionMemberVPIKind : ReflectionField<"VPIKind", 16, U16LE>;
def DefinitionMemberFlags : ReflectionField<"Flags", 18, U16LE>;
def DefinitionMember : ReflectionLayout<"DefinitionMember", 20,
  [DefinitionMemberName, DefinitionMemberFile, DefinitionMemberLine,
   DefinitionMemberColumn, DefinitionMemberVPIKind, DefinitionMemberFlags]>;

// One range per generated (definition, selector) adjacency group; the compact
// target list is shared by every instance and its position is the ordinal.
def DefinitionMemberRelationSelector
    : ReflectionField<"Selector", 0, U16LE>;
def DefinitionMemberRelationFlags
    : ReflectionField<"Flags", 2, U16LE>;
def DefinitionMemberRelationFirstTarget
    : ReflectionField<"FirstTarget", 4, U32LE>;
def DefinitionMemberRelationTargetCount
    : ReflectionField<"TargetCount", 8, U32LE>;
def DefinitionMemberRelation : ReflectionLayout<"DefinitionMemberRelation", 12,
  [DefinitionMemberRelationSelector, DefinitionMemberRelationFlags,
   DefinitionMemberRelationFirstTarget,
   DefinitionMemberRelationTargetCount]>;
def DefinitionMemberRelationTargetMember
    : ReflectionField<"Member", 0, U32LE>;
def DefinitionMemberRelationTarget
    : ReflectionLayout<"DefinitionMemberRelationTarget", 4,
      [DefinitionMemberRelationTargetMember]>;

// Effective member types are hash-consed as complete definition
// specializations. DefinitionBinding selects one specialization per instance.
def DefinitionSpecializationDefinition
    : ReflectionField<"Definition", 0, U32LE>;
def DefinitionSpecializationFirstBinding
    : ReflectionField<"FirstBinding", 4, U32LE>;
def DefinitionSpecializationBindingCount
    : ReflectionField<"BindingCount", 8, U32LE>;
def DefinitionSpecialization : ReflectionLayout<"DefinitionSpecialization", 12,
  [DefinitionSpecializationDefinition,
   DefinitionSpecializationFirstBinding,
   DefinitionSpecializationBindingCount]>;
def DefinitionSpecializationBindingMember
    : ReflectionField<"Member", 0, U32LE>;
def DefinitionSpecializationBindingSemanticType
    : ReflectionField<"SemanticType", 4, U32LE>;
def DefinitionSpecializationBinding
    : ReflectionLayout<"DefinitionSpecializationBinding", 8,
      [DefinitionSpecializationBindingMember,
       DefinitionSpecializationBindingSemanticType]>;

// One instance-specific expression endpoint per definition member. The row
// ordinal is implicit in DefinitionBinding.FirstMemberEndpoint, so the common
// direct case costs only one packed table/index word.
def DefinitionMemberEndpointTarget
    : ReflectionField<"TargetIndexAndTable", 0, U32LE>;
def DefinitionMemberEndpoint : ReflectionLayout<"DefinitionMemberEndpoint", 4,
  [DefinitionMemberEndpointTarget]>;

// Per-bit immutable resolved-net subtypes. Runs are sorted by object index and
// first bit, nonoverlapping, and maximally coalesced. Missing bits deliberately
// mean that the exact dominating subtype is unavailable.
def ResolvedNetRunObjectIndex : ReflectionField<"ObjectIndex", 0, U32LE>;
def ResolvedNetRunNetType : ReflectionField<"NetType", 4, U32LE>;
def ResolvedNetRunFirstBit : ReflectionField<"FirstBit", 8, U64LE>;
def ResolvedNetRunBitCount : ReflectionField<"BitCount", 16, U64LE>;
def ResolvedNetRun : ReflectionLayout<"ResolvedNetRun", 24,
  [ResolvedNetRunObjectIndex, ResolvedNetRunNetType,
   ResolvedNetRunFirstBit, ResolvedNetRunBitCount]>;

// Effective net delays use the same per-bit run representation as resolved
// net subtypes. Rise, fall, and third are scheduler ticks at the design-wide
// time precision; Third is the turn-off delay for ordinary nets and the charge
// decay time for trireg nets. Runs are sorted and maximally coalesced; missing
// bits have the default zero delay and therefore consume no image space.
def NetDelayRunObjectIndex : ReflectionField<"ObjectIndex", 0, U32LE>;
def NetDelayRunReserved : ReflectionField<"Reserved", 4, U32LE>;
def NetDelayRunFirstBit : ReflectionField<"FirstBit", 8, U64LE>;
def NetDelayRunBitCount : ReflectionField<"BitCount", 16, U64LE>;
def NetDelayRunRise : ReflectionField<"Rise", 24, I64LE>;
def NetDelayRunFall : ReflectionField<"Fall", 32, I64LE>;
def NetDelayRunThird : ReflectionField<"Third", 40, I64LE>;
def NetDelayRun : ReflectionLayout<"NetDelayRun", 48,
  [NetDelayRunObjectIndex, NetDelayRunReserved, NetDelayRunFirstBit,
   NetDelayRunBitCount, NetDelayRunRise, NetDelayRunFall, NetDelayRunThird]>;

// HeaderReserved is zero when no extension is present, otherwise it is the
// absolute 32-bit image offset of this directory. Keeping the fixed header
// stable lets future reflection producers add orthogonal generated sections
// without rebuilding every existing consumer image.
def SemanticDirectoryTypeOffset
    : ReflectionField<"SemanticTypeOffset", 0, U64LE>;
def SemanticDirectoryTypeCount
    : ReflectionField<"SemanticTypeCount", 8, U64LE>;
def SemanticDirectoryEdgeOffset
    : ReflectionField<"SemanticTypeEdgeOffset", 16, U64LE>;
def SemanticDirectoryEdgeCount
    : ReflectionField<"SemanticTypeEdgeCount", 24, U64LE>;
def SemanticDirectoryRootOffset
    : ReflectionField<"SemanticRootBindingOffset", 32, U64LE>;
def SemanticDirectoryRootCount
    : ReflectionField<"SemanticRootBindingCount", 40, U64LE>;
def SemanticDirectoryRelationIndexOffset
    : ReflectionField<"RelationIndexOffset", 48, U64LE>;
def SemanticDirectoryRelationIndexCount
    : ReflectionField<"RelationIndexCount", 56, U64LE>;
def SemanticDirectoryRelationIndexDimensionOffset
    : ReflectionField<"RelationIndexDimensionOffset", 64, U64LE>;
def SemanticDirectoryRelationIndexDimensionCount
    : ReflectionField<"RelationIndexDimensionCount", 72, U64LE>;
def SemanticDirectoryRelationIndexKeyOffset
    : ReflectionField<"RelationIndexKeyOffset", 80, U64LE>;
def SemanticDirectoryRelationIndexKeyCount
    : ReflectionField<"RelationIndexKeyCount", 88, U64LE>;
def SemanticDirectoryRelationIndexMemberOffset
    : ReflectionField<"RelationIndexMemberOffset", 96, U64LE>;
def SemanticDirectoryRelationIndexMemberCount
    : ReflectionField<"RelationIndexMemberCount", 104, U64LE>;
def SemanticDirectoryFixedPropertyOffset
    : ReflectionField<"FixedPropertyOffset", 112, U64LE>;
def SemanticDirectoryFixedPropertyCount
    : ReflectionField<"FixedPropertyCount", 120, U64LE>;
def SemanticDirectoryResolvedNetRunOffset
    : ReflectionField<"ResolvedNetRunOffset", 128, U64LE>;
def SemanticDirectoryResolvedNetRunCount
    : ReflectionField<"ResolvedNetRunCount", 136, U64LE>;
def SemanticDirectoryNetDelayRunOffset
    : ReflectionField<"NetDelayRunOffset", 144, U64LE>;
def SemanticDirectoryNetDelayRunCount
    : ReflectionField<"NetDelayRunCount", 152, U64LE>;
def SemanticDirectoryStaticObjectOffset
    : ReflectionField<"StaticObjectOffset", 160, U64LE>;
def SemanticDirectoryStaticObjectCount
    : ReflectionField<"StaticObjectCount", 168, U64LE>;
def SemanticDirectoryDefinitionOffset
    : ReflectionField<"DefinitionOffset", 176, U64LE>;
def SemanticDirectoryDefinitionCount
    : ReflectionField<"DefinitionCount", 184, U64LE>;
def SemanticDirectoryDefinitionBindingOffset
    : ReflectionField<"DefinitionBindingOffset", 192, U64LE>;
def SemanticDirectoryDefinitionBindingCount
    : ReflectionField<"DefinitionBindingCount", 200, U64LE>;
def SemanticDirectoryDefinitionMemberOffset
    : ReflectionField<"DefinitionMemberOffset", 208, U64LE>;
def SemanticDirectoryDefinitionMemberCount
    : ReflectionField<"DefinitionMemberCount", 216, U64LE>;
def SemanticDirectoryDefinitionMemberRelationOffset
    : ReflectionField<"DefinitionMemberRelationOffset", 224, U64LE>;
def SemanticDirectoryDefinitionMemberRelationCount
    : ReflectionField<"DefinitionMemberRelationCount", 232, U64LE>;
def SemanticDirectoryDefinitionMemberRelationTargetOffset
    : ReflectionField<"DefinitionMemberRelationTargetOffset", 240, U64LE>;
def SemanticDirectoryDefinitionMemberRelationTargetCount
    : ReflectionField<"DefinitionMemberRelationTargetCount", 248, U64LE>;
def SemanticDirectoryDefinitionSpecializationOffset
    : ReflectionField<"DefinitionSpecializationOffset", 256, U64LE>;
def SemanticDirectoryDefinitionSpecializationCount
    : ReflectionField<"DefinitionSpecializationCount", 264, U64LE>;
def SemanticDirectoryDefinitionSpecializationBindingOffset
    : ReflectionField<"DefinitionSpecializationBindingOffset", 272, U64LE>;
def SemanticDirectoryDefinitionSpecializationBindingCount
    : ReflectionField<"DefinitionSpecializationBindingCount", 280, U64LE>;
def SemanticDirectoryDefinitionMemberEndpointOffset
    : ReflectionField<"DefinitionMemberEndpointOffset", 288, U64LE>;
def SemanticDirectoryDefinitionMemberEndpointCount
    : ReflectionField<"DefinitionMemberEndpointCount", 296, U64LE>;
def SemanticDirectory : ReflectionLayout<"SemanticDirectory", 304,
  [SemanticDirectoryTypeOffset, SemanticDirectoryTypeCount,
   SemanticDirectoryEdgeOffset, SemanticDirectoryEdgeCount,
   SemanticDirectoryRootOffset, SemanticDirectoryRootCount,
   SemanticDirectoryRelationIndexOffset,
   SemanticDirectoryRelationIndexCount,
   SemanticDirectoryRelationIndexDimensionOffset,
   SemanticDirectoryRelationIndexDimensionCount,
   SemanticDirectoryRelationIndexKeyOffset,
   SemanticDirectoryRelationIndexKeyCount,
   SemanticDirectoryRelationIndexMemberOffset,
   SemanticDirectoryRelationIndexMemberCount,
   SemanticDirectoryFixedPropertyOffset,
   SemanticDirectoryFixedPropertyCount,
   SemanticDirectoryResolvedNetRunOffset,
   SemanticDirectoryResolvedNetRunCount,
   SemanticDirectoryNetDelayRunOffset,
   SemanticDirectoryNetDelayRunCount,
   SemanticDirectoryStaticObjectOffset,
   SemanticDirectoryStaticObjectCount,
   SemanticDirectoryDefinitionOffset,
   SemanticDirectoryDefinitionCount,
   SemanticDirectoryDefinitionBindingOffset,
   SemanticDirectoryDefinitionBindingCount,
   SemanticDirectoryDefinitionMemberOffset,
   SemanticDirectoryDefinitionMemberCount,
   SemanticDirectoryDefinitionMemberRelationOffset,
   SemanticDirectoryDefinitionMemberRelationCount,
   SemanticDirectoryDefinitionMemberRelationTargetOffset,
   SemanticDirectoryDefinitionMemberRelationTargetCount,
   SemanticDirectoryDefinitionSpecializationOffset,
   SemanticDirectoryDefinitionSpecializationCount,
   SemanticDirectoryDefinitionSpecializationBindingOffset,
   SemanticDirectoryDefinitionSpecializationBindingCount,
   SemanticDirectoryDefinitionMemberEndpointOffset,
   SemanticDirectoryDefinitionMemberEndpointCount]>;

def ScopeRecord : ReflectionRecordKind<"Scope", 1, Scope>;
def StorageRecord : ReflectionRecordKind<"Storage", 2, Object>;
def NetRecord : ReflectionRecordKind<"Net", 3, Object>;
def DriverRecord : ReflectionRecordKind<"Driver", 4, Object>;
def ProcessRecord : ReflectionRecordKind<"Process", 5, Object>;
def TypeRecord : ReflectionRecordKind<"Type", 6, Type>;
def FunctionRecord : ReflectionRecordKind<"Function", 7, Object>;
def PortRecord : ReflectionRecordKind<"Port", 8, Object>;
// Source-only VPI identities share the pointer-free object layout. Their
// state offset is always zero; any immutable value payload is represented by
// explicit metadata and relations rather than executable design state.
def StaticObjectRecord : ReflectionRecordKind<"StaticObject", 9, Object>;

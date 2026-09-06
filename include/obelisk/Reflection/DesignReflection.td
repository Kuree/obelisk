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
def SemanticTypeAliasObject : ReflectionField<"AliasObject", 12, U32LE>;
def SemanticTypeIdentityTarget
    : ReflectionField<"IdentityTarget", 16, U32LE>;
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

def ObjectSemanticRootType : ReflectionField<"SemanticType", 0, U32LE>;
def ObjectSemanticRoot : ReflectionLayout<"ObjectSemanticRoot", 4,
  [ObjectSemanticRootType]>;

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
    : ReflectionField<"ObjectSemanticRootOffset", 32, U64LE>;
def SemanticDirectoryRootCount
    : ReflectionField<"ObjectSemanticRootCount", 40, U64LE>;
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
def SemanticDirectory : ReflectionLayout<"SemanticDirectory", 144,
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
   SemanticDirectoryResolvedNetRunCount]>;

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

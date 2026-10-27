//===- BytecodeEncoding.h - Bytecode instruction ABI names -----*- C++ -*-===//
//
// Private compiler names for the single runtime bytecode ABI.
//
//===----------------------------------------------------------------------===//

#ifndef TALOS_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODEENCODING_H
#define TALOS_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODEENCODING_H

#include "talos/Runtime/Runtime.h"

#include <cstdint>

namespace talos::bytecode {

constexpr uint32_t kInvalidRegister = UINT32_MAX;

constexpr uint32_t kIntrinsicStringOutputFormat =
    TALOS_RT_INTRINSIC_V1_FORMAT;
constexpr uint32_t kIntrinsicDisplay = TALOS_RT_INTRINSIC_V1_DISPLAY;
constexpr uint32_t kIntrinsicFinish = TALOS_RT_INTRINSIC_V1_FINISH;
constexpr uint32_t kIntrinsicProgramExit = TALOS_RT_INTRINSIC_V1_PROGRAM_EXIT;
constexpr uint32_t kIntrinsicStop = TALOS_RT_INTRINSIC_V1_STOP;
constexpr uint32_t kIntrinsicFatal = TALOS_RT_INTRINSIC_V1_FATAL;
constexpr uint32_t kIntrinsicError = TALOS_RT_INTRINSIC_V1_ERROR;
constexpr uint32_t kIntrinsicTerminationRequested =
    TALOS_RT_INTRINSIC_V1_TERMINATION_REQUESTED;
constexpr uint32_t kIntrinsicProgramOwned =
    TALOS_RT_INTRINSIC_V1_PROGRAM_OWNED;
constexpr uint32_t kIntrinsicTimeNow = TALOS_RT_INTRINSIC_V1_TIME_NOW;
constexpr uint32_t kIntrinsicSampledRead = TALOS_RT_INTRINSIC_V1_SAMPLED_READ;
constexpr uint32_t kIntrinsicSampledHistory =
    TALOS_RT_INTRINSIC_V1_SAMPLED_HISTORY;
constexpr uint32_t kIntrinsicClockedSampleUpdate =
    TALOS_RT_INTRINSIC_V1_CLOCKED_SAMPLE_UPDATE;
constexpr uint32_t kIntrinsicClockedSampleRead =
    TALOS_RT_INTRINSIC_V1_CLOCKED_SAMPLE_READ;
constexpr uint32_t kIntrinsicTimeToReal = TALOS_RT_INTRINSIC_V1_TIME_TO_REAL;
constexpr uint32_t kIntrinsicTimeFromReal =
    TALOS_RT_INTRINSIC_V1_TIME_FROM_REAL;
constexpr uint32_t kIntrinsicRealFromInteger =
    TALOS_RT_INTRINSIC_V1_REAL_FROM_INTEGER;
constexpr uint32_t kIntrinsicRealToInteger =
    TALOS_RT_INTRINSIC_V1_REAL_TO_INTEGER;
constexpr uint32_t kIntrinsicCountBits = TALOS_RT_INTRINSIC_V1_COUNT_BITS;
constexpr uint32_t kIntrinsicClog2 = TALOS_RT_INTRINSIC_V1_CLOG2;
constexpr uint32_t kIntrinsicFileOpenMCD =
    TALOS_RT_INTRINSIC_V1_FILE_OPEN_MCD;
constexpr uint32_t kIntrinsicFileOpen = TALOS_RT_INTRINSIC_V1_FILE_OPEN;
constexpr uint32_t kIntrinsicFileOpenStringMCD =
    TALOS_RT_INTRINSIC_V1_FILE_OPEN_STRING_MCD;
constexpr uint32_t kIntrinsicFileOpenString =
    TALOS_RT_INTRINSIC_V1_FILE_OPEN_STRING;
constexpr uint32_t kIntrinsicFileGetlineString =
    TALOS_RT_INTRINSIC_V1_FILE_GETLINE_STRING;
constexpr uint32_t kIntrinsicFileScanField =
    TALOS_RT_INTRINSIC_V1_FILE_SCAN_FIELD;
constexpr uint32_t kIntrinsicFileScanDynamic =
    TALOS_RT_INTRINSIC_V1_FILE_SCAN_DYNAMIC;
constexpr uint32_t kIntrinsicScanDynamicValidate =
    TALOS_RT_INTRINSIC_V1_SCAN_DYNAMIC_VALIDATE;
constexpr uint32_t kIntrinsicFileReadMemToken =
    TALOS_RT_INTRINSIC_V1_FILE_READMEM_TOKEN;
constexpr uint32_t kIntrinsicFileErrorString =
    TALOS_RT_INTRINSIC_V1_FILE_ERROR_STRING;
constexpr uint32_t kIntrinsicTimeFormat = TALOS_RT_INTRINSIC_V1_TIME_FORMAT;
constexpr uint32_t kIntrinsicTimeScanScale =
    TALOS_RT_INTRINSIC_V1_TIME_SCAN_SCALE;
constexpr uint32_t kIntrinsicPlusargTest = TALOS_RT_INTRINSIC_V1_PLUSARG_TEST;
constexpr uint32_t kIntrinsicPlusargValue =
    TALOS_RT_INTRINSIC_V1_PLUSARG_VALUE;
constexpr uint32_t kIntrinsicPlusargScan = TALOS_RT_INTRINSIC_V1_PLUSARG_SCAN;
constexpr uint32_t kIntrinsicPlusargParseLogic =
    TALOS_RT_INTRINSIC_V1_PLUSARG_PARSE_LOGIC;
constexpr uint32_t kIntrinsicPlusargParseReal =
    TALOS_RT_INTRINSIC_V1_PLUSARG_PARSE_REAL;
constexpr uint32_t kIntrinsicSystem = TALOS_RT_INTRINSIC_V1_SYSTEM;
constexpr uint32_t kIntrinsicFileClose = TALOS_RT_INTRINSIC_V1_FILE_CLOSE;
constexpr uint32_t kIntrinsicFileFlush = TALOS_RT_INTRINSIC_V1_FILE_FLUSH;
constexpr uint32_t kIntrinsicFileGetc = TALOS_RT_INTRINSIC_V1_FILE_GETC;
constexpr uint32_t kIntrinsicFileUngetc = TALOS_RT_INTRINSIC_V1_FILE_UNGETC;
constexpr uint32_t kIntrinsicFileGetline = TALOS_RT_INTRINSIC_V1_FILE_GETLINE;
constexpr uint32_t kIntrinsicFileReadPacked =
    TALOS_RT_INTRINSIC_V1_FILE_READ_PACKED;
constexpr uint32_t kIntrinsicFileEof = TALOS_RT_INTRINSIC_V1_FILE_EOF;
constexpr uint32_t kIntrinsicFileSeek = TALOS_RT_INTRINSIC_V1_FILE_SEEK;
constexpr uint32_t kIntrinsicFileTell = TALOS_RT_INTRINSIC_V1_FILE_TELL;
constexpr uint32_t kIntrinsicFileRewind = TALOS_RT_INTRINSIC_V1_FILE_REWIND;
constexpr uint32_t kIntrinsicSpawn = TALOS_RT_INTRINSIC_V1_SPAWN;
constexpr uint32_t kIntrinsicProcessCurrent =
    TALOS_RT_INTRINSIC_V1_PROCESS_CURRENT;
constexpr uint32_t kIntrinsicProcessStatus =
    TALOS_RT_INTRINSIC_V1_PROCESS_STATUS;
constexpr uint32_t kIntrinsicProcessRandomGet =
    TALOS_RT_INTRINSIC_V1_PROCESS_RANDOM_GET;
constexpr uint32_t kIntrinsicProcessRandomSet =
    TALOS_RT_INTRINSIC_V1_PROCESS_RANDOM_SET;
constexpr uint32_t kIntrinsicNBA = TALOS_RT_INTRINSIC_V1_NBA;
constexpr uint32_t kIntrinsicStaticNBA = TALOS_RT_INTRINSIC_V1_STATIC_NBA;
constexpr uint32_t kIntrinsicClockingNBA = TALOS_RT_INTRINSIC_V1_CLOCKING_NBA;
constexpr uint32_t kIntrinsicInertialDriver =
    TALOS_RT_INTRINSIC_V1_INERTIAL_DRIVER;
constexpr uint32_t kIntrinsicInertialDriverStrengthPair =
    TALOS_RT_INTRINSIC_V1_INERTIAL_DRIVER_STRENGTH_PAIR;
constexpr uint32_t kIntrinsicInertialPathStrengthPair =
    TALOS_RT_INTRINSIC_V1_INERTIAL_PATH_STRENGTH_PAIR;
constexpr uint32_t kIntrinsicLogicCaseDifferenceMask =
    TALOS_RT_INTRINSIC_V1_LOGIC_CASE_DIFFERENCE_MASK;
constexpr uint32_t kIntrinsicInertialPathDriver =
    TALOS_RT_INTRINSIC_V1_INERTIAL_PATH_DRIVER;
constexpr uint32_t kIntrinsicInertialPathStorage =
    TALOS_RT_INTRINSIC_V1_INERTIAL_PATH_STORAGE;
constexpr uint32_t kIntrinsicEventTrigger =
    TALOS_RT_INTRINSIC_V1_EVENT_TRIGGER;
constexpr uint32_t kIntrinsicEventReplaceAfter =
    TALOS_RT_INTRINSIC_V1_EVENT_REPLACE_AFTER;
constexpr uint32_t kIntrinsicEventTriggered =
    TALOS_RT_INTRINSIC_V1_EVENT_TRIGGERED;
constexpr uint32_t kIntrinsicWaitOrderFailed =
    TALOS_RT_INTRINSIC_V1_WAIT_ORDER_FAILED;
constexpr uint32_t kIntrinsicClockOccurrenceConsume =
    TALOS_RT_INTRINSIC_V1_CLOCK_OCCURRENCE_CONSUME;
constexpr uint32_t kIntrinsicClockingOutputTrack =
    TALOS_RT_INTRINSIC_V1_CLOCKING_OUTPUT_TRACK;
constexpr uint32_t kIntrinsicClockingOutputCurrent =
    TALOS_RT_INTRINSIC_V1_CLOCKING_OUTPUT_CURRENT;
constexpr uint32_t kIntrinsicNoChangeUpdate =
    TALOS_RT_INTRINSIC_V1_NOCHANGE_UPDATE;
constexpr uint32_t kIntrinsicNetCountDrivers =
    TALOS_RT_INTRINSIC_V1_NET_COUNT_DRIVERS;
constexpr uint32_t kIntrinsicPassSwitchControl =
    TALOS_RT_INTRINSIC_V1_PASS_SWITCH_CONTROL;
constexpr uint32_t kIntrinsicPassSwitchControlDelayed =
    TALOS_RT_INTRINSIC_V1_PASS_SWITCH_CONTROL_DELAYED;
constexpr uint32_t kIntrinsicMosDriveDelayed =
    TALOS_RT_INTRINSIC_V1_MOS_DRIVE_DELAYED;
constexpr uint32_t kIntrinsicStateAlloc = TALOS_RT_INTRINSIC_V1_STATE_ALLOC;
constexpr uint32_t kIntrinsicStateAllocTyped =
    TALOS_RT_INTRINSIC_V1_STATE_ALLOC_TYPED;
constexpr uint32_t kIntrinsicDisableChildren =
    TALOS_RT_INTRINSIC_V1_DISABLE_CHILDREN;
constexpr uint32_t kIntrinsicControlEnter =
    TALOS_RT_INTRINSIC_V1_CONTROL_ENTER;
constexpr uint32_t kIntrinsicControlBoundary =
    TALOS_RT_INTRINSIC_V1_CONTROL_BOUNDARY;
constexpr uint32_t kIntrinsicControlLeave =
    TALOS_RT_INTRINSIC_V1_CONTROL_LEAVE;
constexpr uint32_t kIntrinsicControlDisable =
    TALOS_RT_INTRINSIC_V1_CONTROL_DISABLE;
constexpr uint32_t kIntrinsicControlEscapePending =
    TALOS_RT_INTRINSIC_V1_CONTROL_ESCAPE_PENDING;
constexpr uint32_t kIntrinsicStaticOnce = TALOS_RT_INTRINSIC_V1_STATIC_ONCE;
constexpr uint32_t kIntrinsicDeferredOnce =
    TALOS_RT_INTRINSIC_V1_DEFERRED_ONCE;
constexpr uint32_t kIntrinsicDeferredEnqueue =
    TALOS_RT_INTRINSIC_V1_DEFERRED_ENQUEUE;
constexpr uint32_t kIntrinsicDeferredMature =
    TALOS_RT_INTRINSIC_V1_DEFERRED_MATURE;
constexpr uint32_t kIntrinsicAssertionControl =
    TALOS_RT_INTRINSIC_V1_ASSERTION_CONTROL;
constexpr uint32_t kIntrinsicAssertionEnabled =
    TALOS_RT_INTRINSIC_V1_ASSERTION_ENABLED;
constexpr uint32_t kIntrinsicAssertionActionState =
    TALOS_RT_INTRINSIC_V1_ASSERTION_ACTION_STATE;
constexpr uint32_t kIntrinsicAssertionKillEpoch =
    TALOS_RT_INTRINSIC_V1_ASSERTION_KILL_EPOCH;
constexpr uint32_t kIntrinsicMonitorRegister =
    TALOS_RT_INTRINSIC_V1_MONITOR_REGISTER;
constexpr uint32_t kIntrinsicMonitorControl =
    TALOS_RT_INTRINSIC_V1_MONITOR_CONTROL;
constexpr uint32_t kIntrinsicMonitorCurrent =
    TALOS_RT_INTRINSIC_V1_MONITOR_CURRENT;
constexpr uint32_t kIntrinsicDumpOpen = TALOS_RT_INTRINSIC_V1_DUMP_OPEN;
constexpr uint32_t kIntrinsicDumpOpenString =
    TALOS_RT_INTRINSIC_V1_DUMP_OPEN_STRING;
constexpr uint32_t kIntrinsicDumpTimescale =
    TALOS_RT_INTRINSIC_V1_DUMP_TIMESCALE;
constexpr uint32_t kIntrinsicDumpVars = TALOS_RT_INTRINSIC_V1_DUMP_VARS;
constexpr uint32_t kIntrinsicDumpAll = TALOS_RT_INTRINSIC_V1_DUMP_ALL;
constexpr uint32_t kIntrinsicDumpControl = TALOS_RT_INTRINSIC_V1_DUMP_CONTROL;
constexpr uint32_t kIntrinsicDumpLimit = TALOS_RT_INTRINSIC_V1_DUMP_LIMIT;
constexpr uint32_t kIntrinsicDumpFlush = TALOS_RT_INTRINSIC_V1_DUMP_FLUSH;
constexpr uint32_t kIntrinsicDumpPorts = TALOS_RT_INTRINSIC_V1_DUMP_PORTS;
constexpr uint32_t kIntrinsicDumpPortsControl =
    TALOS_RT_INTRINSIC_V1_DUMP_PORTS_CONTROL;
constexpr uint32_t kIntrinsicImport = TALOS_RT_INTRINSIC_V1_IMPORT;
constexpr uint32_t kIntrinsicDPIImport = TALOS_RT_INTRINSIC_V1_DPI_IMPORT;
constexpr uint32_t kIntrinsicClassAlloc = TALOS_RT_INTRINSIC_V1_CLASS_ALLOC;
constexpr uint32_t kIntrinsicClassCopy = TALOS_RT_INTRINSIC_V1_CLASS_COPY;
constexpr uint32_t kIntrinsicClassIsInstance =
    TALOS_RT_INTRINSIC_V1_CLASS_IS_INSTANCE;
constexpr uint32_t kIntrinsicClassCast = TALOS_RT_INTRINSIC_V1_CLASS_CAST;
constexpr uint32_t kIntrinsicClassFieldRef =
    TALOS_RT_INTRINSIC_V1_CLASS_FIELD_REF;
constexpr uint32_t kIntrinsicManagedWatch =
    TALOS_RT_INTRINSIC_V1_MANAGED_WATCH;
constexpr uint32_t kIntrinsicManagedLoad = TALOS_RT_INTRINSIC_V1_MANAGED_LOAD;
constexpr uint32_t kIntrinsicManagedStore =
    TALOS_RT_INTRINSIC_V1_MANAGED_STORE;
constexpr uint32_t kIntrinsicManagedOverride =
    TALOS_RT_INTRINSIC_V1_MANAGED_OVERRIDE;
constexpr uint32_t kIntrinsicManagedReleaseOverride =
    TALOS_RT_INTRINSIC_V1_MANAGED_RELEASE_OVERRIDE;
constexpr uint32_t kIntrinsicManagedNBA = TALOS_RT_INTRINSIC_V1_MANAGED_NBA;
constexpr uint32_t kIntrinsicManagedNBAMasked =
    TALOS_RT_INTRINSIC_V1_MANAGED_NBA_MASKED;
constexpr uint32_t kIntrinsicWeakCreate = TALOS_RT_INTRINSIC_V1_WEAK_CREATE;
constexpr uint32_t kIntrinsicWeakGet = TALOS_RT_INTRINSIC_V1_WEAK_GET;
constexpr uint32_t kIntrinsicWeakClear = TALOS_RT_INTRINSIC_V1_WEAK_CLEAR;
constexpr uint32_t kIntrinsicGCSafepoint = TALOS_RT_INTRINSIC_V1_GC_SAFEPOINT;
constexpr uint32_t kIntrinsicClassID = TALOS_RT_INTRINSIC_V1_CLASS_ID;
constexpr uint32_t kIntrinsicCovergroupSetEnabled =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_SET_ENABLED;
constexpr uint32_t kIntrinsicCovergroupSetName =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_SET_NAME;
constexpr uint32_t kIntrinsicCovergroupSetIntegerOption =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_SET_INTEGER_OPTION;
constexpr uint32_t kIntrinsicCovergroupGetIntegerOption =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_GET_INTEGER_OPTION;
constexpr uint32_t kIntrinsicCovergroupGetStringOption =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_GET_STRING_OPTION;
constexpr uint32_t kIntrinsicCovergroupSetStringOption =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_SET_STRING_OPTION;
constexpr uint32_t kIntrinsicCovergroupSetTypeIntegerOption =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_SET_TYPE_INTEGER_OPTION;
constexpr uint32_t kIntrinsicCovergroupSetTypeStringOption =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_SET_TYPE_STRING_OPTION;
constexpr uint32_t kIntrinsicCovergroupOptionReference =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_OPTION_REFERENCE;
constexpr uint32_t kIntrinsicCovergroupTypeOptionReference =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_TYPE_OPTION_REFERENCE;
constexpr uint32_t kIntrinsicCovergroupSampleEnabled =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_SAMPLE_ENABLED;
constexpr uint32_t kIntrinsicCovergroupCreate =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_CREATE;
constexpr uint32_t kIntrinsicCovergroupPrepare =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_PREPARE;
constexpr uint32_t kIntrinsicCovergroupCandidates =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_CANDIDATES;
constexpr uint32_t kIntrinsicCovergroupBindWith =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_BIND_WITH;
constexpr uint32_t kIntrinsicCovergroupReleasePreparation =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_RELEASE_PREPARATION;
constexpr uint32_t kIntrinsicCovergroupFormalRead =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_FORMAL_READ;
constexpr uint32_t kIntrinsicCovergroupSample =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_SAMPLE;
constexpr uint32_t kIntrinsicCovergroupClockEventRegister =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_CLOCK_EVENT_REGISTER;
constexpr uint32_t kIntrinsicCovergroupBlockEventRegister =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_BLOCK_EVENT_REGISTER;
constexpr uint32_t kIntrinsicCovergroupBlockEventFire =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_BLOCK_EVENT_FIRE;
constexpr uint32_t kIntrinsicCovergroupInstanceQuery =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_INSTANCE_QUERY;
constexpr uint32_t kIntrinsicCovergroupTypeQuery =
    TALOS_RT_INTRINSIC_V1_COVERGROUP_TYPE_QUERY;
constexpr uint32_t kIntrinsicCoveragePointHit =
    TALOS_RT_INTRINSIC_V1_COVERAGE_POINT_HIT;
constexpr uint32_t kIntrinsicFunctionalCoverageGet =
    TALOS_RT_INTRINSIC_V1_FUNCTIONAL_COVERAGE_GET;
constexpr uint32_t kIntrinsicFunctionalCoverageSetDbName =
    TALOS_RT_INTRINSIC_V1_FUNCTIONAL_COVERAGE_SET_DB_NAME;
constexpr uint32_t kIntrinsicFunctionalCoverageLoadDb =
    TALOS_RT_INTRINSIC_V1_FUNCTIONAL_COVERAGE_LOAD_DB;
constexpr uint32_t kIntrinsicCoverageControlDefinition =
    TALOS_RT_INTRINSIC_V1_COVERAGE_CONTROL_DEFINITION;
constexpr uint32_t kIntrinsicCoverageControlInstance =
    TALOS_RT_INTRINSIC_V1_COVERAGE_CONTROL_INSTANCE;
constexpr uint32_t kIntrinsicCoverageQueryDefinition =
    TALOS_RT_INTRINSIC_V1_COVERAGE_QUERY_DEFINITION;
constexpr uint32_t kIntrinsicCoverageQueryInstance =
    TALOS_RT_INTRINSIC_V1_COVERAGE_QUERY_INSTANCE;
constexpr uint32_t kIntrinsicCoverageDatabaseSave =
    TALOS_RT_INTRINSIC_V1_COVERAGE_DATABASE_SAVE;
constexpr uint32_t kIntrinsicCoverageDatabaseMerge =
    TALOS_RT_INTRINSIC_V1_COVERAGE_DATABASE_MERGE;
constexpr uint32_t kIntrinsicArgumentRefFromRef =
    TALOS_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_REF;
constexpr uint32_t kIntrinsicArgumentRefFromManaged =
    TALOS_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_MANAGED;
constexpr uint32_t kIntrinsicArgumentRefLoad =
    TALOS_RT_INTRINSIC_V1_ARGUMENT_REF_LOAD;
constexpr uint32_t kIntrinsicArgumentRefStore =
    TALOS_RT_INTRINSIC_V1_ARGUMENT_REF_STORE;
constexpr uint32_t kIntrinsicManagedRootExtract =
    TALOS_RT_INTRINSIC_V1_MANAGED_ROOT_EXTRACT;
constexpr uint32_t kIntrinsicManagedCandidateRoot =
    TALOS_RT_INTRINSIC_V1_MANAGED_CANDIDATE_ROOT;
constexpr uint32_t kIntrinsicReferencePathIndex =
    TALOS_RT_INTRINSIC_V1_REFERENCE_PATH_INDEX;
constexpr uint32_t kIntrinsicReferencePathStringCharacter =
    TALOS_RT_INTRINSIC_V1_REFERENCE_PATH_STRING_CHARACTER;
constexpr uint32_t kIntrinsicReferencePathAggregateElement =
    TALOS_RT_INTRINSIC_V1_REFERENCE_PATH_AGGREGATE_ELEMENT;
constexpr uint32_t kIntrinsicContainerImportFixed =
    TALOS_RT_INTRINSIC_V1_CONTAINER_IMPORT_FIXED;
constexpr uint32_t kIntrinsicContainerExportFixed =
    TALOS_RT_INTRINSIC_V1_CONTAINER_EXPORT_FIXED;
constexpr uint32_t kIntrinsicContainerExportBitstream =
    TALOS_RT_INTRINSIC_V1_CONTAINER_EXPORT_BITSTREAM;
constexpr uint32_t kIntrinsicAggregateExportBitstream =
    TALOS_RT_INTRINSIC_V1_AGGREGATE_EXPORT_BITSTREAM;
constexpr uint32_t kIntrinsicAggregateImportBitstream =
    TALOS_RT_INTRINSIC_V1_AGGREGATE_IMPORT_BITSTREAM;
constexpr uint32_t kIntrinsicVPIFrameBind =
    TALOS_RT_INTRINSIC_V1_VPI_FRAME_BIND;
constexpr uint32_t kIntrinsicBoxBindType =
    TALOS_RT_INTRINSIC_V1_BOX_BIND_TYPE;
constexpr uint32_t kIntrinsicBoxIsType = TALOS_RT_INTRINSIC_V1_BOX_IS_TYPE;
constexpr uint32_t kIntrinsicArgumentRefFromPath =
    TALOS_RT_INTRINSIC_V1_ARGUMENT_REF_FROM_PATH;
constexpr uint32_t kIntrinsicStringLiteral =
    TALOS_RT_INTRINSIC_V1_STRING_LITERAL;
constexpr uint32_t kIntrinsicStringFromPacked =
    TALOS_RT_INTRINSIC_V1_STRING_FROM_PACKED;
constexpr uint32_t kIntrinsicStringToPacked =
    TALOS_RT_INTRINSIC_V1_STRING_TO_PACKED;
constexpr uint32_t kIntrinsicStringConcat =
    TALOS_RT_INTRINSIC_V1_STRING_CONCAT;
constexpr uint32_t kIntrinsicStringRepeat =
    TALOS_RT_INTRINSIC_V1_STRING_REPEAT;
constexpr uint32_t kIntrinsicStringLength =
    TALOS_RT_INTRINSIC_V1_STRING_LENGTH;
constexpr uint32_t kIntrinsicStringGetc = TALOS_RT_INTRINSIC_V1_STRING_GETC;
constexpr uint32_t kIntrinsicStringPutc = TALOS_RT_INTRINSIC_V1_STRING_PUTC;
constexpr uint32_t kIntrinsicStringSubstr =
    TALOS_RT_INTRINSIC_V1_STRING_SUBSTR;
constexpr uint32_t kIntrinsicStringCompare =
    TALOS_RT_INTRINSIC_V1_STRING_COMPARE;
constexpr uint32_t kIntrinsicStringCaseConvert =
    TALOS_RT_INTRINSIC_V1_STRING_CASE_CONVERT;
constexpr uint32_t kIntrinsicStringScanField =
    TALOS_RT_INTRINSIC_V1_STRING_SCAN_FIELD;
constexpr uint32_t kIntrinsicStringScanDynamic =
    TALOS_RT_INTRINSIC_V1_STRING_SCAN_DYNAMIC;
constexpr uint32_t kIntrinsicStringScanRaw =
    TALOS_RT_INTRINSIC_V1_STRING_SCAN_RAW;
constexpr uint32_t kIntrinsicFileScanRaw =
    TALOS_RT_INTRINSIC_V1_FILE_SCAN_RAW;
constexpr uint32_t kIntrinsicStringParseInteger =
    TALOS_RT_INTRINSIC_V1_STRING_PARSE_INTEGER;
constexpr uint32_t kIntrinsicStringParseLogic =
    TALOS_RT_INTRINSIC_V1_STRING_PARSE_LOGIC;
constexpr uint32_t kIntrinsicStringParseReal =
    TALOS_RT_INTRINSIC_V1_STRING_PARSE_REAL;
constexpr uint32_t kIntrinsicStringParseRealConstant =
    TALOS_RT_INTRINSIC_V1_STRING_PARSE_REAL_CONSTANT;
constexpr uint32_t kIntrinsicStringFormatInteger =
    TALOS_RT_INTRINSIC_V1_STRING_FORMAT_INTEGER;
constexpr uint32_t kIntrinsicStringFormatReal =
    TALOS_RT_INTRINSIC_V1_STRING_FORMAT_REAL;
constexpr uint32_t kIntrinsicContainerSize =
    TALOS_RT_INTRINSIC_V1_CONTAINER_SIZE;
constexpr uint32_t kIntrinsicContainerCreateLike =
    TALOS_RT_INTRINSIC_V1_CONTAINER_CREATE_LIKE;
constexpr uint32_t kIntrinsicContainerRead =
    TALOS_RT_INTRINSIC_V1_CONTAINER_READ;
constexpr uint32_t kIntrinsicContainerWrite =
    TALOS_RT_INTRINSIC_V1_CONTAINER_WRITE;
constexpr uint32_t kIntrinsicContainerCreate =
    TALOS_RT_INTRINSIC_V1_CONTAINER_CREATE;
constexpr uint32_t kIntrinsicContainerClone =
    TALOS_RT_INTRINSIC_V1_CONTAINER_CLONE;
constexpr uint32_t kIntrinsicContainerSwap =
    TALOS_RT_INTRINSIC_V1_CONTAINER_SWAP;
constexpr uint32_t kIntrinsicContainerDelete =
    TALOS_RT_INTRINSIC_V1_CONTAINER_DELETE;
constexpr uint32_t kIntrinsicRandomBounded =
    TALOS_RT_INTRINSIC_V1_RANDOM_BOUNDED;
constexpr uint32_t kIntrinsicRandomDistribution =
    TALOS_RT_INTRINSIC_V1_RANDOM_DISTRIBUTION;
constexpr uint32_t kIntrinsicStochasticQueue =
    TALOS_RT_INTRINSIC_V1_STOCHASTIC_QUEUE;
constexpr uint32_t kIntrinsicRandomSolve = TALOS_RT_INTRINSIC_V1_RANDOM_SOLVE;
constexpr uint32_t kIntrinsicRandomSolveState =
    TALOS_RT_INTRINSIC_V1_RANDOM_SOLVE_STATE;
constexpr uint32_t kIntrinsicRandomSolveWideState =
    TALOS_RT_INTRINSIC_V1_RANDOM_SOLVE_WIDE_STATE;
constexpr uint32_t kIntrinsicRandomRealToRank =
    TALOS_RT_INTRINSIC_V1_RANDOM_REAL_TO_RANK;
constexpr uint32_t kIntrinsicRandomRealFromRank =
    TALOS_RT_INTRINSIC_V1_RANDOM_REAL_FROM_RANK;
constexpr uint32_t kIntrinsicRandomCycleNext =
    TALOS_RT_INTRINSIC_V1_RANDOM_CYCLE_NEXT;
constexpr uint32_t kIntrinsicRandomCycleSample =
    TALOS_RT_INTRINSIC_V1_RANDOM_CYCLE_SAMPLE;
constexpr uint32_t kIntrinsicRandomNext = TALOS_RT_INTRINSIC_V1_RANDOM_NEXT;
constexpr uint32_t kIntrinsicRandomInitializationEnter =
    TALOS_RT_INTRINSIC_V1_RANDOM_INITIALIZATION_ENTER;
constexpr uint32_t kIntrinsicRandomInitializationLeave =
    TALOS_RT_INTRINSIC_V1_RANDOM_INITIALIZATION_LEAVE;
constexpr uint32_t kIntrinsicLegacyRandom =
    TALOS_RT_INTRINSIC_V1_RANDOM_LEGACY;
constexpr uint32_t kIntrinsicRandomSeed = TALOS_RT_INTRINSIC_V1_RANDOM_SEED;
constexpr uint32_t kIntrinsicRandomGetState =
    TALOS_RT_INTRINSIC_V1_RANDOM_GET_STATE;
constexpr uint32_t kIntrinsicRandomSetState =
    TALOS_RT_INTRINSIC_V1_RANDOM_SET_STATE;
constexpr uint32_t kIntrinsicQueueDelete = TALOS_RT_INTRINSIC_V1_QUEUE_DELETE;
constexpr uint32_t kIntrinsicQueueInsert = TALOS_RT_INTRINSIC_V1_QUEUE_INSERT;
constexpr uint32_t kIntrinsicMailboxCreate =
    TALOS_RT_INTRINSIC_V1_MAILBOX_CREATE;
constexpr uint32_t kIntrinsicMailboxNum = TALOS_RT_INTRINSIC_V1_MAILBOX_NUM;
constexpr uint32_t kIntrinsicMailboxTryPut =
    TALOS_RT_INTRINSIC_V1_MAILBOX_TRY_PUT;
constexpr uint32_t kIntrinsicMailboxTryPeek =
    TALOS_RT_INTRINSIC_V1_MAILBOX_TRY_PEEK;
constexpr uint32_t kIntrinsicMailboxTryGet =
    TALOS_RT_INTRINSIC_V1_MAILBOX_TRY_GET;
constexpr uint32_t kIntrinsicSemaphoreCreate =
    TALOS_RT_INTRINSIC_V1_SEMAPHORE_CREATE;
constexpr uint32_t kIntrinsicSemaphorePut =
    TALOS_RT_INTRINSIC_V1_SEMAPHORE_PUT;
constexpr uint32_t kIntrinsicSemaphoreTryGet =
    TALOS_RT_INTRINSIC_V1_SEMAPHORE_TRY_GET;
constexpr uint32_t kIntrinsicEventCreate = TALOS_RT_INTRINSIC_V1_EVENT_CREATE;
constexpr uint32_t kIntrinsicAssocCreate = TALOS_RT_INTRINSIC_V1_ASSOC_CREATE;
constexpr uint32_t kIntrinsicAssocRead = TALOS_RT_INTRINSIC_V1_ASSOC_READ;
constexpr uint32_t kIntrinsicAssocWrite = TALOS_RT_INTRINSIC_V1_ASSOC_WRITE;
constexpr uint32_t kIntrinsicAssocExists = TALOS_RT_INTRINSIC_V1_ASSOC_EXISTS;
constexpr uint32_t kIntrinsicAssocDelete = TALOS_RT_INTRINSIC_V1_ASSOC_DELETE;
constexpr uint32_t kIntrinsicAssocDefault =
    TALOS_RT_INTRINSIC_V1_ASSOC_DEFAULT;
constexpr uint32_t kIntrinsicAssocTraverse =
    TALOS_RT_INTRINSIC_V1_ASSOC_TRAVERSE;
constexpr uint32_t kIntrinsicReferencePathAssoc =
    TALOS_RT_INTRINSIC_V1_REFERENCE_PATH_ASSOC;

enum Opcode : uint16_t {
  Nop = TALOS_RT_DB_NOP,
  Constant = TALOS_RT_DB_CONSTANT,
  Move = TALOS_RT_DB_MOVE,
  Not = TALOS_RT_DB_NOT,
  And = TALOS_RT_DB_AND,
  Or = TALOS_RT_DB_OR,
  Xor = TALOS_RT_DB_XOR,
  Add = TALOS_RT_DB_ADD,
  Sub = TALOS_RT_DB_SUB,
  Mul = TALOS_RT_DB_MUL,
  UDiv = TALOS_RT_DB_UDIV,
  SDiv = TALOS_RT_DB_SDIV,
  URem = TALOS_RT_DB_UREM,
  SRem = TALOS_RT_DB_SREM,
  Shl = TALOS_RT_DB_SHL,
  LShr = TALOS_RT_DB_LSHR,
  AShr = TALOS_RT_DB_ASHR,
  Compare = TALOS_RT_DB_COMPARE,
  Select = TALOS_RT_DB_SELECT,
  Reduce = TALOS_RT_DB_REDUCE,
  Concat = TALOS_RT_DB_CONCAT,
  Extract = TALOS_RT_DB_EXTRACT,
  Insert = TALOS_RT_DB_INSERT,
  LoadFrame = TALOS_RT_DB_LOAD_FRAME,
  StoreFrame = TALOS_RT_DB_STORE_FRAME,
  MakeHandle = TALOS_RT_DB_MAKE_HANDLE,
  HandleOffset = TALOS_RT_DB_HANDLE_OFFSET,
  LoadState = TALOS_RT_DB_LOAD_STATE,
  StoreState = TALOS_RT_DB_STORE_STATE,
  Jump = TALOS_RT_DB_JUMP,
  Branch = TALOS_RT_DB_BRANCH,
  Call = TALOS_RT_DB_CALL,
  Return = TALOS_RT_DB_RETURN,
  Continue = TALOS_RT_DB_CONTINUE,
  Suspend = TALOS_RT_DB_SUSPEND,
  Terminate = TALOS_RT_DB_TERMINATE,
  Intrinsic = TALOS_RT_DB_INTRINSIC,
  Fail = TALOS_RT_DB_FAIL,
  MakeLocalHandle = TALOS_RT_DB_MAKE_LOCAL_HANDLE,
  HandleID = TALOS_RT_DB_HANDLE_ID,
  TaskCall = TALOS_RT_DB_TASK_CALL,
  VirtualCall = TALOS_RT_DB_VIRTUAL_CALL,
  ClearFrameRoot = TALOS_RT_DB_CLEAR_FRAME_ROOT,
  OverrideState = TALOS_RT_DB_OVERRIDE_STATE,
  ReleaseState = TALOS_RT_DB_RELEASE_STATE,
  FAdd = TALOS_RT_DB_FADD,
  FSub = TALOS_RT_DB_FSUB,
  FMul = TALOS_RT_DB_FMUL,
  FDiv = TALOS_RT_DB_FDIV,
  FNeg = TALOS_RT_DB_FNEG,
  FCompare = TALOS_RT_DB_FCOMPARE,
  FExt = TALOS_RT_DB_FEXT,
  FTrunc = TALOS_RT_DB_FTRUNC,
  FPow = TALOS_RT_DB_FPOW,
  VirtualTaskCall = TALOS_RT_DB_VIRTUAL_TASK_CALL,
  FrameRoot = TALOS_RT_DB_FRAME_ROOT,
  InterfaceCall = TALOS_RT_DB_INTERFACE_CALL,
  InterfaceTaskCall = TALOS_RT_DB_INTERFACE_TASK_CALL,
  ProcessControl = TALOS_RT_DB_PROCESS_CONTROL,
  Bitcast = TALOS_RT_DB_BITCAST,
  Power = TALOS_RT_DB_POWER,
  Replicate = TALOS_RT_DB_REPLICATE,
  PopCount = TALOS_RT_DB_POPCOUNT,
};

} // namespace talos::bytecode

#endif // TALOS_LIB_CONVERSION_SIMULATIONTOBYTECODE_BYTECODEENCODING_H

//===- AnalysisTestPasses.h - Analysis test passes ------------*- C++ -*-===//

#ifndef OBELISK_TEST_ANALYSIS_ANALYSISTESTPASSES_H
#define OBELISK_TEST_ANALYSIS_ANALYSISTESTPASSES_H

namespace obelisk {

void registerAccessFootprintAnalysisTestPass();
void registerActivationMemoryAnalysisTestPass();
void registerActivationPhaseAnalysisTestPass();
void registerBytecodeImageTestPass();
void registerClockInferenceAnalysisTestPass();
void registerClassDispatchAnalysisTestPass();
void registerCombinationalDependencyAnalysisTestPass();
void registerCoverageSchemaTestPass();
void registerManagedClassLayoutAnalysisTestPass();
void registerLocalStorageAnalysisTestPass();
void registerLocalUnknownAnalysisTestPass();
void registerNativeAOTAnalysisTestPass();
void registerNativeStateLayoutAnalysisTestPass();
void registerRandomProgramTestPass();
void registerSimulationProcessFrameAnalysisTestPass();
void registerSimulationScheduleAnalysisTestPass();
void registerSimulationVPIAnalysisTestPass();
void registerStaticSpecializationAnalysisTestPass();
void registerStateDomainTestPasses();
void registerStorageWriteAnalysisTestPass();
void registerModuleTestPasses();
void registerNativeBackendTestPipeline();
void registerValueInterfacesTestPass();

} // namespace obelisk

#endif // OBELISK_TEST_ANALYSIS_ANALYSISTESTPASSES_H

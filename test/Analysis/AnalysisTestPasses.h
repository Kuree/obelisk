//===- AnalysisTestPasses.h - Analysis test passes ------------*- C++ -*-===//

#ifndef TALOS_TEST_ANALYSIS_ANALYSISTESTPASSES_H
#define TALOS_TEST_ANALYSIS_ANALYSISTESTPASSES_H

namespace talos {

void registerAccessFootprintAnalysisTestPass();
void registerActivationMemoryAnalysisTestPass();
void registerActivationPhaseAnalysisTestPass();
void registerBytecodeImageTestPass();
void registerClockInferenceAnalysisTestPass();
void registerDPIEffectAnalysisTestPass();
void registerClassDispatchAnalysisTestPass();
void registerCombinationalDependencyAnalysisTestPass();
void registerCoverageSchemaTestPass();
void registerManagedClassLayoutAnalysisTestPass();
void registerLocalStorageAnalysisTestPass();
void registerLocalUnknownAnalysisTestPass();
void registerSteadyUnknownAnalysisTestPass();
void registerNativeAOTAnalysisTestPass();
void registerNativeSizeCensusTestPass();
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

} // namespace talos

#endif // TALOS_TEST_ANALYSIS_ANALYSISTESTPASSES_H

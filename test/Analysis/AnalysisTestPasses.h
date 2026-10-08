//===- AnalysisTestPasses.h - Analysis test passes ------------*- C++ -*-===//

#ifndef OBELISK_TEST_ANALYSIS_ANALYSISTESTPASSES_H
#define OBELISK_TEST_ANALYSIS_ANALYSISTESTPASSES_H

namespace obelisk {

void registerAccessFootprintAnalysisTestPass();
void registerActivationMemoryAnalysisTestPass();
void registerClockInferenceAnalysisTestPass();
void registerCombinationalDependencyAnalysisTestPass();
void registerManagedClassLayoutAnalysisTestPass();
void registerLocalStorageAnalysisTestPass();
void registerNativeAOTAnalysisTestPass();
void registerNativeStateLayoutAnalysisTestPass();
void registerSimulationProcessFrameAnalysisTestPass();
void registerSimulationScheduleAnalysisTestPass();
void registerSimulationVPIAnalysisTestPass();
void registerStaticSpecializationAnalysisTestPass();
void registerStateDomainTestPasses();
void registerStorageWriteAnalysisTestPass();
void registerValueInterfacesTestPass();

} // namespace obelisk

#endif // OBELISK_TEST_ANALYSIS_ANALYSISTESTPASSES_H

//===- AnalysisTestPasses.h - Analysis test passes ------------*- C++ -*-===//

#ifndef OBELISK_TEST_ANALYSIS_ANALYSISTESTPASSES_H
#define OBELISK_TEST_ANALYSIS_ANALYSISTESTPASSES_H

namespace obelisk {

void registerNativeAOTAnalysisTestPass();
void registerSimulationProcessFrameAnalysisTestPass();
void registerSimulationScheduleAnalysisTestPass();
void registerSimulationVPIAnalysisTestPass();
void registerStateDomainTestPasses();

} // namespace obelisk

#endif // OBELISK_TEST_ANALYSIS_ANALYSISTESTPASSES_H

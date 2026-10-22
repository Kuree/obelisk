// Host side of native-aot-dpi-context-import.sv: the context import calls
// the design's export, which reads no design state.
extern int get_period(void);
void start(void) {}
int scan(void) { return get_period() / 1000; }

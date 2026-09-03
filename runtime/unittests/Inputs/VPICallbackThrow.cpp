// A deliberately hostile VPI startup routine used to verify that the runtime
// contains exceptions at the C ABI boundary and releases its loader handle.
extern "C" void obelisk_vpi_callback_test_throw() { throw 42; }

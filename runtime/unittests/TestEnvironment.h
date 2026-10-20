// Portable environment mutation for runtime tests.
#ifndef OBELISK_UNITTESTS_TESTENVIRONMENT_H
#define OBELISK_UNITTESTS_TESTENVIRONMENT_H

#include <cstdlib>

inline int setTestEnvironment(const char *name, const char *value) {
#if defined(_WIN32)
  return _putenv_s(name, value);
#else
  return setenv(name, value, 1);
#endif
}

inline int unsetTestEnvironment(const char *name) {
#if defined(_WIN32)
  return _putenv_s(name, "");
#else
  return unsetenv(name);
#endif
}
#endif

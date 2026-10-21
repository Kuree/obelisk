// Object-file names of generated symbols, for asm labels in runtime tests.
// An asm label is the final symbol name, so it must carry the target's C
// symbol prefix: empty on ELF and COFF, '_' on Mach-O.
#ifndef OBELISK_UNITTESTS_GENERATEDSYMBOL_H
#define OBELISK_UNITTESTS_GENERATEDSYMBOL_H

#define OBELISK_TEST_STRINGIFY_(text) #text
#define OBELISK_TEST_STRINGIFY(text) OBELISK_TEST_STRINGIFY_(text)
#define OBELISK_TEST_SYMBOL(name)                                              \
  OBELISK_TEST_STRINGIFY(__USER_LABEL_PREFIX__) name

#endif // OBELISK_UNITTESTS_GENERATEDSYMBOL_H

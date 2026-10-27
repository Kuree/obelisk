// Object-file names of generated symbols, for asm labels in runtime tests.
// An asm label is the final symbol name, so it must carry the target's C
// symbol prefix: empty on ELF and COFF, '_' on Mach-O.
#ifndef TALOS_UNITTESTS_GENERATEDSYMBOL_H
#define TALOS_UNITTESTS_GENERATEDSYMBOL_H

#define TALOS_TEST_STRINGIFY_(text) #text
#define TALOS_TEST_STRINGIFY(text) TALOS_TEST_STRINGIFY_(text)
#define TALOS_TEST_SYMBOL(name)                                              \
  TALOS_TEST_STRINGIFY(__USER_LABEL_PREFIX__) name

#endif // TALOS_UNITTESTS_GENERATEDSYMBOL_H

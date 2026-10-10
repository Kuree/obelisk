#include <stdint.h>
#include <stdio.h>

extern void activation(void);
extern uint8_t read_state(uint64_t index);
extern void write_state(uint8_t value);
static uint8_t observed;

void observer(void) {
  observed = read_state(0);
  write_state(99);
}

int main(void) {
  activation();
  printf("observed=%u final=%u unrelated=%u\n", observed, read_state(0),
         read_state(4));
  return observed != 2 || read_state(0) != 100 || read_state(4) != 42;
}

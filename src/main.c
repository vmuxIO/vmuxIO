#include <stdio.h>

#include "libvfio-user.h"

int main() {
  printf("hello 0x%X\n", VFIO_DEVICE_STATE_V1_RESUMING);
  return 0;
}

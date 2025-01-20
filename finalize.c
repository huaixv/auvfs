#include "log.h"
#include "auv.h"
#include "stdio.h"
#include "stdlib.h"

int main() {

  log_set_level(LOG_DEBUG);

#define DIFF_ROOT "/usr"

  auv_finialize(DIFF_ROOT, "/mnt/newroot" DIFF_ROOT);

  return EXIT_SUCCESS;
}

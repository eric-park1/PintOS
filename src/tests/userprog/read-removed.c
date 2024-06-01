/* a check to make sure a removed file can still be read. */
#include <limits.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  // Create a new file
  char buf[20];

  int fd = create("temp.txt", 10);
  fd = open("temp.txt");
  CHECK(fd, "temp.txt");

  CHECK(remove("temp.txt"), "temp.txt");
  msg("Bytes read for removed file: %d", read(fd, buf, 10));
}

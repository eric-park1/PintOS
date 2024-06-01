/* a check to make sure a simple seek syscall is working properly.
   If a file is read beyond its file boundary, it reads 0. */
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

  seek(fd, 5);
  msg("Bytes read should be: %d", read(fd, buf, 10));

  seek(fd, 15);
  msg("Bytes read beyond boundary should be: %d", read(fd, buf, 5));
}

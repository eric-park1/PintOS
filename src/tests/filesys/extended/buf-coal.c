/* Test your buffer cache’s ability to write full blocks to disk without reading them first. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include "devices/block.h"
//#include "filesys/cache.h"

#define BLOCK_SIZE 512
#define BLOCK_COUNT 128

const char* file_name = "temp_file";
char buf[BLOCK_SIZE];

void test_main(void) {
  int fd;
  random_bytes(buf, sizeof buf);

  // Creating file
  msg("making %s", file_name);
  CHECK(create(file_name, 0), "creating %s", file_name);
  CHECK((fd = open(file_name)) > 1, "opening %s", file_name);

  int num_read_first = get_blocks_read();
  int num_write_first = get_blocks_write();
  //Writing into file
  for (int i = 0; i < BLOCK_COUNT; i++) {
    write(fd, buf, BLOCK_SIZE);
  }

  // reading file
  for (int i = 0; i < BLOCK_COUNT; i++) {
    read(fd, buf, BLOCK_SIZE);
  }

  // Counting number of writes & reads
  int num_read_second = get_blocks_read();
  int num_write_second = get_blocks_write();

  // msg("read: %d", num_read_second - num_read_first);
  // msg("wrote: %d", num_write_second- num_write_first);
  if ((num_write_second - num_write_first) % BLOCK_COUNT == 2) {
    msg("correct number of device writes");
  } else {
    msg("incorrect number of device writes");
  }

  close(fd);
  msg("closing %s", file_name);
}
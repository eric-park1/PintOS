/* Test the buffer cache’s effectiveness by measuring its cache hit rate. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define BLOCK_SIZE 512
#define BLOCK_COUNT 50

const char* file_name = "temp_file";
char buf[BLOCK_SIZE];

void test_main(void) {
  int fd;
  random_bytes(buf, sizeof buf);

  // creating file
  msg("making %s", file_name);
  CHECK(create(file_name, 0), "creating %s", file_name);
  CHECK((fd = open(file_name)) > 1, "opening %s", file_name);

  for (int i = 0; i < BLOCK_COUNT; i++) {
    write(fd, buf, BLOCK_SIZE);
  }

  close(fd);
  msg("closing %s", file_name);

  // resetting cache
  msg("clearing cache");
  cache_reset();

  // reading file
  CHECK((fd = open(file_name)) > 1, "opening %s", file_name);
  for (int i = 0; i < BLOCK_COUNT; i++) {
    read(fd, buf, BLOCK_SIZE);
  }

  close(fd);
  msg("closing %s", file_name);

  // calculating first cache rate
  int first_hits = get_cache_hit();
  int first_misses = get_cache_miss();
  int first_hit_rate = first_hits / (first_hits + first_misses);

  CHECK((fd = open(file_name)) > 1, "opening %s", file_name);
  // reading file
  for (int i = 0; i < BLOCK_COUNT; i++) {
    read(fd, buf, BLOCK_SIZE);
  }

  close(fd);
  msg("closing %s", file_name);

  // calculating second cache rate
  int second_hits = get_cache_hit();
  int second_misses = get_cache_miss();
  int second_hit_rate = second_hits / (second_hits + second_misses);

  // comparing cache hit rates
  if (first_hit_rate < second_hit_rate) {
    msg("New hit rate is not higher than old");
  } else {
    msg("New hit rate is higher than old");
  }
}
#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <stdbool.h>
#include "devices/block.h"
#include "filesys/off_t.h"
#include "threads/synch.h"

/* One cache entry. */
struct entry {
  bool valid;            /* Whether this entry is valid. */
  bool dirty;            /* Whether this entry is dirty. */
  block_sector_t sector; /* Disk sector this entry contains. Also the TAG. */
  bool r_bit;            /* Whether this entry has been recently used. For the clock algorithm. */
  uint8_t disk[BLOCK_SECTOR_SIZE]; /* Actual data. */
  struct lock entry_lock;          /* Lock to synchronize access to this entry. */
};

void cache_init(void);
void cache_flush(void);
void cache_write(block_sector_t sector, const void* buf);
void cache_read(block_sector_t sector, void* buf);
void cache_reset(void);
int get_cache_hit(void);
int get_cache_miss(void);

#endif /* filesys/cache.h */

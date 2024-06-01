#include <string.h>
#include "filesys/cache.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "devices/block.h"
#include "threads/malloc.h"

#define MAX_CACHE_CAPACITY 64

struct entry cache_array[MAX_CACHE_CAPACITY]; // Array of cache entries
unsigned clock_hand;                          // Position of clock hand in cache
size_t misses;
size_t hits;
struct lock cache_lock;

static struct entry* cache_access(void);

/* Initializes the cache. */
void cache_init(void) {
  // Initialize 64 cache entries w/ valid and dirty bit as false and initialize their corresponding locks
  struct entry* e;
  for (int i = 0; i < MAX_CACHE_CAPACITY; i++) {
    e = &cache_array[i];
    e->valid = false;
    e->dirty = false;
    lock_init(&e->entry_lock);
  }
  // Initialize clock_hand to start at the first position, and other data
  clock_hand = 0;
  misses = 0;
  hits = 0;
  lock_init(&cache_lock);
}

/* Iterate through cache and flush dirty blocks to disk. */
void cache_flush(void) {
  lock_acquire(&cache_lock);
  struct entry* e;
  for (int i = 0; i < MAX_CACHE_CAPACITY; i++) {
    e = &cache_array[i];
    lock_acquire(&e->entry_lock);
    if (e->valid && e->dirty) {
      block_write(fs_device, e->sector, e->disk);
      e->dirty = false;
    }
    lock_release(&e->entry_lock);
  }
  lock_release(&cache_lock);
}

/* Attempts to retrieve a free block in our cache and evicts via 
clock algorithm if full. */
static struct entry* cache_access(void) {
  /* Runs clock algorithm until we find a free block/one that we can evict */
  while (true) {
    struct entry* curr =
        &cache_array[clock_hand]; // Current block that the clock hand is pointing at
    if (!curr->valid) {           // Can return invalid blocks
      return curr;
    } else if (curr->r_bit) { // Set R bit to false but don't evict initially as per clock algorithm
      curr->r_bit = false;
    } else {             // Evict otherwise
      if (curr->dirty) { // Write to disk if we're evicting a dirty block
        block_write(fs_device, curr->sector, curr->disk);
        curr->dirty = false;
      }
      curr->valid = false;
      return curr;
    }
    // Increment clock hand or reset it if it reaches the last entry
    clock_hand = clock_hand == MAX_CACHE_CAPACITY ? clock_hand++ : 0;
  }
}

/* Writes BUF to the cache for the given SECTOR. */
void cache_write(block_sector_t sector, const void* buf) {
  lock_acquire(&cache_lock);
  struct entry* e = NULL;
  struct entry* curr;
  for (int i = 0; i < MAX_CACHE_CAPACITY; i++) {
    curr = &cache_array[i];
    if (curr->valid &&
        curr->sector ==
            sector) { // If the entry we're looking at is valid and it matches the given sector, then that's a hit
      hits++;
      e = curr;
      break;
    }
  }

  if (e == NULL) { // Dirty cause write, valid, and recently used
    misses++;
    e = cache_access();
    e->dirty = true;
    e->valid = true;
    e->sector = sector;
    e->r_bit = true;
    memcpy(e->disk, buf, BLOCK_SECTOR_SIZE);
  } else {
    e->r_bit = true;
    e->dirty = true;
    memcpy(e->disk, buf, BLOCK_SECTOR_SIZE);
  }
  lock_release(&cache_lock);
}

/* Reads bytes at disk SECTOR from cache into BUF. */
void cache_read(block_sector_t sector, void* buf) {
  lock_acquire(&cache_lock);
  // Attempt to look for block within cache
  struct entry* e = NULL;
  struct entry* curr;
  for (int i = 0; i < MAX_CACHE_CAPACITY; i++) {
    curr = &cache_array[i];
    if (curr->valid &&
        curr->sector ==
            sector) { // If the entry we're looking at is valid and it matches the given sector, then that's a hit
      hits++;
      e = curr;
      break;
    }
  }

  if (e ==
      NULL) { // Couldn't find block in cache so increment misses, otherwise set recently used bit to true and memcpy into buffer
    misses++;
    e = cache_access(); // Find free block
    e->dirty = false;
    e->valid = true;
    e->sector = sector;
    block_read(fs_device, sector, e->disk);
    e->r_bit = true;
    memcpy(buf, e->disk, BLOCK_SECTOR_SIZE);
  } else {
    e->r_bit = true;
    memcpy(buf, e->disk, BLOCK_SECTOR_SIZE);
  }
  lock_release(&cache_lock);
}

/* Resets the cache to its initial state. */
void cache_reset(void) {
  cache_flush();

  /* Acquire the main cache lock */
  lock_acquire(&cache_lock);
  clock_hand = 0;
  misses = 0;
  hits = 0;

  int i;
  for (i = 0; i < MAX_CACHE_CAPACITY; i++) {
    cache_array[i].valid = false;
  }
  /* Release the main cache lock */
  lock_release(&cache_lock);
}

/* Returns the number of cache hits for testing. */
int get_cache_hit(void) { return hits; }

/* Returns the number of cache misses for testing. */
int get_cache_miss(void) { return misses; }

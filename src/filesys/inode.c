#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/cache.h" // added for p1: buffer cache
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/thread.h" // added for project 3: subdirectories

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define MAX_FILE_SIZE (512 * 128 * 128)

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk {
  block_sector_t dbl_sect;
  off_t length;          /* File size in bytes. */
  bool isdir;            /* Whether this inode_disk represents a directory or a file. */
  block_sector_t parent; /* Block sector number of this inode_disk's parent directory. */
  unsigned magic;        /* Magic number. */
  uint32_t unused[123];
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors(off_t size) { return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE); }

/* In-memory inode. */
struct inode {
  struct list_elem elem;  /* Element in inode list. */
  block_sector_t sector;  /* Sector number of disk location. */
  off_t length;           /* File size in bytes. */
  int open_cnt;           /* Number of openers. */
  bool removed;           /* True if deleted, false otherwise. */
  int deny_write_cnt;     /* 0: writes ok, >0: deny writes. */
  struct inode_disk data; /* Inode content. */
  struct lock ilock;      /* Lock to synchronize access to this inode. */
};

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector(const struct inode* inode, off_t pos) {
  ASSERT(inode != NULL);

  if (pos <= inode->length) {
    // Get offset within our doubly indirect block of desired POS.
    off_t indirect_offset = pos / (BLOCK_SECTOR_SIZE * 128);
    // Get offset within our indirect block of desired POS.
    off_t direct_offset = (pos % (BLOCK_SECTOR_SIZE * 128)) / BLOCK_SECTOR_SIZE;

    // Get the doubly indirect block
    struct inode_disk id;
    block_sector_t dbl_block[128];
    cache_read(inode->data.dbl_sect, dbl_block);

    // Get block sect of desired indirect block
    block_sector_t indir_sect = dbl_block[indirect_offset];

    // Get the indirect block.
    block_sector_t indir_block[128];
    cache_read(indir_sect, indir_block);

    // Get block sect of desired POS from the indirect block
    block_sector_t data_sect = indir_block[direct_offset];
    return data_sect;
  } else {
    return -1;
  }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void inode_init(void) { list_init(&open_inodes); }

/* Resizes a file given its inode_disk ID and a new size SIZE.
  No change if the new size SIZE cannot be allocated. */
bool inode_resize(struct inode_disk* id, off_t size) {
  ASSERT(id != NULL);
  ASSERT(size <= MAX_FILE_SIZE);
  bool success;

  /* Edge case: don't need to resize. */
  if (id->dbl_sect == 0 && size == 0) {
    // id->length = size;
    return true;
  }

  // Allocate doubly indirect block if it has not been allocated.
  block_sector_t dbl_block[128];
  if (id->dbl_sect == 0) {
    if (!free_map_allocate(1, &id->dbl_sect)) {
      return false;
    }
  }
  // Get the doubly indirect block.
  cache_read(id->dbl_sect, dbl_block);

  // Iterate through indirect sects in doubly indirect block.
  for (int i = 0; i < 128; i++) {
    block_sector_t indir_sect = dbl_block[i];
    off_t indir_block_base = i * 128 * BLOCK_SECTOR_SIZE;
    // Grow: allocate new indirect block if needed
    if (size > indir_block_base && indir_sect == 0) {
      if (!free_map_allocate(1, &indir_sect)) {
        inode_resize(id, id->length);
        return false;
      }
      dbl_block[i] = indir_sect;
    }
    if (dbl_block[i] != 0) {
      // Get the indirect block.
      block_sector_t indir_block[128];
      memset(indir_block, 0, BLOCK_SECTOR_SIZE);
      cache_read(dbl_block[i], indir_block);

      /* Iterate through direct sects in indirect block. */
      for (int j = 0; j < 128; j++) {
        if (size <= (indir_block_base + j * BLOCK_SECTOR_SIZE) && indir_block[j] != 0) {
          // Shrink: release direct block if needed
          free_map_release(indir_block[j], 1);
          indir_block[j] = 0;
        } else if (size > (indir_block_base + j * BLOCK_SECTOR_SIZE) && indir_block[j] == 0) {
          // Grow: allocate direct block if needed
          block_sector_t dir_sect = indir_block[j];
          if (!free_map_allocate(1, &dir_sect)) {
            inode_resize(id, id->length);
            return false;
          }
          indir_block[j] = dir_sect;
        }
      }
      // Shrink: release indirect block if needed.
      if (size <= indir_block_base && indir_sect != 0) {
        /* At this point, all data blocks within this indirect block 
          have been released in the previous inner for loop,
          so we don't need to release them here.
        */
        free_map_release(dbl_block[i], 1);
        dbl_block[i] = 0;
      }
      cache_write(indir_sect, indir_block);
    }
  }
  cache_write(id->dbl_sect, dbl_block);
  id->length = size;
  return true;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length, bool directory) {
  struct inode_disk* disk_inode = NULL;
  bool success = false;
  ASSERT(length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc(1, sizeof *disk_inode);
  if (disk_inode != NULL) {
    /* Set disk_inode fields. */
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;
    disk_inode->parent = sector; // Sector that created it is its parent directory
    disk_inode->isdir = directory;

    /* Allocate pointers for this disk inode. */
    if (sector == FREE_MAP_SECTOR) {
      /* If it's the free map sector, set inode_disk, dbl_sect. */
      if (free_map_allocate(1, &disk_inode->dbl_sect)) {
        cache_write(sector, disk_inode);
        static char zeros[BLOCK_SECTOR_SIZE];
        cache_write(disk_inode->dbl_sect, zeros);
        success = true;
      }
    } else if (length == 0) {
      /* If the length is 0, don't allocate any pointers and write the disk inode. */
      cache_write(sector, disk_inode);
      success = true;
    } else if (length != 0) {
      /* If the length is not 0, allocate pointers accordingly. */
      static char zeros[BLOCK_SECTOR_SIZE];
      if (free_map_allocate(1, &disk_inode->dbl_sect)) { // set dbl_sect
        block_sector_t dbl_block[128];                   // create dbl_block
        memset(dbl_block, 0, BLOCK_SECTOR_SIZE);
        /* Populate double block. */
        bool outer_success = true;
        bool inner_success = true;
        for (int i = 0; i < 128; i++) {
          if (!inner_success) {
            outer_success = false;
            break;
          }
          off_t indir_block_base = i * 128 * BLOCK_SECTOR_SIZE;
          if (indir_block_base < length) {
            if (!free_map_allocate(1, &dbl_block[i])) { // set indirect sect
              outer_success = false;
              break;
            }
            block_sector_t indir_block[128]; // create indirect block
            memset(indir_block, 0, BLOCK_SECTOR_SIZE);
            /* Populate indirect block. */
            for (int j = 0; j < 128; j++) {
              if (indir_block_base + j * BLOCK_SECTOR_SIZE < length) {
                if (!free_map_allocate(1, &indir_block[j])) {
                  inner_success = false;
                  break;
                }
                cache_write(indir_block[j], zeros); // write out zeroed-out data block
              } else {
                break;
              }
            }
            cache_write(dbl_block[i], indir_block); // write out indirect block
          } else {
            break;
          }
        }
        /* If failed to allocate for the given LENGTH, revert to original state. */
        if (!outer_success) {
          int i = 0;
          while (dbl_block[i] != 0) {
            block_sector_t indir_block[128];
            cache_read(dbl_block[i], indir_block);
            int j = 0;
            while (indir_block[j] != 0) {
              free_map_release(indir_block[j], 1);
              j++;
            }
            free_map_release(dbl_block[i], 1);
          }
          free_map_release(disk_inode->dbl_sect, 1);
        } else {
          cache_write(disk_inode->dbl_sect, dbl_block); // write out doubly block
          disk_inode->length = length;
          cache_write(sector, disk_inode); // write out disk inode
          success = true;
        }
      }
    }
  }
  free(disk_inode);
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode* inode_open(block_sector_t sector) {
  struct list_elem* e;
  struct inode* inode;

  /* Check whether this inode is already open. */
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e)) {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector) {
      inode_reopen(inode);
      return inode;
    }
  }

  /* Allocate memory. */
  inode = malloc(sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front(&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->ilock);
  cache_read(inode->sector, &inode->data);
  inode->length = inode->data.length;
  return inode;
}

/* Reopens and returns INODE. */
struct inode* inode_reopen(struct inode* inode) {
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber(const struct inode* inode) { return inode->sector; }

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode* inode) {
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0) {
    /* Remove from inode list and release lock. */
    list_remove(&inode->elem);

    /* Deallocate blocks if removed. */
    if (inode->removed) {
      free_map_release(inode->sector, 1);
      inode_free(inode);
    }

    free(inode);
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove(struct inode* inode) {
  ASSERT(inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode* inode, void* buffer_, off_t size, off_t offset) {
  uint8_t* buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t* bounce = NULL;

  /* Edge case: reading the free_map. */
  if (inode->sector == FREE_MAP_SECTOR) {
    cache_read(inode->data.dbl_sect, buffer);
    return 512;
  }

  while (size > 0) {
    /* Disk sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Read full sector directly into caller's buffer. */
      cache_read(sector_idx, buffer + bytes_read);
    } else {
      /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
      if (bounce == NULL) {
        bounce = malloc(BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
          break;
      }
      // block_read(fs_device, sector_idx, bounce);
      cache_read(sector_idx, bounce);
      memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }
  free(bounce);

  return bytes_read;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an end of file is reached. */

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t inode_write_at(struct inode* inode, const void* buffer_, off_t size, off_t offset) {
  /* Edge case: cannot write to this inode. */
  if (inode->deny_write_cnt)
    return 0;

  /* Edge case: writing to the free map. */
  if (inode->sector == FREE_MAP_SECTOR) {
    block_sector_t free_map_sect = inode->data.dbl_sect;
    const void* bits = buffer_;
    cache_write(free_map_sect, bits);
    return 512;
  }

  const uint8_t* buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t* bounce = NULL;
  struct inode_disk id;

  cache_read(inode->sector, &id); // retrieve inode disk of this inode

  /* Resize if new length greater than current length. */
  if (offset + size > inode->length) {
    // If failed to resize, return
    if (!inode_resize(&id, offset + size)) {
      return 0;
    }
    // Otherwise, set new length and write out new inode_disk.
    inode->length = id.length;
    cache_write(inode->sector, &id);
  }
  inode->data = id;

  while (size > 0) {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset); // first sector to write to
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Write full sector directly to disk. */
      cache_write(sector_idx, buffer + bytes_written);
    } else {
      /* We need a bounce buffer. */
      if (bounce == NULL) {
        bounce = malloc(BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
          break;
      }

      /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
      if (sector_ofs > 0 || chunk_size < sector_left)
        cache_read(sector_idx, bounce);
      else
        memset(bounce, 0, BLOCK_SECTOR_SIZE);
      memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
      cache_write(sector_idx, bounce);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }
  free(bounce);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write(struct inode* inode) {
  inode->deny_write_cnt++;
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode* inode) {
  ASSERT(inode->deny_write_cnt > 0);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode* inode) { return inode->length; }

/* Returns whether INODE represents a directory. */
bool inode_directory(const struct inode* inode) {
  struct inode_disk* disk_inode = &inode->data;
  bool isdir = disk_inode->isdir;
  return isdir;
}

/* Returns opens INODE's parent directory's inode. */
struct inode* inode_parent(struct inode* inode) {
  return inode->data.parent;
}

/* Sets this inode's parent to the given PARENT. */
void inode_ps(struct inode* d, const struct inode* parent) { d->data.parent = parent->sector; }

/* Returns whether INODE is removed. */
bool inode_removed(struct inode* inode) { return inode->removed; }

/* Deallocates INODE's pointers. */
static bool inode_free(struct inode* inode) {
  struct inode_disk inode_d = inode->data;
  block_sector_t dbl_block[128];
  /* Read in the doubly indirect block. */
  cache_read(inode_d.dbl_sect, dbl_block);
  off_t i;
  for (int i = 0; i < 128; i++) {
    block_sector_t indir_sect = dbl_block[i];
    off_t indir_block_base = i * 128 * BLOCK_SECTOR_SIZE;
    if (indir_sect != 0) {
      /* Read in the indirect block. */
      block_sector_t indir_block[128];
      memset(indir_block, 0, BLOCK_SECTOR_SIZE);
      cache_read(dbl_block[i], indir_block);
      static char zeros[BLOCK_SECTOR_SIZE];
      // Release direct blocks.
      for (int j = 0; j < 128; j++) {
        if (indir_block[j] != 0) {
          free_map_release(indir_block[j], 1);
          indir_block[j] = 0;
        }
      }
      // Release indirect block.
      free_map_release(indir_sect, 1);
    }
  }
  // Release the double block.
  free_map_release(inode_d.dbl_sect, 1);
  // Release the inode_disk block.
  free_map_release(inode->sector, 1);
  return true;
}

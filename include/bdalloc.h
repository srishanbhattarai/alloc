#include <stdint.h>

#ifndef _BD_ALLOC_H
#define _BD_ALLOC_H 1
#endif

// Up to 4GB of memory
#define _BD_MAX_MEM_ORDER 32

/* Inform C++, that we are in C land */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct freelist_t {
  struct freelist_t *prev;
  struct freelist_t *next;
} freelist_t;

typedef struct bdalloc_t {
  void *arena;
  uint64_t size;

  // 256B overhead
  freelist_t *blocks[_BD_MAX_MEM_ORDER];
} bdalloc_t;

/* Initialize the allocator with the size. Returns a pointer to the inner arena
 */
extern void *bdalloc_init(bdalloc_t *allocator, uint64_t size);

/* Allocate SIZE bytes of memory */
extern void *bdalloc(bdalloc_t *allocator, uint64_t size);

/* Free a block allocated by `bdalloc`. This only returns the block to the
 * allocator */
extern void bdalloc_free(bdalloc_t *allocator, void *block);

/* Deinitialize the allocator which frees the inner arena */
extern void bdalloc_deinit(bdalloc_t *allocator);

#ifdef __cplusplus
}
#endif

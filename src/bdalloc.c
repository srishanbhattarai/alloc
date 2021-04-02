#include <assert.h>
#include <bdalloc.h>
#include <stdio.h>

// memset
#include <string.h>

// malloc
#include <stdlib.h>

/**
 * A minimum of 16 bytes must be allocated.
 */
#define _BD_MIN_ALLOC_ORDER 4
#define _BD_MIN_ALLOC_SIZE (1 << _BD_MIN_ALLOC_ORDER)

// remove the head node from the given list, fixing up the prev/next pointers
static freelist_t *freelist_detach(freelist_t **list) {
  assert(list != NULL);

  freelist_t *head = *list;
  if (head->next)
    head->next->prev = head->prev;
  if (head->prev)
    head->prev->next = head->next;

  *list = head->next;

  return head;
}

// splice off this node from the freelist it is in
static void freelist_splice(freelist_t *node) {
  if (node->prev)
    node->prev->next = node->next;
  if (node->next)
    node->next->prev = node->prev;

  node->prev = NULL;
  node->next = NULL;
}

// attach entry to the front of the list, fixing up the prev/next pointers
static void freelist_attach(freelist_t *list, freelist_t *entry) {
  if (list->next)
    list->next->prev = entry;
  list->next = entry;
  entry->prev = list;
}

typedef uint64_t u64;
typedef int64_t s64;

// prefixes each block of memory handed out to the user, must be 8 bytes
typedef struct block_header_t {
  u64 order_and_flags;
} block_header_t;

/* The allocator currently uses 8 byte per block for header information */
#define _BD_BLOCK_HEADER_SIZE sizeof(block_header_t)

/**
 * Computes log base two of a perfect power of two integer .
 *
 * The number of trailing zeroes in a number 2^i, is i.
 */
#define _BD_LOG2I(num) __builtin_ctz(num)

/**
 * Given a block of size 2^order, located at 'addr', returns the sibling
 * of this block (also of same size).
 *
 * Because of the way we allocate, siblings are powers of two away from
 * each other, which means flipping the 'order' bit of the address
 * of one, provides the address of the other.
 */
inline freelist_t *get_sibling_addr(void *addr, int order) {
  void *sibling_addr = (void *)((u64)addr ^ (1 << order));

  freelist_t *sibling = (freelist_t *)(sibling_addr);

  return sibling;
}

inline u64 max(u64 a, u64 b) { return a > b ? a : b; }
inline u64 min(u64 a, u64 b) { return a < b ? a : b; }

/**
 * A branchless routine to compute the next power of two for 'value'
 *
 * Copied from Hacker's Delight, Figure 3-3.
 */
inline u64 next_power_of_two(u64 x) {
  x--;

  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;

  return ++x;
}

inline void create_freelist(void *block) {
  freelist_t node = {NULL, NULL};
  memcpy(block, &node, sizeof(node));
}

/**
 * Initializes the allocator.
 */
extern void *bdalloc_init(bdalloc_t *allocator, u64 size) {
  size = next_power_of_two(size + _BD_BLOCK_HEADER_SIZE);
  u64 order = _BD_LOG2I(size);

  // Cannot initialize blocks too small
  // TODO: @ErrorHandling
  assert(size >= (1 << _BD_MIN_ALLOC_ORDER));

  // TODO: @CustomArenas
  allocator->arena = malloc(size);
  if (allocator->arena == NULL)
    return NULL;
  allocator->size = size;

  // zero it out. this is necessary because we embed block free/used states as a
  // header
  memset(allocator->arena, 0, size);

  // The blocks are empty, except the first - which points the entire arena
  for (int i = 0; i < _BD_MAX_MEM_ORDER; ++i) {
    allocator->blocks[i] = NULL;
  }
  allocator->blocks[order] = (freelist_t *)allocator->arena;

  // The arena itself must also behave as a freelist_t
  create_freelist(allocator->arena);

  return allocator->arena;
}

/**
 * Transforms a block of memory that we are currently threading the freelist
 * through, into a block that the user can use.
 *
 * Attaches the header block, and moves the pointer forwards by one header
 * block. The input pointer should be considered illegal for modification
 * until the user gives it back.
 *
 * The header contains an integer which has two values:
 * 1. The MSB is set to 1 signaling that this block is under use
 * 2. The remaining 63 bits represent the allocation order
 */
inline void *freelist_to_user_blk(freelist_t *block, u64 alloc_order) {
  block_header_t *mem = (block_header_t *)block;

  u64 order_and_flags = alloc_order | (1UL << 63);
  mem->order_and_flags = order_and_flags;

  void *user_mem = (void *)(mem + 1);

  return user_mem;
}

/* Deinitializes the allocator */
extern void bdalloc_deinit(bdalloc_t *allocator) {
  // TODO: @CustomArenas
  free(allocator->arena);
}

/* Allocate SIZE bytes */
extern void *bdalloc(bdalloc_t *allocator, u64 size) {
  // Find out the size (and corresponding order) we are going to allocate.
  u64 alloc_size = max(size, _BD_MIN_ALLOC_SIZE);
  alloc_size = next_power_of_two(alloc_size + _BD_BLOCK_HEADER_SIZE);
  u64 alloc_order = _BD_LOG2I(alloc_size);

  // Check if a block of alloc_order is already available. If it is,
  // then simply detach that block, add the header, and return it
  // to the user.
  {
    freelist_t **alloc_blocks = &allocator->blocks[alloc_order];
    if (*alloc_blocks != NULL) {
      freelist_t *block = freelist_detach(alloc_blocks);
      assert(block != NULL);
      void *mem = freelist_to_user_blk(block, alloc_order);

      return mem;
    }
  }

  // try finding a bigger available block
  u64 available_order = alloc_order;
  {
    freelist_t **block = &allocator->blocks[available_order];
    for (; available_order < _BD_MAX_MEM_ORDER && *block == NULL;
         available_order++) {
      block = &allocator->blocks[available_order];
    }

    // if no block found, either out of memory, or too fragmented.
    if (*block == NULL) {
      return NULL;
    }

    // the loop results in an off by one value
    available_order--;
  }

  // Break up larger order blocks into blocks of alloc_order. This
  // works by splitting up blocks into two equal halves, then splitting the
  // first half again, and again until the desired block is created.
  {
    for (u64 order = available_order; order > alloc_order; order--) {
      // first child starts at the same address as parent.
      freelist_t *first = freelist_detach(&allocator->blocks[order]);

      // sibling initially has no pointers until it is attached to the freelist
      // at the smaller order
      freelist_t *second = get_sibling_addr(first, order - 1);
      second->prev = NULL;
      second->next = NULL;

      // child is 1 level lower
      freelist_t **children = &allocator->blocks[order - 1];
      if (*children == NULL) {
        *children = first;
        freelist_attach(*children, second);
      } else {
        freelist_attach(*children, first);
        freelist_attach(*children, second);
      }
    }
  }

  // An entry must exist for this allocation order at this point.
  assert(allocator->blocks[alloc_order] != NULL);

  // Detach the entry, and write the headers
  freelist_t *mem_block = freelist_detach(&allocator->blocks[alloc_order]);
  void *mem = freelist_to_user_blk(mem_block, alloc_order);

  return mem;
}

/**
 * Given a pointer to a block that is being deallocated, unwinds the pointer
 * to reveal the header and returns the allocation order.
 *
 * After this method, *block can be threaded back onto the freelist.
 */
inline u64 prepare_for_free(void **block) {
  block_header_t *mem = (block_header_t *)(*block);
  mem--; // unwind to reveal header

  // mask out the MSB, rest of the bits represent the allocation order
  u64 mask = ~(1UL << 63);
  u64 order = (mem->order_and_flags) & mask;

  // point to the real block (where the header starts)
  *block = (void *)mem;

  // clean up the data to make it castable to freelist
  create_freelist(*block);

  return order;
}

// Removes 'block' from the allocator's allocation list 'alloc_list' assuming
// the block does exist in that list.
inline void bdalloc_splice_block(freelist_t **alloc_list, freelist_t *block) {
  // remove the block from the list, fixing up neighboring pointers
  freelist_splice(block);

  // if it was the first block in the alloc list, fixup the head of the list by
  // by detaching the current head (i.e. detaching 'block')
  if (*alloc_list == block)
    freelist_detach(alloc_list);
}

// Recursively colaesces a block of a given order with it's buddy, provided that
// the buddy is free.
void coalesce_at_order(bdalloc_t *allocator, freelist_t *block, u64 order,
                       u64 max_order) {
  if (order == max_order)
    return;

  freelist_t *bud = get_sibling_addr(block, order);

  block_header_t *bud_header = (block_header_t *)bud;
  int is_bud_in_use = (bud_header->order_and_flags >> 63UL) & 0x1;
  if (is_bud_in_use)
    return;

  // splice off the block & it's buddy from their allocation lists (they belong
  // to the same one)
  bdalloc_splice_block(&allocator->blocks[order], block);
  bdalloc_splice_block(&allocator->blocks[order], bud);

  // take the smaller of the two addresses, as the smaller address will
  // be the address for the combined block
  freelist_t *next = block;
  if (min((u64)block, (u64)bud) == (u64)bud)
    next = bud;

  // attach the combined block to the freelist of order+1
  freelist_t **list = &allocator->blocks[order + 1];
  if (*list == NULL) {
    *list = next;
  } else {
    freelist_attach(*list, next);
  }

  // recursively coalesce
  coalesce_at_order(allocator, next, order + 1, max_order);
}

/* Free a block allocated by `bdalloc`. This only returns the block to the
 * allocator, not necessarily to the system. */
extern void bdalloc_free(bdalloc_t *allocator, void *blk) {
  u64 order = prepare_for_free(&blk);
  freelist_t *freed_blk = (freelist_t *)blk;

  freelist_t **list = &allocator->blocks[order];
  if (*list == NULL) {
    *list = freed_blk;
  } else {
    freelist_attach(*list, freed_blk);
  }

  u64 max_order = _BD_LOG2I(allocator->size);

  coalesce_at_order(allocator, freed_blk, order, max_order);
}

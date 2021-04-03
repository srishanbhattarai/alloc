# bdalloc
This project implements a fixed memory allocator for 64-bit systems based on Knuth's original [power of two allocator](https://en.wikipedia.org/wiki/Buddy_memory_allocation).

The allocator is efficient in splitting and coalescing memory, and is resilient to external fragmentation. Some of the implementation notes below may help decide if internal fragmentation is tolerable.

* It uses a power of two scheme i.e. blocks are always rounded up to the next power of two when allocating
* Maximum arena that can be allocated is `1 << 32` bytes. To manage large blocks of memory you probably want to use a `slab` allocator instead of (or alongside) this one.
* The allocator itself only has 3 pointers worth of space overhead. However, each allocation, regardless of size, has a QWORD sized header.
  * Using headers provides a simple `malloc/free` like API.
* Performance may degrade if you repeatedly alloc/dealloc the same block size. frees are not optimized (yet!)
* 32-bit pointers are not supported

# Example
```c
// Initialize the allocator with the arena size you want, calls malloc()
bdalloc_t allocator;
void *arena = bdalloc_init(&allocator, 1024);
assert(arena != NULL);

// allocate on the arena
char* q = (char*) bdalloc(&allocator, 512);
char* r = (char*) bdalloc(&allocator, 256);

// return blocks back to the allocator
bdalloc_free(&allocator, r);
bdalloc_free(&allocator, q);

// De-init the allocator, calls free()
bdalloc_deinit(&allocator);
```

# License
MIT

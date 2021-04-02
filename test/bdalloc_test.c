#include <assert.h>
#include <bdalloc.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
  bdalloc_t allocator;
  void *arena = bdalloc_init(&allocator, 1024);
  assert(arena != NULL);

  char *p = (char *)allocator.arena;
  char *q = (char *)bdalloc(&allocator, 512);
  char *r = (char *)bdalloc(&allocator, 256);

  printf("p=%p, q=%p, r=%p, q-p=%lu, r-q=%lu\n", p, q, r, q - p, r - q);

  bdalloc_free(&allocator, r);
  bdalloc_free(&allocator, q);

  q = (char *)bdalloc(&allocator, 512);
  r = (char *)bdalloc(&allocator, 256);

  printf("p=%p, q=%p, r=%p, q-p=%lu, r-q=%lu\n", p, q, r, q - p, r - q);

  bdalloc_free(&allocator, q);
  bdalloc_free(&allocator, r);

  bdalloc_deinit(&allocator);
}

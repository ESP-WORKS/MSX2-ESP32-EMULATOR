#include <stdlib.h>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
static inline void *heap_caps_malloc(size_t n,unsigned){return malloc(n);}
static inline size_t heap_caps_get_free_size(unsigned){ return 512*1024; }

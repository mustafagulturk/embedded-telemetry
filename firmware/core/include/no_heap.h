/**
 * @file no_heap.h
 * @brief Force-included into every project source: dynamic memory is banned.
 *
 * Any use of these identifiers is a compile error (MISRA C:2012 Rule 21.3).
 */
#ifndef NO_HEAP_H
#define NO_HEAP_H

#include <stdlib.h>

#pragma GCC poison malloc calloc realloc free

#endif /* NO_HEAP_H */

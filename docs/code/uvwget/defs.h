#ifndef DEFS_H_
#define DEFS_H_

#include <stddef.h> 

#define CONTAINER_OF(ptr, type, field)                                        \
  ((type *) ((char *) (ptr) - offsetof(type, field)))

#endif /* DEFS_H */


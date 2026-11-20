#pragma once

#include <stdlib.h>
#include <stdio.h>

#define ASSERT(expr)                                                         \
  do {                                                                       \
    if (!(expr)) {                                                           \
      fprintf(stderr, "Assertion failed: %s, file %s, line %d\n",            \
              #expr, __FILE__, __LINE__);                                    \
      std::abort();                                                          \
    }                                                                        \
  } while (false)

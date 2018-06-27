#ifndef UV_SSIZE_T_H
#define UV_SSIZE_T_H

#if defined(_SSIZE_T_) || defined(_SSIZE_T_DEFINED)
# include <stdlib.h> /* ssize_t */
typedef ssize_t uv_ssize_t;
#elif defined(_WIN32)
typedef intptr_t uv_ssize_t;
#else
# include <unistd.h> /* ssize_t */
typedef ssize_t uv_ssize_t;
#endif

#endif /* UV_SSIZE_T_H */

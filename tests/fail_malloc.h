/*
** Force-included with `cc -include tests/fail_malloc.h` into the
** STUDENT's .c files only (never into main.c): every malloc() they call
** goes through gnl_test_malloc() in main.c, which can make one fail.
*/
#ifndef FAIL_MALLOC_H
# define FAIL_MALLOC_H
# include <stddef.h>

void	*gnl_test_malloc(size_t size);
# define malloc(size) gnl_test_malloc(size)

#endif

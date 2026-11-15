/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   get_next_line_bonus.h                              :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mnahli <mnahli@student.1337.ma>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/11/15 18:00:00 by mnahli            #+#    #+#             */
/*   Updated: 2026/11/15 18:00:00 by mnahli           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef GET_NEXT_LINE_BONUS_H
# define GET_NEXT_LINE_BONUS_H

# ifndef BUFFER_SIZE
#  define BUFFER_SIZE 42
# endif

/*
** READ_SIZE is what malloc() and read() really use. With BUFFER_SIZE <= 0
** get_next_line() returns NULL before reading, but the compiler would
** still reject read(fd, buf, -5) under -Werror, so it never sees it.
*/
# if BUFFER_SIZE > 0
#  define READ_SIZE BUFFER_SIZE
# else
#  define READ_SIZE 1
# endif

# include <stdlib.h>
# include <unistd.h>

/*
** Highest fd + 1 we keep a stash for (one slot per file descriptor).
** Linux default soft limit for open files is 1024 (ulimit -n).
*/
# ifndef FD_MAX
#  define FD_MAX 1024
# endif

char	*get_next_line(int fd);

size_t	gnl_strlen(const char *s);
char	*gnl_strchr(const char *s, int c);
char	*gnl_strjoin(char *stash, const char *buf);
char	*gnl_substr(const char *s, size_t start, size_t len);

#endif

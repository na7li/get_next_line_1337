/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   get_next_line_bonus.c                              :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mnahli <mnahli@student.1337.ma>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/11/15 18:00:00 by mnahli            #+#    #+#             */
/*   Updated: 2026/11/15 18:00:00 by mnahli           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "get_next_line_bonus.h"

/*
** Step 1 - READ: append BUFFER_SIZE-byte chunks to the stash until the stash
** contains a '\n' or read() reaches the end of the file.
** If the stash already holds a full line, nothing is read at all.
** On a read/malloc error the stash is freed and NULL is returned.
*/
static char	*fill_stash(int fd, char *stash)
{
	char	*buf;
	ssize_t	bytes;

	if (gnl_strchr(stash, '\n'))
		return (stash);
	buf = malloc((size_t)READ_SIZE + 1);
	bytes = 1;
	while (buf && bytes > 0 && !gnl_strchr(stash, '\n'))
	{
		bytes = read(fd, buf, READ_SIZE);
		if (bytes > 0)
		{
			buf[bytes] = '\0';
			stash = gnl_strjoin(stash, buf);
		}
		if (bytes > 0 && !stash)
			bytes = -1;
	}
	if (!buf || bytes < 0)
	{
		free(stash);
		stash = NULL;
	}
	free(buf);
	return (stash);
}

/*
** Step 2 - EXTRACT: copy the stash up to and including the first '\n'
** (or the whole stash if the file did not end with '\n').
** An empty stash means there is nothing left to return.
*/
static char	*extract_line(const char *stash)
{
	size_t	len;

	if (!stash || !stash[0])
		return (NULL);
	len = 0;
	while (stash[len] && stash[len] != '\n')
		len++;
	if (stash[len] == '\n')
		len++;
	return (gnl_substr(stash, 0, len));
}

/*
** Step 3 - KEEP THE REST: drop the line we just returned from the stash.
** If nothing is left, the stash is freed and becomes NULL.
*/
static char	*trim_stash(char *stash, size_t line_len)
{
	char	*rest;

	if (!stash[line_len])
	{
		free(stash);
		return (NULL);
	}
	rest = gnl_substr(stash, line_len, gnl_strlen(stash) - line_len);
	free(stash);
	return (rest);
}

/*
** BONUS: ONE static variable, an array with one stash per fd.
** stash[3] and stash[4] never touch each other, so we can alternate
** between files. read(fd, NULL, 0) reads nothing: it only checks that
** fd is readable; if not, that fd's leftovers are dropped.
*/
char	*get_next_line(int fd)
{
	static char	*stash[FD_MAX];
	char		*line;

	if (fd < 0 || fd >= FD_MAX || BUFFER_SIZE <= 0)
		return (NULL);
	if (read(fd, NULL, 0) < 0)
	{
		free(stash[fd]);
		stash[fd] = NULL;
		return (NULL);
	}
	stash[fd] = fill_stash(fd, stash[fd]);
	line = extract_line(stash[fd]);
	if (!line)
	{
		free(stash[fd]);
		stash[fd] = NULL;
		return (NULL);
	}
	stash[fd] = trim_stash(stash[fd], gnl_strlen(line));
	return (line);
}

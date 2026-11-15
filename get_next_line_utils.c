/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   get_next_line_utils.c                              :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mnahli <mnahli@student.1337.ma>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/11/15 18:00:00 by mnahli            #+#    #+#             */
/*   Updated: 2026/11/15 18:00:00 by mnahli           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "get_next_line.h"

size_t	gnl_strlen(const char *s)
{
	size_t	i;

	i = 0;
	if (!s)
		return (0);
	while (s[i])
		i++;
	return (i);
}

char	*gnl_strchr(const char *s, int c)
{
	if (!s)
		return (NULL);
	while (*s)
	{
		if (*s == (char)c)
			return ((char *)s);
		s++;
	}
	if ((char)c == '\0')
		return ((char *)s);
	return (NULL);
}

/*
** Returns a new string "stash + buf" and ALWAYS frees the old stash,
** so the caller can simply write: stash = gnl_strjoin(stash, buf);
** stash may be NULL (first call). Returns NULL if malloc fails.
*/
char	*gnl_strjoin(char *stash, const char *buf)
{
	char	*joined;
	size_t	i;
	size_t	j;

	joined = malloc(gnl_strlen(stash) + gnl_strlen(buf) + 1);
	if (joined)
	{
		i = 0;
		while (stash && stash[i])
		{
			joined[i] = stash[i];
			i++;
		}
		j = 0;
		while (buf[j])
		{
			joined[i + j] = buf[j];
			j++;
		}
		joined[i + j] = '\0';
	}
	free(stash);
	return (joined);
}

/*
** Copies at most len chars of s, starting at index start.
** The caller guarantees start <= strlen(s).
*/
char	*gnl_substr(const char *s, size_t start, size_t len)
{
	char	*sub;
	size_t	i;

	sub = malloc(len + 1);
	if (!sub)
		return (NULL);
	i = 0;
	while (i < len && s[start + i])
	{
		sub[i] = s[start + i];
		i++;
	}
	sub[i] = '\0';
	return (sub);
}

/*
** get_next_line tester  —  tests/main.c
**
** Works with ANY get_next_line (yours or a peer's): it declares the
** prototype itself, so it does not need the student's header.
**
**   cc -Wall -Wextra -Werror -D BUFFER_SIZE=42 \
**      get_next_line.c get_next_line_utils.c tests/main.c -o gnl_test
**   ./gnl_test                  run every mandatory test
**   ./gnl_test bonus            + multi-fd tests (compile the _bonus.c files)
**   ./gnl_test -q               only print failures + the summary line
**   ./gnl_test --no-fork        run in one process (use it under valgrind)
**   ./gnl_test file.txt         print a file line by line, cat -e style
**   ./gnl_test -                same, reading from stdin
**
** malloc-failure build (each malloc of get_next_line fails once, in turn):
**   cc -c -include tests/fail_malloc.h get_next_line.c get_next_line_utils.c
**   cc -D GNL_FAIL_MALLOC tests/main.c get_next_line.o get_next_line_utils.o \
**      -o gnl_malloc && ./gnl_malloc malloc
**   (tests/run.sh does this for you, with valgrind / leaks on top)
**
** Each test runs in its own process (fork), so a segfault or an infinite
** loop shows up as CRASH / TIMEOUT and the other tests keep running.
** It also gives every test a fresh static variable.
**
** Verdicts:
**   OK       behaves as the subject requires
**   KO       wrong line / missing NULL
**   WARN     "strict" test: good practice, debatable as a hard requirement
**   CRASH    killed by a signal (segfault, abort, double free...)
**   TIMEOUT  still running after TIMEOUT_S seconds
*/

#ifdef GNL_FAIL_MALLOC
# undef malloc
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

char	*get_next_line(int fd);

#ifdef BUFFER_SIZE
# define BS_KNOWN 1
# define BS_VALUE ((long)(BUFFER_SIZE))
#else
# define BS_KNOWN 0
# define BS_VALUE 42L
#endif

#define TIMEOUT_S 10
#define MAX_DETAILS 3

enum e_kind { MANDATORY, STRICT, BONUS };

typedef struct s_test
{
	const char	*name;
	void		(*fn)(void);
	enum e_kind	kind;
}	t_test;

static int			g_fail;
static int			g_details;
static char			g_tmpdir[64];
static int			g_file_id;
static int			g_color;

#define C_RED    (g_color ? "\033[31m" : "")
#define C_GREEN  (g_color ? "\033[32m" : "")
#define C_YELLOW (g_color ? "\033[33m" : "")
#define C_DIM    (g_color ? "\033[2m" : "")
#define C_BOLD   (g_color ? "\033[1m" : "")
#define C_RESET  (g_color ? "\033[0m" : "")

/* ------------------------------------------------------------------ */
/*  small helpers                                                      */
/* ------------------------------------------------------------------ */

static void	*xmalloc(size_t n)
{
	void	*p = malloc(n);

	if (!p)
	{
		perror("tester: malloc");
		exit(2);
	}
	return (p);
}

/* print a string with \n, \t and non-printables made visible */
static void	show(const char *label, const char *s)
{
	size_t	len;
	size_t	i;

	if (!s)
	{
		printf("           %-9s NULL\n", label);
		return ;
	}
	len = strlen(s);
	printf("           %-9s \"", label);
	for (i = 0; i < len && i < 60; i++)
	{
		if (s[i] == '\n')
			printf("\\n");
		else if (s[i] == '\t')
			printf("\\t");
		else if ((unsigned char)s[i] < 32 || (unsigned char)s[i] > 126)
			printf("\\x%02x", (unsigned char)s[i]);
		else
			putchar(s[i]);
	}
	printf("\"%s  (len %zu)\n", len > 60 ? "..." : "", len);
}

static void	fail_header(const char *ctx)
{
	g_fail++;
	if (++g_details <= MAX_DETAILS)
		printf("         %s%s%s\n", C_DIM, ctx, C_RESET);
}

/* call get_next_line(fd) once and compare with want (NULL = expect NULL) */
static int	expect(int fd, const char *want, const char *ctx)
{
	char	*got;
	int		ok;
	size_t	i;

	got = get_next_line(fd);
	ok = (!want && !got) || (want && got && strcmp(want, got) == 0);
	if (!ok)
	{
		fail_header(ctx);
		if (g_details <= MAX_DETAILS)
		{
			show("expected", want);
			show("got", got);
			if (want && got)
			{
				i = 0;
				while (want[i] && want[i] == got[i])
					i++;
				printf("           first difference at index %zu\n", i);
			}
		}
	}
	free(got);
	return (ok);
}

/*
** Feed the expected content line by line: every line keeps its '\n',
** the last one has none if the file does not end with '\n'.
** Then get_next_line must return NULL (twice: still NULL after EOF).
*/
static int	expect_content(int fd, const char *content, const char *what)
{
	const char	*p = content;
	const char	*nl;
	char		*want;
	char		ctx[256];
	size_t		len;
	int			n = 0;

	while (*p)
	{
		nl = strchr(p, '\n');
		len = nl ? (size_t)(nl - p + 1) : strlen(p);
		want = xmalloc(len + 1);
		memcpy(want, p, len);
		want[len] = '\0';
		n++;
		snprintf(ctx, sizeof(ctx), "%s: call #%d", what, n);
		if (!expect(fd, want, ctx))
			return (free(want), 0);
		free(want);
		p += len;
	}
	snprintf(ctx, sizeof(ctx), "%s: call #%d (end of file -> NULL)", what, n + 1);
	if (!expect(fd, NULL, ctx))
		return (0);
	snprintf(ctx, sizeof(ctx), "%s: call #%d (after EOF -> still NULL)", what, n + 2);
	return (expect(fd, NULL, ctx));
}

/* create a temp file holding content, return its path (malloc'd) */
static char	*make_file(const char *content)
{
	char	*path;
	int		fd;
	size_t	len;

	path = xmalloc(128);
	snprintf(path, 128, "%s/f%04d.txt", g_tmpdir, g_file_id++);
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
	{
		perror(path);
		exit(2);
	}
	len = strlen(content);
	if (write(fd, content, len) != (ssize_t)len)
		exit(2);
	close(fd);
	return (path);
}

static int	open_content(const char *content)
{
	char	*path = make_file(content);
	int		fd = open(path, O_RDONLY);

	free(path);
	if (fd < 0)
		exit(2);
	return (fd);
}

/* write content to a file, read it back with gnl, close it */
static void	check_file(const char *content, const char *what)
{
	int	fd = open_content(content);

	expect_content(fd, content, what);
	close(fd);
}

/*
** Return the read end of a pipe. A writer process sends content
** `chunk` bytes at a time, sleeping `delay_us` between chunks, so
** read() returns short, uneven pieces (not BUFFER_SIZE bytes).
*/
static int	pipe_content(const char *content, size_t chunk, int delay_us,
				pid_t *writer)
{
	int		p[2];
	size_t	len = strlen(content);
	size_t	off = 0;
	size_t	n;

	if (pipe(p) < 0)
		exit(2);
	fflush(stdout);
	*writer = fork();
	if (*writer == 0)
	{
		close(p[0]);
		while (off < len)
		{
			n = len - off < chunk ? len - off : chunk;
			if (write(p[1], content + off, n) < 0)
				_exit(1);
			off += n;
			if (delay_us)
				usleep(delay_us);
		}
		close(p[1]);
		_exit(0);
	}
	close(p[1]);
	return (p[0]);
}

/* content made of `len` letters a..z repeating, shifted by `seed` */
static char	*letters(size_t len, int seed)
{
	char	*s = xmalloc(len + 1);
	size_t	i;

	for (i = 0; i < len; i++)
		s[i] = 'a' + (char)((i + seed) % 26);
	s[len] = '\0';
	return (s);
}

static char	*cat3(const char *a, const char *b, const char *c)
{
	size_t	la = strlen(a);
	size_t	lb = strlen(b);
	size_t	lc = strlen(c);
	char	*s = xmalloc(la + lb + lc + 1);

	memcpy(s, a, la);
	memcpy(s + la, b, lb);
	memcpy(s + la + lb, c, lc + 1);
	return (s);
}

/* ------------------------------------------------------------------ */
/*  MANDATORY tests                                                    */
/* ------------------------------------------------------------------ */

static void	t_invalid_fd(void)
{
	int	fd;

	expect(-1, NULL, "fd = -1");
	expect(-42, NULL, "fd = -42");
	expect(4242, NULL, "fd = 4242 (never opened)");
	fd = open_content("I am closed\n");
	close(fd);
	expect(fd, NULL, "fd that was already closed");
}

static void	t_empty_file(void)
{
	check_file("", "empty file");
}

static void	t_only_newline(void)
{
	check_file("\n", "file = \"\\n\"");
}

static void	t_only_newlines(void)
{
	check_file("\n\n\n\n\n", "file = 5 x \"\\n\"");
}

static void	t_one_line_nl(void)
{
	check_file("Hello, 1337!\n", "one line ending with \\n");
}

static void	t_one_line_no_nl(void)
{
	check_file("Hello, 1337!", "one line, no \\n at the end");
}

static void	t_one_char(void)
{
	check_file("x", "file = \"x\"");
	check_file("x\n", "file = \"x\\n\"");
}

static void	t_lines_nl_end(void)
{
	check_file("first line\nsecond line\nthird line\n",
		"3 lines, file ends with \\n");
}

static void	t_lines_no_nl_end(void)
{
	check_file("first line\nsecond line\nthird line",
		"3 lines, last one without \\n");
}

static void	t_empty_lines_middle(void)
{
	check_file("a\n\nb\n\n\nc\n\n", "empty lines in the middle and at the end");
	check_file("\n\nstarts with 2 empty lines", "empty lines at the start");
}

static void	t_short_lines(void)
{
	check_file("1\n22\n333\n4444\n55555\n666666\n7777777\n88888888\n",
		"growing short lines");
	check_file("   \n\t\t\n  spaces  \n \t \n", "spaces and tabs");
}

/*
** The classic traps: a line exactly BUFFER_SIZE long, one char shorter,
** one char longer, 2 x BUFFER_SIZE ... plus a spread of other lengths.
*/
static void	t_line_lengths(void)
{
	long	lens[64];
	int		n = 0;
	long	fixed[] = {1, 2, 3, 4, 5, 7, 8, 9, 10, 15, 16, 17, 31, 32, 33,
		41, 42, 43, 63, 64, 65, 99, 100, 101, 127, 128, 129, 255, 256, 257,
		511, 512, 513, 1023, 1024, 1025, 4095, 4096, 4097};
	long	bs = BS_VALUE;
	char	what[128];
	char	*l1;
	char	*l2;
	char	*l3;
	char	*c;
	int		i;

	for (i = 0; i < (int)(sizeof(fixed) / sizeof(*fixed)); i++)
		lens[n++] = fixed[i];
	if (bs > 0 && bs <= 50000)
	{
		long	extra[] = {bs - 1, bs, bs + 1, 2 * bs - 1, 2 * bs, 2 * bs + 1};

		for (i = 0; i < 6; i++)
			if (extra[i] > 0)
				lens[n++] = extra[i];
	}
	for (i = 0; i < n; i++)
	{
		l1 = letters(lens[i], 0);
		l2 = letters(lens[i], 7);
		l3 = letters(lens[i], 13);
		c = cat3(l1, "\n", "");
		snprintf(what, sizeof(what), "1 line of %ld chars + \\n", lens[i]);
		check_file(c, what);
		free(c);
		c = cat3(l1, "", "");
		snprintf(what, sizeof(what), "1 line of %ld chars, no \\n", lens[i]);
		check_file(c, what);
		free(c);
		{
			char	*a = cat3(l1, "\n", l2);
			char	*b = cat3(a, "\n", l3);

			snprintf(what, sizeof(what), "3 lines of %ld chars", lens[i]);
			check_file(b, what);
			free(a);
			free(b);
		}
		free(l1);
		free(l2);
		free(l3);
	}
}

static void	t_long_line(void)
{
	char	*big = letters(20000, 3);
	char	*c;

	c = cat3(big, "\n", "short line after\n");
	check_file(c, "20000-char line + \\n, then a short line");
	free(c);
	check_file(big, "20000-char line, no \\n");
	c = cat3("short line before\n", big, "\n");
	check_file(c, "short line, then a 20000-char line");
	free(c);
	free(big);
}

static void	t_many_lines(void)
{
	size_t	cap = 200000;
	char	*s = xmalloc(cap);
	size_t	len = 0;
	int		i;
	int		j;

	for (i = 1; i <= 2000; i++)
	{
		len += snprintf(s + len, cap - len, "line %04d:", i);
		for (j = 0; j < i % 53; j++)
			s[len++] = 'A' + (char)(j % 26);
		s[len++] = '\n';
	}
	s[len] = '\0';
	check_file(s, "2000 lines of 10..62 chars");
	free(s);
}

static void	t_read_after_eof(void)
{
	int	fd = open_content("only line\n");

	expect(fd, "only line\n", "first call");
	expect(fd, NULL, "call 2 (EOF)");
	expect(fd, NULL, "call 3 (still EOF)");
	expect(fd, NULL, "call 4 (still EOF)");
	expect(fd, NULL, "call 5 (still EOF)");
	close(fd);
}

/* a fd is not only a regular file: pipes return short reads */
static void	t_pipe_chunks(void)
{
	const char	*c = "hello\nworld\nthis arrives in tiny pieces\n\nno newline at end";
	pid_t		w;
	int			fd;

	fd = pipe_content(c, 3, 2000, &w);
	expect_content(fd, c, "pipe, writer sends 3 bytes every 2 ms");
	close(fd);
	waitpid(w, NULL, 0);
	fd = pipe_content(c, 1, 0, &w);
	expect_content(fd, c, "pipe, writer sends 1 byte at a time");
	close(fd);
	waitpid(w, NULL, 0);
}

static void	t_stdin(void)
{
	const char	*c = "from stdin 1\nfrom stdin 2\n\nlast line without newline";
	int			saved = dup(STDIN_FILENO);
	pid_t		w;
	int			fd;

	fd = pipe_content(c, 7, 1000, &w);
	dup2(fd, STDIN_FILENO);
	close(fd);
	expect_content(STDIN_FILENO, c, "fd 0 (stdin fed by a pipe)");
	dup2(saved, STDIN_FILENO);
	close(saved);
	waitpid(w, NULL, 0);
}

static void	t_bad_fd_types(void)
{
	char	*path = make_file("cannot be read\n");
	int		fd;

	fd = open(g_tmpdir, O_RDONLY);
	expect(fd, NULL, "fd of a directory (read() fails with EISDIR)");
	close(fd);
	fd = open(path, O_WRONLY);
	expect(fd, NULL, "fd opened O_WRONLY (read() fails with EBADF)");
	close(fd);
	free(path);
}

/* after EOF the static must be empty: next file starts clean */
static void	t_reuse_fd_after_eof(void)
{
	int	fd1 = open_content("file A line 1\nfile A line 2");
	int	fd2;

	expect_content(fd1, "file A line 1\nfile A line 2", "file A");
	close(fd1);
	fd2 = open_content("file B line 1\nfile B line 2\n");
	if (fd2 != fd1)
	{
		dup2(fd2, fd1);
		close(fd2);
	}
	expect_content(fd1, "file B line 1\nfile B line 2\n",
		"file B opened on the same fd number after A hit EOF");
	close(fd1);
}

/*
** STRICT: stop reading A in the middle, close it, and open B on the SAME
** fd number. A good gnl notices the closed fd (read error -> NULL) and
** drops A's leftovers, so B starts clean. Many valid projects fail this,
** so it is a WARN, not a KO.
*/
static void	t_strict_close_mid_file(void)
{
	int	fd1 = open_content("A1\nA2\nA3\n");
	int	fd2;

	expect(fd1, "A1\n", "file A: call #1");
	close(fd1);
	expect(fd1, NULL, "same fd after close(): should be NULL");
	fd2 = open_content("B1\nB2\n");
	if (fd2 != fd1)
	{
		dup2(fd2, fd1);
		close(fd2);
	}
	expect_content(fd1, "B1\nB2\n", "file B on the same fd: no A leftovers");
	close(fd1);
}

/*
** A real read() error in the middle of a file: after line 1 a directory
** is put on the same fd number (dup2), so the next read() fails with
** EISDIR. Lines already in the stash may still come out, but they must
** be the right ones, and then get_next_line must return NULL.
** Returns the fd (now the directory) so the strict test can go on.
*/
static int	read_error_mid_file(void)
{
	const char	*rest[] = {"line 2\n", "line 3\n", "line 4\n"};
	int			fd = open_content("line 1\nline 2\nline 3\nline 4\n");
	int			dir = open(g_tmpdir, O_RDONLY);
	char		*got;
	int			k;

	expect(fd, "line 1\n", "before the error: call #1");
	dup2(dir, fd);
	close(dir);
	for (k = 0; k < 4; k++)
	{
		got = get_next_line(fd);
		if (!got)
			break ;
		if (k == 3 || strcmp(got, rest[k]) != 0)
		{
			fail_header("after read() failed: wrong line (must be NULL or the next line)");
			if (g_details <= MAX_DETAILS)
			{
				show("expected", k < 3 ? rest[k] : NULL);
				show("got", got);
			}
			free(got);
			return (fd);
		}
		free(got);
	}
	expect(fd, NULL, "fd still failing: must stay NULL");
	return (fd);
}

static void	t_read_error(void)
{
	close(read_error_mid_file());
}

/* STRICT: after the error, the static must not keep old bytes */
static void	t_strict_after_read_error(void)
{
	int	fd = read_error_mid_file();
	int	fd2 = open_content("new file 1\nnew file 2\n");

	dup2(fd2, fd);
	close(fd2);
	expect_content(fd, "new file 1\nnew file 2\n",
		"new file on the same fd after the error: no old leftovers");
	close(fd);
}

#ifdef GNL_FAIL_MALLOC
/*
** Built with -include fail_malloc.h on the student's files only: their
** malloc() goes through here. Pass n makes their n-th malloc return NULL,
** and the passes go on until every malloc of a full read has failed once.
*/
static long	g_malloc_fail_at = -1;
static long	g_malloc_count;

void	*gnl_test_malloc(size_t size)
{
	if (g_malloc_count++ == g_malloc_fail_at)
		return (NULL);
	return (malloc(size));
}

static size_t	next_line_len(const char *p)
{
	const char	*nl = strchr(p, '\n');

	return (nl ? (size_t)(nl - p + 1) : strlen(p));
}

static void	t_malloc_fail(void)
{
	const char	*c = "first line\nsecond line\n\nfourth line, no newline";
	char		*path = make_file(c);
	char		*got;
	char		*want;
	const char	*p;
	char		ctx[200];
	long		n;
	long		total = 0;
	int			k;
	int			fd;

	for (n = 0; n < 1000; n++)
	{
		fd = open(path, O_RDONLY);
		p = c;
		g_malloc_count = 0;
		g_malloc_fail_at = n;
		for (k = 0; k < 8; k++)
		{
			got = get_next_line(fd);
			if (!got)
				break ;
			if (!*p || strlen(got) != next_line_len(p)
				|| strncmp(got, p, next_line_len(p)) != 0)
			{
				snprintf(ctx, sizeof(ctx), "malloc #%ld returned NULL: call #%d"
					" gave a wrong line (must be NULL or the right line)", n + 1, k + 1);
				fail_header(ctx);
				want = *p ? strndup(p, next_line_len(p)) : NULL;
				if (g_details <= MAX_DETAILS)
				{
					show("expected", want);
					show("got", got);
				}
				free(want);
				free(got);
				break ;
			}
			p += next_line_len(p);
			free(got);
		}
		total = g_malloc_count;
		g_malloc_fail_at = -1;
		while ((got = get_next_line(fd)))
			free(got);
		close(fd);
		if (total <= n)
			break ;
	}
	if (n == 0 && total == 0)
		printf("         %s(malloc could not be intercepted on this system:"
			" test skipped)%s\n", C_DIM, C_RESET);
	else if (!g_fail)
		printf("         %s%ld malloc calls, each one failed once: no crash,"
			" no wrong line%s\n", C_DIM, n, C_RESET);
	free(path);
}
#endif

/* BUFFER_SIZE <= 0: every call must return NULL (no crash, no read) */
static void	t_bs_not_positive(void)
{
	int	fd = open_content("some text\nmore text\n");

	expect(fd, NULL, "BUFFER_SIZE <= 0 must return NULL");
	expect(fd, NULL, "BUFFER_SIZE <= 0 must return NULL (2nd call)");
	close(fd);
}

/* ------------------------------------------------------------------ */
/*  BONUS tests (multiple fds at the same time)                        */
/* ------------------------------------------------------------------ */

/*
** Round-robin over n sources: one get_next_line per fd per turn,
** checking every fd against its own expected content.
*/
static void	interleave(int *fds, const char **contents, int n, const char *what)
{
	const char	*pos[64];
	int			done[64];
	int			left = n;
	int			calls = 0;
	int			i;
	char		ctx[256];

	for (i = 0; i < n; i++)
	{
		pos[i] = contents[i];
		done[i] = 0;
	}
	while (left > 0 && calls < 100000)
	{
		for (i = 0; i < n; i++)
		{
			const char	*nl;
			size_t		len;
			char		*want;

			if (done[i])
				continue ;
			calls++;
			if (!*pos[i])
			{
				snprintf(ctx, sizeof(ctx), "%s: source #%d (fd %d) end -> NULL",
					what, i + 1, fds[i]);
				if (!expect(fds[i], NULL, ctx))
					return ;
				done[i] = 1;
				left--;
				continue ;
			}
			nl = strchr(pos[i], '\n');
			len = nl ? (size_t)(nl - pos[i] + 1) : strlen(pos[i]);
			want = xmalloc(len + 1);
			memcpy(want, pos[i], len);
			want[len] = '\0';
			snprintf(ctx, sizeof(ctx), "%s: source #%d (fd %d), call #%d",
				what, i + 1, fds[i], calls);
			if (!expect(fds[i], want, ctx))
			{
				free(want);
				return ;
			}
			free(want);
			pos[i] += len;
		}
	}
}

static void	t_bonus_three_files(void)
{
	const char	*c[3] = {
		"A line 1\nA line 2\nA line 3\nA line 4\n",
		"B line 1\nB line 2",
		"C line 1\n\nC line 3\nC line 4\nC line 5\nC line 6 no newline"};
	int			fds[3];
	int			i;

	for (i = 0; i < 3; i++)
		fds[i] = open_content(c[i]);
	interleave(fds, c, 3, "3 files, one line from each in turn");
	for (i = 0; i < 3; i++)
		close(fds[i]);
}

static void	t_bonus_long_lines(void)
{
	char		*a = letters(5000, 1);
	char		*b = letters(3333, 2);
	const char	*c[2];
	int			fds[2];
	char		*ca = cat3(a, "\nshort\n", a);
	char		*cb = cat3("tiny\n", b, "\nend\n");

	c[0] = ca;
	c[1] = cb;
	fds[0] = open_content(ca);
	fds[1] = open_content(cb);
	interleave(fds, c, 2, "2 files with long lines, alternating");
	close(fds[0]);
	close(fds[1]);
	free(a);
	free(b);
	free(ca);
	free(cb);
}

static void	t_bonus_file_pipe_stdin(void)
{
	const char	*c[3] = {
		"file 1\nfile 2\nfile 3\n",
		"pipe 1\npipe 2\npipe 3\npipe 4",
		"stdin 1\nstdin 2\n"};
	int			fds[3];
	int			saved = dup(STDIN_FILENO);
	pid_t		w1;
	pid_t		w2;
	int			tmp;

	fds[0] = open_content(c[0]);
	fds[1] = pipe_content(c[1], 2, 500, &w1);
	tmp = pipe_content(c[2], 3, 500, &w2);
	dup2(tmp, STDIN_FILENO);
	close(tmp);
	fds[2] = STDIN_FILENO;
	interleave(fds, c, 3, "file + pipe + stdin, alternating");
	close(fds[0]);
	close(fds[1]);
	dup2(saved, STDIN_FILENO);
	close(saved);
	waitpid(w1, NULL, 0);
	waitpid(w2, NULL, 0);
}

static void	t_bonus_invalid_between(void)
{
	int	fd = open_content("keep 1\nkeep 2\nkeep 3\n");

	expect(fd, "keep 1\n", "valid fd: call #1");
	expect(-1, NULL, "fd -1 in between");
	expect(4242, NULL, "fd 4242 (not open) in between");
	expect(fd, "keep 2\n", "valid fd still has its state: call #2");
	expect(fd, "keep 3\n", "valid fd: call #3");
	expect(fd, NULL, "valid fd: EOF");
	close(fd);
}

static void	t_bonus_huge_fd(void)
{
	expect(1000000, NULL, "fd = 1000000 (must not index past the array)");
	expect(2147483647, NULL, "fd = INT_MAX");
}

static void	t_bonus_many_fds(void)
{
	enum { N = 40 };
	char		*contents[N];
	const char	*c[N];
	int			fds[N];
	char		buf[256];
	int			i;
	int			k;
	size_t		len;

	for (i = 0; i < N; i++)
	{
		len = 0;
		for (k = 0; k <= i % 7; k++)
			len += snprintf(buf + len, sizeof(buf) - len, "fd#%d line %d\n", i, k);
		contents[i] = strdup(buf);
		c[i] = contents[i];
		fds[i] = open_content(contents[i]);
	}
	interleave(fds, c, N, "40 files open at once");
	for (i = 0; i < N; i++)
	{
		close(fds[i]);
		free(contents[i]);
	}
}

/* ------------------------------------------------------------------ */
/*  runner                                                             */
/* ------------------------------------------------------------------ */

static const t_test	g_tests[] = {
	{"invalid fd (-1, -42, 4242, closed)", t_invalid_fd, MANDATORY},
	{"empty file", t_empty_file, MANDATORY},
	{"file = \"\\n\"", t_only_newline, MANDATORY},
	{"file = only newlines", t_only_newlines, MANDATORY},
	{"one line with \\n", t_one_line_nl, MANDATORY},
	{"one line without \\n", t_one_line_no_nl, MANDATORY},
	{"one character", t_one_char, MANDATORY},
	{"several lines, ends with \\n", t_lines_nl_end, MANDATORY},
	{"several lines, no final \\n", t_lines_no_nl_end, MANDATORY},
	{"empty lines (start / middle / end)", t_empty_lines_middle, MANDATORY},
	{"short lines, spaces, tabs", t_short_lines, MANDATORY},
	{"line lengths around BUFFER_SIZE", t_line_lengths, MANDATORY},
	{"20000-char lines", t_long_line, MANDATORY},
	{"2000 lines", t_many_lines, MANDATORY},
	{"keeps returning NULL after EOF", t_read_after_eof, MANDATORY},
	{"pipe with short / slow reads", t_pipe_chunks, MANDATORY},
	{"standard input (fd 0)", t_stdin, MANDATORY},
	{"directory fd / write-only fd", t_bad_fd_types, MANDATORY},
	{"same fd number reused after EOF", t_reuse_fd_after_eof, MANDATORY},
	{"read() fails in the middle of a file", t_read_error, MANDATORY},
	{"close() mid-file, reopen same fd", t_strict_close_mid_file, STRICT},
	{"read() error, then a new file on that fd", t_strict_after_read_error, STRICT},
#ifdef GNL_FAIL_MALLOC
	{"malloc fails (every call, one at a time)", t_malloc_fail, MANDATORY},
#endif
	{"bonus: 3 files interleaved", t_bonus_three_files, BONUS},
	{"bonus: long lines interleaved", t_bonus_long_lines, BONUS},
	{"bonus: file + pipe + stdin", t_bonus_file_pipe_stdin, BONUS},
	{"bonus: invalid fd in between", t_bonus_invalid_between, BONUS},
	{"bonus: huge fd numbers", t_bonus_huge_fd, BONUS},
	{"bonus: 40 fds at once", t_bonus_many_fds, BONUS},
};

typedef struct s_stats
{
	int	ok;
	int	ko;
	int	warn;
	int	crash;
	int	timeout;
}	t_stats;

static void	verdict(const t_test *t, const char *word, const char *color,
				const char *extra)
{
	printf("  %s%-8s%s %s%s%s\n", color, word, C_RESET, t->name,
		extra ? "  " : "", extra ? extra : "");
}

/*
** Print what a test wrote. An AddressSanitizer report is cut down to the
** error line, the first stack frames (file:line) and its summary.
*/
static void	print_child_output(char *out)
{
	char	*line;
	char	*next;
	int		frames = 0;

	if (!strstr(out, "AddressSanitizer"))
	{
		for (line = out; line && *line; line = next)
		{
			next = strchr(line, '\n');
			if (next)
				*next++ = '\0';
			printf("%s%s\n", line[0] == ' ' ? "" : "           ", line);
		}
		return ;
	}
	for (line = out; line && *line; line = next)
	{
		next = strchr(line, '\n');
		if (next)
			*next++ = '\0';
		if (strstr(line, "ERROR: AddressSanitizer")
			|| strstr(line, "SUMMARY: AddressSanitizer"))
			printf("           %s\n", strstr(line, "AddressSanitizer"));
		else if (strncmp(line, "    #", 5) == 0 && frames < 5)
		{
			frames++;
			printf("           %s\n", line + 4);
		}
		else if (strncmp(line, "         ", 9) == 0)
			printf("%s\n", line);
	}
}

/* seconds before a test counts as TIMEOUT: GNL_TIMEOUT=n overrides 10 */
static unsigned	timeout_s(void)
{
	const char	*env = getenv("GNL_TIMEOUT");

	if (env && atoi(env) > 0)
		return ((unsigned)atoi(env));
	return (TIMEOUT_S);
}

static void	run_forked(const t_test *t, t_stats *st, int quiet)
{
	int		p[2];
	pid_t	pid;
	int		status;
	char	*out = NULL;
	size_t	len = 0;
	char	buf[4096];
	ssize_t	r;

	if (pipe(p) < 0)
		exit(2);
	fflush(stdout);
	pid = fork();
	if (pid == 0)
	{
		close(p[0]);
		dup2(p[1], STDOUT_FILENO);
		dup2(p[1], STDERR_FILENO);
		close(p[1]);
		alarm(timeout_s());
		g_fail = 0;
		g_details = 0;
		t->fn();
		if (g_details > MAX_DETAILS)
			printf("         %s... %d more failed check(s)%s\n", C_DIM,
				g_details - MAX_DETAILS, C_RESET);
		fflush(stdout);
		_exit(g_fail ? 1 : 0);
	}
	close(p[1]);
	while ((r = read(p[0], buf, sizeof(buf))) > 0)
	{
		out = realloc(out, len + r + 1);
		memcpy(out + len, buf, r);
		len += r;
		out[len] = '\0';
	}
	close(p[0]);
	waitpid(pid, &status, 0);
	if (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM)
	{
		st->timeout++;
		snprintf(buf, sizeof(buf), "(still running after %u s: infinite loop or very slow)",
			timeout_s());
		verdict(t, "TIMEOUT", C_RED, buf);
	}
	else if (WIFSIGNALED(status))
	{
		st->crash++;
		verdict(t, "CRASH", C_RED, strsignal(WTERMSIG(status)));
	}
	else if (WEXITSTATUS(status) == 0)
	{
		st->ok++;
		if (!quiet)
			verdict(t, "OK", C_GREEN, NULL);
	}
	else if (t->kind == STRICT)
	{
		st->warn++;
		verdict(t, "WARN", C_YELLOW, "(strict check, not a hard requirement)");
	}
	else
	{
		st->ko++;
		verdict(t, "KO", C_RED, NULL);
	}
	if (out && (!quiet || WIFSIGNALED(status) || WEXITSTATUS(status)))
		print_child_output(out);
	free(out);
}

static void	run_in_process(const t_test *t, t_stats *st, int quiet)
{
	g_fail = 0;
	g_details = 0;
	t->fn();
	if (!g_fail)
	{
		st->ok++;
		if (!quiet)
			verdict(t, "OK", C_GREEN, NULL);
	}
	else if (t->kind == STRICT)
	{
		st->warn++;
		verdict(t, "WARN", C_YELLOW, "(details above)");
	}
	else
	{
		st->ko++;
		verdict(t, "KO", C_RED, "(details above)");
	}
}

static void	cleanup_tmpdir(void)
{
	DIR				*d;
	struct dirent	*e;
	char			path[512];

	d = opendir(g_tmpdir);
	if (!d)
		return ;
	while ((e = readdir(d)))
	{
		if (e->d_name[0] == '.')
			continue ;
		snprintf(path, sizeof(path), "%s/%s", g_tmpdir, e->d_name);
		unlink(path);
	}
	closedir(d);
	rmdir(g_tmpdir);
}

/* ./gnl_test file.txt  or  ./gnl_test -   : show every line, cat -e style */
static int	print_mode(const char *path)
{
	int		fd;
	char	*line;
	int		n = 0;
	size_t	i;

	fd = strcmp(path, "-") == 0 ? STDIN_FILENO : open(path, O_RDONLY);
	if (fd < 0)
	{
		perror(path);
		return (1);
	}
	printf("%s== %s (BUFFER_SIZE=%ld%s) ==%s\n", C_BOLD, path, BS_VALUE,
		BS_KNOWN ? "" : " ?", C_RESET);
	while ((line = get_next_line(fd)))
	{
		printf("%s[%3d]%s ", C_DIM, ++n, C_RESET);
		for (i = 0; line[i]; i++)
		{
			if (line[i] == '\n')
				printf("%s$%s", C_GREEN, C_RESET);
			else
				putchar(line[i]);
		}
		if (i == 0 || line[i - 1] != '\n')
			printf("%s   <- no \\n (last line)%s", C_YELLOW, C_RESET);
		putchar('\n');
		free(line);
	}
	printf("%s[END]%s get_next_line returned NULL after %d line(s)\n",
		C_DIM, C_RESET, n);
	if (fd != STDIN_FILENO)
		close(fd);
	return (0);
}

int	main(int argc, char **argv)
{
	int		bonus = 0;
	int		fork_mode = 1;
	int		quiet = 0;
	int		only_malloc = 0;
	int		i;
	size_t	k;
	t_stats	st = {0, 0, 0, 0, 0};

	g_color = isatty(STDOUT_FILENO);
	for (i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "bonus") == 0)
			bonus = 1;
		else if (strcmp(argv[i], "--no-fork") == 0)
			fork_mode = 0;
		else if (strcmp(argv[i], "-q") == 0)
			quiet = 1;
		else if (strcmp(argv[i], "malloc") == 0)
			only_malloc = 1;
		else
			return (print_mode(argv[i]));
	}
	snprintf(g_tmpdir, sizeof(g_tmpdir), "/tmp/gnl_test_XXXXXX");
	if (!mkdtemp(g_tmpdir))
	{
		perror("mkdtemp");
		return (2);
	}
	if (!quiet)
	{
		printf("%sget_next_line tester%s  BUFFER_SIZE = ", C_BOLD, C_RESET);
		if (BS_KNOWN)
			printf("%ld", BS_VALUE);
		else
			printf("not given with -D (student default; 42 assumed for sizes)");
		printf("  |  %s%s\n\n", bonus ? "mandatory + bonus" : "mandatory",
			fork_mode ? "" : "  |  --no-fork");
	}
	for (k = 0; k < sizeof(g_tests) / sizeof(*g_tests); k++)
	{
		t_test	t = g_tests[k];

		if (BS_KNOWN && BS_VALUE <= 0)
		{
			if (k > 0)
				break ;
			t.name = "BUFFER_SIZE <= 0 returns NULL";
			t.fn = t_bs_not_positive;
			t.kind = MANDATORY;
		}
		if (t.kind == BONUS && !bonus)
			continue ;
		if (only_malloc && strncmp(t.name, "malloc fails", 12) != 0)
			continue ;
		if (fork_mode)
			run_forked(&t, &st, quiet);
		else
			run_in_process(&t, &st, quiet);
	}
	cleanup_tmpdir();
	printf("%s%sBUFFER_SIZE=%-9ld%s  %s%d OK%s", quiet ? "" : "\n", C_BOLD,
		BS_VALUE, C_RESET, C_GREEN, st.ok, C_RESET);
	if (st.ko)
		printf("  %s%d KO%s", C_RED, st.ko, C_RESET);
	if (st.crash)
		printf("  %s%d CRASH%s", C_RED, st.crash, C_RESET);
	if (st.timeout)
		printf("  %s%d TIMEOUT%s", C_RED, st.timeout, C_RESET);
	if (st.warn)
		printf("  %s%d WARN%s", C_YELLOW, st.warn, C_RESET);
	printf("\n");
	return (st.ko || st.crash || st.timeout);
}

*This project has been created as part of the 42 curriculum by mnahli.*

# get_next_line

## Description

`get_next_line(int fd)` returns the next line read from a file descriptor, one line per
call, until there is nothing left (then it returns `NULL`). The returned line keeps its
`\n`, except the last line of a file that does not end with `\n`.

It works on any readable fd: regular files, pipes and the standard input. The goal of the
project is to learn **static variables**: a variable that keeps its value between two calls
of the same function, so the bytes read "too far" by one call are not lost for the next.

| File | Content |
|---|---|
| `get_next_line.c` | `get_next_line` + its 3 steps (`fill_stash`, `extract_line`, `trim_stash`) and `drop_stash` (free + NULL on every error path) |
| `get_next_line_utils.c` | `gnl_strlen`, `gnl_strchr`, `gnl_strjoin`, `gnl_substr` |
| `get_next_line.h` | prototypes, default `BUFFER_SIZE` (42) |
| `*_bonus.[ch]` | same algorithm, one stash **per fd** → several fds at the same time |

## Instructions

There is no Makefile (the subject does not ask for one). Compile with your own `main`:

```sh
cc -Wall -Wextra -Werror -D BUFFER_SIZE=42 get_next_line.c get_next_line_utils.c main.c
cc -Wall -Wextra -Werror -D BUFFER_SIZE=42 get_next_line_bonus.c get_next_line_utils_bonus.c main.c
```

`-D BUFFER_SIZE=n` is optional (default 42). Any value works, from 1 to 10000000 and above
`INT_MAX` (sizes go through `size_t` / `ssize_t`, the buffer is on the heap);
`BUFFER_SIZE <= 0` makes every call return `NULL`.

### Tests (`tests/` folder - not part of the submission)

```sh
tests/run.sh                  # everything (~4 min), see the list below
tests/run.sh /path/to/peer    # same checks on another student's repo
```

`run.sh` checks, in order:
- the required files exist, and norminette;
- with `nm` on the compiled code: only `read`, `malloc`, `free` are used, no global variable;
- 22 tests (20 mandatory + 2 strict) at 20 BUFFER_SIZEs from 1 to 10000000, without `-D`,
  and with 0 and -5 (must return NULL); 6 more multi-fd tests for the bonus;
- **malloc failure**: each malloc of a full read returns NULL once, in turn - no crash,
  no wrong line;
- **AddressSanitizer** at 7 sizes: buffer overflows, use-after-free, double free;
- **valgrind** (or `leaks` on macOS): real leaks only ("definitely lost"), on the normal
  build and on the malloc-failure build.

Every test runs in its own process: a segfault is reported as `CRASH`, an infinite loop as
`TIMEOUT`, and the other tests keep running. `WARN` = strict check (good practice, not a
hard rule of the subject): don't fail a peer on a WARN alone.

Evaluating someone else: clone this repo **outside** their folder, then point the script
at their folder. Nothing is written into their repo (binaries go to `/tmp`).

```sh
git clone https://github.com/na7li/get_next_line_1337 /tmp/gnl
cd /path/to/their/get_next_line && /tmp/gnl/tests/run.sh .
```

Or by hand, for one BUFFER_SIZE:

```sh
cc -Wall -Wextra -Werror -D BUFFER_SIZE=1 get_next_line.c get_next_line_utils.c tests/main.c -o gnl_test
./gnl_test                    # 22 tests (each one in its own process)
./gnl_test bonus              # + 6 multi-fd tests (compile the _bonus files instead)
./gnl_test some_file.txt      # print a file line by line, cat -e style
./gnl_test -                  # same, from stdin (try: ./gnl_test - < get_next_line.c)
valgrind --leak-check=full ./gnl_test --no-fork

# malloc failure: compile THEIR files with the hook, then link with the tester
cc -c -D BUFFER_SIZE=42 -include tests/fail_malloc.h get_next_line.c get_next_line_utils.c
cc -D BUFFER_SIZE=42 -D GNL_FAIL_MALLOC tests/main.c get_next_line.o get_next_line_utils.o -o gnl_malloc
./gnl_malloc malloc
```

Remove `tests/` (or keep it out of the repo) before pushing to the Vogsphere repository.

## Algorithm

One `static char *stash` holds the bytes that were read but not returned yet.
Every call does three steps:

1. **Read** - while the stash has no `\n` and `read()` returns more than 0, read
   `BUFFER_SIZE` bytes into a temporary buffer and append them to the stash. If the stash
   already contains a `\n`, nothing is read at all.
2. **Extract** - copy the stash up to and including the first `\n` (or all of it at the
   end of the file). An empty stash means end of file → free it and return `NULL`.
3. **Keep the rest** - replace the stash with what comes after the line (or `NULL` if
   nothing is left), and return the line.

Every error ends the same way: `return (drop_stash(&stash));` frees the stash, sets it to
`NULL` and returns `NULL`. That covers a failed `read()`, and a failed malloc at any of the
three places that allocate (the joined stash, the line, the rest). If the copy of the rest
fails, the line is freed too: returning it would silently lose the data after it.

Before that, `read(fd, NULL, 0)` asks the kernel whether `fd` can be read without reading
anything. If it fails (closed fd, write-only fd, directory), the stash is freed: old
leftovers can never come out of a file opened later on the same fd number.

**Why this algorithm**
- It reads *as little as possible*: it stops as soon as one `\n` is in memory, as the
  subject asks, instead of loading the whole file.
- It works for every `BUFFER_SIZE`: a line split across many reads (small buffer) is
  rebuilt by appending; many lines in one read (huge buffer) stay in the stash for the
  next calls.
- Memory is bounded: the stash never holds more than one line + one buffer, and it is
  freed at EOF or on error, so no leaks (checked with valgrind, also while every malloc
  fails in turn).
- `gnl_strjoin` always frees the old stash, so `stash = gnl_strjoin(stash, buf)` can never
  leak, even when malloc fails.

**Bonus**: the single static variable becomes an array `static char *stash[FD_MAX]`
(`FD_MAX` = 1024, the default Linux open-file limit). Each fd has its own slot, so reading
fd 3, then 4, then 3 again never mixes their leftovers. fds outside `[0, FD_MAX)` return
`NULL` instead of writing out of the array.

## Resources

- `man 2 read`, `man 3 malloc`, `man 2 open`
- [Static variables in C](https://en.wikipedia.org/wiki/Static_variable) - storage
  duration and lifetime
- [File descriptors](https://en.wikipedia.org/wiki/File_descriptor)
- The Norm v4 (42 intranet)

### Use of AI

Claude (Anthropic) was used to write the implementation, the test program in `tests/`
and an illustrated explanation of the algorithm (static variable, the stash, the three
steps, the bonus). Every line was then read, run under all the tests, and explained
by me so I can defend it without help.

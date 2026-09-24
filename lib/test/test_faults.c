/* GNU/ELF link wrapping injects allocation/read failures, without production
 * test hooks. Real PTYs and termios are used during server startup. */
#include "cgra.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size);
ssize_t __real_read(int fd, void *buf, size_t size);
ssize_t __wrap_read(int fd, void *buf, size_t size);
ssize_t __wrap___read_chk(int fd, void *buf, size_t size, size_t capacity);

static int fail_allocation, fail_read, read_calls, ready_calls, checks;
#define CHECK(cond, message) do { \
    int passed = (cond); \
    printf("%s fault %d - %s\n", passed ? "ok" : "not ok", ++checks, message); \
    assert(passed); \
} while (0)

void *__wrap_calloc(size_t count, size_t size)
{
    if (fail_allocation) { errno = ENOMEM; return NULL; }
    return __real_calloc(count, size);
}

ssize_t __wrap_read(int fd, void *buf, size_t size)
{
    if (fail_read) { errno = ++read_calls == 1 ? EINTR : EIO; return -1; }
    return __real_read(fd, buf, size);
}

/* Fortify can lower read to __read_chk, notably in the sanitizer profile.
 * Keep the bound check while routing both calls through the same fault. */
ssize_t __wrap___read_chk(int fd, void *buf, size_t size, size_t capacity)
{
    assert(size <= capacity);
    return __wrap_read(fd, buf, size);
}

static void ready(const char *path, void *user)
{
    (void)user;
    if (path && path[0]) ready_calls++;
}

int main(void)
{
    alarm(5);
    cgra_t *dev = cgra_open("sim:", 115200);
    CHECK(dev != NULL, "open before injecting allocation failures");
    int16_t h = 2, x = 3, out = 1234;
    fail_allocation = 1;
    CHECK(cgra_conv(dev, &h, 1, &x, 1, &out, 1) == CGRA_ERR_NOMEM && out == 1234,
          "workspace failure is distinct from invalid arguments and preserves output");
    cgra_stats_t stats;
    cgra_get_stats(dev, &stats);
    CHECK(stats.transactions == 0 && stats.tx_bytes == 0, "workspace failure performs no device I/O");
    errno = 0;
    CHECK(cgra_open("sim:", 115200) == NULL && errno == ENOMEM,
          "open allocation failure reports ENOMEM");
    CHECK(cgra_emulate_pty(ready, NULL) == CGRA_ERR_NOMEM && ready_calls == 0,
          "server allocation failure returns NOMEM before readiness callback");
    fail_allocation = 0;
    fail_read = 1;
    CHECK(cgra_emulate_pty(ready, NULL) == CGRA_ERR_IO && ready_calls == 1 && read_calls == 2,
          "server retries EINTR and reports the subsequent read failure");
    fail_read = 0;
    cgra_close(dev);
    printf("# %d injected fault checks passed\n", checks);
    return 0;
}

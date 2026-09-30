/*
 * serve.c — expose the in-process emulator on a pseudo-terminal.
 *
 * This lets the exact same protocol/PE model that backs cgra_open("sim:")
 * appear as a real serial device (/dev/pts/N), so minicom, pyserial, or a
 * second cgra process (`cgra -d /dev/pts/N ...`) can drive a virtual CGRA.
 */

#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE

#include "cgra.h"
#include "emu.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <poll.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/* Returns 1 for readiness, 0 for a requested stop, -1 for a system error. */
static int wait_io(int fd, short events, int stop_fd)
{
    for (;;) {
        struct pollfd pfd[2] = {{.fd = fd, .events = events},
                               {.fd = stop_fd, .events = POLLIN}};
        int rc = poll(pfd, 2, -1);
        if (rc < 0 && errno == EINTR) continue;
        if (rc < 0) return -1;
        if (pfd[1].revents & (POLLIN | POLLHUP)) return 0;
        if (pfd[1].revents & (POLLERR | POLLNVAL)) return -1;
        if (pfd[0].revents & events) return 1;
        return -1;
    }
}

int cgra_emulate_pty(void (*on_ready)(const char *path, void *user), void *user)
{
    const cgra_pty_options_t options = {CGRA_ROWS, CGRA_COLS, CGRA_PROTO_VER, -1};
    return cgra_emulate_pty_ex(&options, on_ready, user);
}

int cgra_emulate_pty_ex(const cgra_pty_options_t *options,
                       void (*on_ready)(const char *path, void *user), void *user)
{
    if (!options || !options->rows || !options->cols ||
        options->rows > CGRA_MAX_EDGE || options->cols > CGRA_MAX_EDGE ||
        options->rows * options->cols > CGRA_MAX_PE ||
        (options->version != 2 && options->version != 3) || options->stop_fd < -1 ||
        (options->stop_fd >= 0 && fcntl(options->stop_fd, F_GETFD) < 0))
        return CGRA_ERR_ARG;
    /* Copy borrowed options before entering the callback. */
    const cgra_pty_options_t opts = *options;
    int mfd = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
    if (mfd < 0) return CGRA_ERR_IO;
    int sfd = -1, result = CGRA_ERR_IO;
    cgra_emu_t *e = NULL;
    if (grantpt(mfd) != 0 || unlockpt(mfd) != 0) goto done;
    const char *sn = ptsname(mfd);
    if (!sn) goto done;
    /* Keep a raw slave open so closing a client does not terminate service. */
    sfd = open(sn, O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
    if (sfd < 0) goto done;
    struct termios tio;
    if (tcgetattr(sfd, &tio) != 0) goto done;
    cfmakeraw(&tio);
    if (tcsetattr(sfd, TCSANOW, &tio) != 0) goto done;
    e = emu_new_geometry(opts.rows, opts.cols);
    if (!e) { result = CGRA_ERR_NOMEM; goto done; }
    emu_set_legacy(e, opts.version == 2);
    if (on_ready) on_ready(sn, user);
    for (;;) {
        int ready = wait_io(mfd, POLLIN, opts.stop_fd);
        if (ready <= 0) { result = ready == 0 ? CGRA_OK : CGRA_ERR_IO; break; }
        uint8_t in[256];
        ssize_t k = read(mfd, in, sizeof(in));
        if (k < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (k <= 0) break;
        /* Drain after each input byte: batched CMD_RD/ID requests otherwise
         * overrun the model's output FIFO before emu_drain is called. */
        for (size_t i = 0; i < (size_t)k; i++) {
            emu_push(e, in[i]);
            uint8_t out[2 * CGRA_MAX_PE + 2];
            size_t n = emu_drain(e, out, sizeof(out)), off = 0;
            while (off < n) {
                ready = wait_io(mfd, POLLOUT, opts.stop_fd);
                if (ready <= 0) {
                    result = ready == 0 ? CGRA_OK : CGRA_ERR_IO;
                    goto done;
                }
                ssize_t w = write(mfd, out + off, n - off);
                if (w < 0 && (errno == EINTR || errno == EAGAIN)) continue;
                if (w <= 0) goto done;
                off += (size_t)w;
            }
        }
    }
 done:
    emu_free(e);
    if (sfd >= 0) close(sfd);
    close(mfd);
    return result;
}

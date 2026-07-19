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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

int cgra_emulate_pty(void (*on_ready)(const char *path, void *user), void *user)
{
    int mfd = posix_openpt(O_RDWR | O_NOCTTY);
    if (mfd < 0)
        return CGRA_ERR_IO;
    if (grantpt(mfd) != 0 || unlockpt(mfd) != 0) {
        close(mfd);
        return CGRA_ERR_IO;
    }
    const char *sn = ptsname(mfd);
    if (sn == NULL) {
        close(mfd);
        return CGRA_ERR_IO;
    }

    /* Hold the slave open in raw mode: keeps the line discipline byte-clean
     * and prevents the master read() from returning EIO between clients. */
    int sfd = open(sn, O_RDWR | O_NOCTTY);
    if (sfd < 0) {
        close(mfd);
        return CGRA_ERR_IO;
    }
    struct termios tio;
    if (tcgetattr(sfd, &tio) == 0) {
        cfmakeraw(&tio);
        tcsetattr(sfd, TCSANOW, &tio);
    }

    if (on_ready != NULL)
        on_ready(sn, user);

    cgra_emu_t *e = emu_new();
    if (e == NULL) {
        close(sfd);
        close(mfd);
        return CGRA_ERR_IO;
    }

    for (;;) {
        uint8_t in[256];
        ssize_t k = read(mfd, in, sizeof(in));
        if (k < 0)
            break;
        if (k == 0)
            continue;
        emu_feed(e, in, (size_t)k);

        uint8_t out[512];
        size_t n = emu_drain(e, out, sizeof(out));
        if (n > 0) {
            size_t off = 0;
            while (off < n) {
                ssize_t w = write(mfd, out + off, n - off);
                if (w <= 0) { emu_free(e); close(sfd); close(mfd); return CGRA_ERR_IO; }
                off += (size_t)w;
            }
        }
    }

    emu_free(e);
    close(sfd);
    close(mfd);
    return CGRA_OK;
}

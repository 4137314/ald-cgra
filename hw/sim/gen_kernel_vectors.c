/* Run the real C kernels over a PTY and record requests/replies for RTL replay.
 * A child uses libcgra's serial backend; the parent models the remote device.
 * The shared workload also checks every result against scalar CPU arithmetic. */
#define _XOPEN_SOURCE 600
#include "emu.h"
#include "../../sw/test/kernel_cases.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

static int write_all(int fd, const uint8_t *buf, size_t len)
{
    while (len) {
        ssize_t n = write(fd, buf, len);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 4) { fprintf(stderr, "usage: %s output-file rows cols\n", argv[0]); return 1; }
    unsigned rows = (unsigned)atoi(argv[2]), cols = (unsigned)atoi(argv[3]);
    cgra_emu_t *emu = emu_new_geometry(rows, cols);
    if (!emu) return 1;
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) || unlockpt(master)) { perror("PTY"); return 1; }
    char *path = ptsname(master);
    /* Keep the slave open while the child starts/stops to avoid master EIO. */
    int slave = path ? open(path, O_RDWR | O_NOCTTY) : -1;
    if (slave < 0) { perror("PTY slave"); return 1; }
    FILE *out = fopen(argv[1], "w");
    if (!out) { perror(argv[1]); return 1; }
    pid_t child = fork();
    if (child < 0) { perror("fork"); return 1; }
    if (child == 0) {
        alarm(30);
        close(master);
        close(slave);
        fclose(out);
        emu_free(emu);
        cgra_t *dev = cgra_open(path, 115200);
        if (!dev) _exit(1);
        int rc = kernel_cases(dev);
        cgra_close(dev);
        _exit(rc);
    }
    uint8_t request[256], reply[256], input[256];
    size_t used = 0, transactions = 0;
    int status = 0, failed = 0, reaped = 0;
    /* Child alarm bounds the workload; select lets us notice its exit. */
    while (!failed) {
        fd_set set;
        FD_ZERO(&set); FD_SET(master, &set);
        struct timeval timeout = {0, 100000};
        int ready = select(master + 1, &set, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            failed = 1; break;
        }
        if (ready) {
            ssize_t nr = read(master, input, sizeof(input));
            if (nr <= 0) { failed = 1; break; }
            for (ssize_t i = 0; i < nr; i++) {
                if (used == sizeof(request)) { failed = 1; break; }
                request[used++] = input[i];
                emu_push(emu, input[i]);
                size_t count = emu_drain(emu, reply, sizeof(reply));
                if (!count) continue;
                fprintf(out, "%zu %zu", used, count);
                for (size_t j = 0; j < used; j++) fprintf(out, " %u", request[j]);
                for (size_t j = 0; j < count; j++) fprintf(out, " %u", reply[j]);
                fputc('\n', out);
                used = 0;
                transactions++;
                if (write_all(master, reply, count)) { failed = 1; break; }
            }
        }
        pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child) { reaped = 1; break; }
        if (done < 0 && errno != EINTR) { failed = 1; break; }
    }
    if (!reaped) {
        kill(child, SIGKILL);
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) || used || !transactions) failed = 1;
    if (!failed) fputs("0 0\n", out);
    if (fclose(out)) failed = 1;
    close(slave); close(master); emu_free(emu);
    if (failed) { unlink(argv[1]); fprintf(stderr, "kernel transcript failed\n"); return 1; }
    printf("kernel transcript: %ux%u, %zu transactions, scalar references passed\n", rows, cols, transactions);
    return 0;
}

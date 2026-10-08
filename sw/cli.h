/* Private CLI contracts. These declarations are not part of libcgra's ABI. */
#ifndef CGRA_CLI_H
#define CGRA_CLI_H
#include <stdio.h>
#include <stdint.h>
#include "cgra.h"
#include "dsl.h"

#define MAX_VEC 4096

typedef struct {
    FILE *out;                 /* Borrowed staged stream; NULL means stdout. */
    int unsigned_output;
    const char *format;        /* Borrowed for this invocation only. */
    int report_ready;          /* Complete reports can contain failed cases. */
} cli_context;

typedef struct {
    char port[DSL_VAL];
    int baud, timeout_ms;
} connection_info;

int cli_dispatch(int argc, char **argv, int in_shell);
void cli_usage(FILE *out);
int cli_cmd_shell(char *config);
int cli_parse_ints(const char *text, int16_t *out, size_t capacity, size_t *count);
int cli_read_vec(const char *source, int16_t *out, size_t capacity, size_t *count);
void cli_print_vec(cli_context *inv, const int16_t *values, size_t count, const dsl_io *io);
cgra_t *cli_open_device_resolved(dsl_ctx *dsl, const char *name, char *error,
                               size_t error_size, connection_info *details);
cgra_t *cli_open_device(dsl_ctx *dsl, const char *name, char *error, size_t error_size);
int cli_mkdir_p(const char *path);
int cli_cmd_emulate(unsigned rows, unsigned cols, unsigned version, unsigned flags);
int cli_cmd_probe(dsl_ctx *dsl, const char *device);
int cli_selftest(void);
int cli_cmd_bench(cli_context *inv, dsl_ctx *dsl, const char *device, const char *mode,
                  int size, int repeat);
int cli_cmd_benchall(cli_context *inv, dsl_ctx *dsl, const char *device, int size, int repeat);
#endif

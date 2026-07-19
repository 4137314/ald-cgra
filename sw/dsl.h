/*
 * dsl.h — the custom CGRA configuration language (model + parser + discovery).
 *
 * A ".cgra" file is a sequence of INI-flavoured, named sections:
 *
 *   [device NAME]  port/baud/timeout/board          — connection profiles
 *   [mode NAME]    pattern/op/a/b/steps/out or pe .. — array configurations
 *   [pipeline N]   stage = MODE [imm=K] ...          — multi-stage kernels
 *   [io NAME]      format/width/sep                  — data binding profiles
 *
 * Files are merged by name across the whole discovery chain (built-in
 * defaults < /etc/cgra < ~/.config/cgra < project-local < CLI), so a later
 * file overrides individual fields of an entity defined earlier.
 */

#ifndef CGRA_DSL_H
#define CGRA_DSL_H

#include <stddef.h>
#include "cgra.h"

#define DSL_NAME 48
#define DSL_VAL  192
#define DSL_TOK  32

#define DSL_MAX_DEV    16
#define DSL_MAX_MODE   64
#define DSL_MAX_PIPE   32
#define DSL_MAX_IO     16
#define DSL_MAX_STAGE  16
#define DSL_MAX_PATHS  32
#define DSL_MAX_VARS   64

typedef struct {
    char name[DSL_NAME];
    char port[DSL_VAL];
    int  baud;
    int  timeout_ms;
    char board[DSL_NAME];
} dsl_device;

typedef struct {
    int  r, c;
    char op[DSL_TOK];   /* opcode word, e.g. "mac" */
    char a[DSL_TOK];    /* operand source, e.g. "north" or "const(1)" */
    char b[DSL_TOK];
} dsl_pe;

typedef struct {
    char name[DSL_NAME];
    char doc[DSL_VAL];
    char pattern[DSL_TOK];  /* diagonal | reduce | custom */
    char op[DSL_TOK];       /* for diagonal/reduce */
    char a[DSL_TOK];        /* operand source (default north) */
    char b[DSL_TOK];        /* operand source (default west)  */
    char steps[DSL_TOK];    /* rows | auto | <int> */
    char out[DSL_TOK];      /* diag | pe R,C (default depends on pattern) */
    char reset[DSL_TOK];    /* each | once (clear PE registers between chunks) */
    dsl_pe pe[CGRA_NUM_PE];
    int  npe;
} dsl_mode;

typedef struct {
    char mode[DSL_NAME];
    long imm;
    int  has_imm;
} dsl_stage;

typedef struct {
    char name[DSL_NAME];
    char doc[DSL_VAL];
    dsl_stage stage[DSL_MAX_STAGE];
    int  nstage;
} dsl_pipeline;

typedef struct {
    char name[DSL_NAME];
    char format[DSL_TOK];   /* dec | hex | bin */
    int  width;
    char sep[DSL_TOK];      /* ws | comma | newline */
} dsl_io;

typedef struct {
    char name[DSL_NAME];
    char val[DSL_VAL];
} dsl_var;

typedef struct {
    dsl_device   dev[DSL_MAX_DEV];    int ndev;
    dsl_mode     mode[DSL_MAX_MODE];  int nmode;
    dsl_pipeline pipe[DSL_MAX_PIPE];  int npipe;
    dsl_io       io[DSL_MAX_IO];      int nio;
    dsl_var      var[DSL_MAX_VARS];   int nvar;

    /* config files that were actually loaded, for `cgra config` */
    char paths[DSL_MAX_PATHS][DSL_VAL];
    int  npath;
} dsl_ctx;

/* Reset to empty. */
void dsl_init(dsl_ctx *d);

/* Parse one buffer / file into d (merging by name). Returns 0 or -1;
 * on error, err (if non-NULL) receives a message. `origin` labels the
 * source in error messages. */
int dsl_parse(dsl_ctx *d, const char *text, const char *origin,
              char *err, size_t errsz);
int dsl_parse_file(dsl_ctx *d, const char *path, char *err, size_t errsz);

/* Parse the compiled-in default configuration. */
int dsl_load_defaults(dsl_ctx *d, char *err, size_t errsz);

/* Full discovery: defaults < /etc/cgra < user config dir < project-local.
 * extra, if non-NULL, is an extra file parsed last (from --config). */
int dsl_load(dsl_ctx *d, const char *extra, char *err, size_t errsz);

/* The default configuration text (also written out by `cgra init`). */
const char *dsl_default_text(void);

/* Resolved user config directory ($XDG_CONFIG_HOME/cgra or ~/.config/cgra).
 * Returns 0 and fills buf, or -1 if HOME is unset. */
int dsl_user_dir(char *buf, size_t bufsz);

/* Lookups (NULL if not found). */
dsl_device   *dsl_find_device(dsl_ctx *d, const char *name);
dsl_mode     *dsl_find_mode(dsl_ctx *d, const char *name);
dsl_pipeline *dsl_find_pipeline(dsl_ctx *d, const char *name);
dsl_io       *dsl_find_io(dsl_ctx *d, const char *name);

#endif /* CGRA_DSL_H */

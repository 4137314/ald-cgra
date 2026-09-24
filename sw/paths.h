#ifndef CGRA_PATHS_H
#define CGRA_PATHS_H

/* sw.mk supplies the configured path via a generated header. Standalone
 * compiler/RTL test builds use the standard installation prefix. DESTDIR is
 * a staging directory and must never appear in the runtime path. */
#ifndef CGRA_STDLIB_DIR
#define CGRA_STDLIB_DIR "/usr/local/share/cgra/stdlib"
#endif

#endif

/* The process entry point; each shell command gets a fresh invocation. */
#include "cli.h"

int main(int argc, char **argv)
{
    return cli_dispatch(argc, argv, 0);
}

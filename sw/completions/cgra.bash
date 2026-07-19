# bash completion for the cgra CLI.
#   source sw/completions/cgra.bash    (or install to /etc/bash_completion.d/)
# Completes subcommands, options, and — via `cgra modes/devices` — the live
# names of modes and device profiles from your configuration.

_cgra() {
    local cur prev words cword
    _init_completion 2>/dev/null || {
        cur="${COMP_WORDS[COMP_CWORD]}"
        prev="${COMP_WORDS[COMP_CWORD-1]}"
    }

    local cmds="modes devices pipelines io config check init ping reset dump \
                probe show run pipe matvec conv scan bench emulate selftest"
    local opts="-d --device -c --config --a --b -m --matrix --x --cols --io \
                -o --out --json --dtype --op --size --repeat --imm --steps \
                -v --verbose --force -h --help"

    # first word: the subcommand
    if [[ $COMP_CWORD -eq 1 ]]; then
        COMPREPLY=( $(compgen -W "$cmds" -- "$cur") )
        return
    fi

    case "$prev" in
        -d|--device)
            COMPREPLY=( $(compgen -W "auto sim $(cgra devices 2>/dev/null | awk '{print $1}')" -- "$cur") )
            return ;;
        --io)
            COMPREPLY=( $(compgen -W "$(cgra io 2>/dev/null | awk '{print $1}')" -- "$cur") )
            return ;;
        --dtype)
            COMPREPLY=( $(compgen -W "s16 u16" -- "$cur") )
            return ;;
        --op)
            COMPREPLY=( $(compgen -W "sum max min prod" -- "$cur") )
            return ;;
        -c|--config|-o|--out)
            COMPREPLY=( $(compgen -f -- "$cur") )
            return ;;
    esac

    # second word after run/show/pipe/bench: a mode / pipeline name
    if [[ $COMP_CWORD -eq 2 ]]; then
        case "${COMP_WORDS[1]}" in
            run|show|bench)
                COMPREPLY=( $(compgen -W "$(cgra modes 2>/dev/null | awk '{print $1}')" -- "$cur") )
                return ;;
            pipe)
                COMPREPLY=( $(compgen -W "$(cgra pipelines 2>/dev/null | awk 'NF>1 && $1!~/^-/{print $1}')" -- "$cur") )
                return ;;
        esac
    fi

    COMPREPLY=( $(compgen -W "$opts" -- "$cur") )
}
complete -F _cgra cgra

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
                probe show run pipe matvec conv scan block-dot bench benchall emulate selftest shell"
    local opts="-h --help --version" cmd="" word skip=0
    # Find the command even when global options precede it. Option values are
    # not commands (a device profile may itself be named 'run', for example).
    for word in "${COMP_WORDS[@]:1:COMP_CWORD-1}"; do
        if (( skip )); then skip=0; continue; fi
        case "$word" in
            -d|--device|-c|--config|-m|--matrix|-o|--out|--a|--b|--x|--rows|--protocol|--cols|--io|--dtype|--op|--size|--repeat|--imm|--steps)
                skip=1 ;;
            -*) ;;
            *) cmd="$word"; break ;;
        esac
    done
    case "$cmd" in
        modes|devices|pipelines|io|config|check) opts+=" -c --config -o --out" ;;
        ping) opts+=" -c --config -d --device -o --out" ;;
        reset|probe) opts+=" -c --config -d --device" ;;
        dump|run|pipe|matvec|conv|scan|block-dot)
            opts+=" -c --config -d --device -o --out --io --json --dtype"
            case "$cmd" in
                run) opts+=" --a --b --imm --steps" ;;
                pipe) opts+=" --a --b" ;;
                matvec) opts+=" --a --b -m --matrix --x --cols" ;;
                conv) opts+=" --a --b -m --matrix --x" ;;
                block-dot) opts+=" --a --b --repeat" ;;
                scan) opts+=" --a --op" ;;
            esac ;;
        show) opts+=" -c --config -d --device -o --out --imm -v --verbose" ;;
        bench|benchall) opts+=" -c --config -d --device -o --out --json --size --repeat" ;;
        init) opts+=" --force" ;;
        emulate) opts+=" --rows --cols --protocol --blocks" ;;
        shell) opts+=" -c --config" ;;
        "") opts+=" -c --config -d --device" ;;
    esac

    # first word: the subcommand
    if [[ -z "$cmd" && $cur != -* && $prev != -c && $prev != --config && $prev != -d && $prev != --device ]]; then
        COMPREPLY=( $(compgen -W "$cmds" -- "$cur") )
        return
    fi

    case "$prev" in
        -d|--device)
            COMPREPLY=( $(compgen -W "auto sim sim:blocks sim:flaky sim:v2 $(cgra devices 2>/dev/null | awk '{print $1}')" -- "$cur") )
            return ;;
        --io)
            COMPREPLY=( $(compgen -W "$(cgra io 2>/dev/null | awk '{print $1}')" -- "$cur") )
            return ;;
        --protocol)
            COMPREPLY=( $(compgen -W "2 3" -- "$cur") )
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

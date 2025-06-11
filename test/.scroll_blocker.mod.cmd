savedcmd_scroll_blocker.mod := printf '%s\n'   scroll_blocker.o | awk '!x[$$0]++ { print("./"$$0) }' > scroll_blocker.mod

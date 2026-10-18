#pragma once

/*
 * platform_posix.hpp - POSIX half of the Havel platform shim (PLAT-001).
 *
 * This is deliberately just the header set the tree already includes at its
 * POSIX call sites. It adds no behaviour and introduces no new names, so a
 * Linux translation unit that swaps a bare <unistd.h> for this header sees
 * an identical macro environment.
 *
 * Note for future ports: <dlfcn.h> is NOT here on purpose. dlopen is C++ and
 * gets a real abstraction in dl.hpp; only dlerror-style C use needs the raw
 * header, and that stays at the call site.
 */

#include "havel_platform.h"

#if HAVEL_PLATFORM_POSIX

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#endif  // HAVEL_PLATFORM_POSIX

#!/usr/bin/env python3
# Runs a command and reports wall time plus CPU time (user+sys) consumed
# by it and its children. CPU time is the honest metric on this shared
# 4-core box (loadavg often 15+), where wall time is scheduler noise.
#
# Usage: rusage_run.py <command...>
# Output (stdout of child passes through; final line):
#   RUSAGE: wall=<s> cpu_user=<s> cpu_sys=<s> cpu_total=<s>
import resource
import subprocess
import sys
import time

cmd = sys.argv[1:]
before = resource.getrusage(resource.RUSAGE_CHILDREN)
t0 = time.monotonic()
rc = subprocess.call(cmd)
wall = time.monotonic() - t0
after = resource.getrusage(resource.RUSAGE_CHILDREN)
user = after.ru_utime - before.ru_utime
sys_t = after.ru_stime - before.ru_stime
print(
    f"RUSAGE: wall={wall:.2f} cpu_user={user:.2f} "
    f"cpu_sys={sys_t:.2f} cpu_total={user + sys_t:.2f} rc={rc}",
    file=sys.stderr,
)
sys.exit(rc)

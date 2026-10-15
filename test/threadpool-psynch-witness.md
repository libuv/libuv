# Darwin threadpool wakeup oracle

This manual test reproduces the half-range psynch condition-variable failure
reported in #5280. It uses the production worker and queue implementation with
controlled worker enrollment. It does not write pthread counters or block a
worker on filesystem I/O.

Build assertions enabled so the pool's mutex matches Node's ERRORCHECK pairing:

```sh
cmake -S . -B build -DLIBUV_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  '-DCMAKE_C_FLAGS_RELWITHDEBINFO=-O2 -g'
cmake --build build --parallel 2
cc -O2 -UNDEBUG -Iinclude -Isrc -Itest \
  test/threadpool-psynch-witness.c build/libuv.a -lpthread \
  -o build/threadpool-psynch-witness
build/threadpool-psynch-witness 1024
build/threadpool-psynch-witness 8388605
```

The short control must complete four jobs and return 0. On an unpatched affected
Darwin host, the boundary run returns 10: all four workers are parked, work
remains queued, callbacks remain at zero, L equals U, and the oldest wait's L is
exactly `0x80000000` behind the condition's current L. The patched run refreshes
the old wait during aging, completes all four jobs, and exits normally.

Expect about a minute per boundary run on Apple silicon. The six-hundred-second
alarm is only an outer safety limit; an observation deadline alone is not the
failure oracle. Inspect the emitted queue, generation and callback evidence.

This witness controls condition aging. A later ordinary sequential-workload
reproduction also reached the same state, as described below. The exact
application job-producer sequence remains unproven.

The regular `threadpool_idle_wait_refresh` test separately checks the shipping
pool's idle-wait policy without controlled enrollment or reading private pthread
structure fields. It observes actual psynch waits through Mach thread state,
requires every idle worker to change its wait generation, then runs fresh work.

## Ordinary sequential workload

A Node 24.21.0/libuv 1.52.1 arm64 run on macOS 26.3.1 also reproduced this without
condition aging or enrollment control. Submit one empty `uv_queue_work`, then
submit its successor from the completion callback, keeping one job outstanding
for the default four-worker pool. One run stalled at 11,032,951 completions;
another with a real PTY stalled at 11,886,884 completions:

```text
L = 3043043584; oldest worker wait L = 895559936
L - oldest wait L = 0x80000000
four idle workers; queued work
```

After four fresh async stats and a PTY write, fifteen seconds produced no stat
callbacks or PTY echo. Six jobs remained queued, `L == U`, and workers remained
parked. Read-only snapshots showed the oldest worker staying at generation
895559936 while newer generations advanced through millions of jobs. Another
30-million-job run completed, so the scheduling is intermittent.

The downstream opt-in Node/PTY workload is in Orca PR #29133:
https://github.com/stablyai/orca/pull/29133
It uses a small N-API addon to run the sequential jobs; it writes no private
pthread counters and performs no condition aging. The downstream diagnostic
snapshot that established the half-range relationship is separate from that
portable workload. This does not identify which application job producers or
host scheduling originally led to the production incident.

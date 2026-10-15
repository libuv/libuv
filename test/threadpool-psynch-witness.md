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

This is a controlled condition-aging reproduction, not ordinary application
startup or normal work/signal cycling. Why the application's workload left one
worker parked for so many subsequent wait generations is still unproven.

The regular `threadpool_idle_wait_refresh` test separately checks the shipping
pool's idle-wait policy without controlled enrollment or reading private pthread
structure fields. It observes actual psynch waits through Mach thread state,
requires every idle worker to change its wait generation, then runs fresh work.

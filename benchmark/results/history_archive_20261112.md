# Historical Archive Validation

Local validation on 2026-11-12, using a rebuilt PSOL/module and an isolated,
unprivileged Nginx 1.31.8 with two workers. The installed service was not changed.

## Correctness

- 268 selected `pagespeed_automatic_test` tests passed, including archive,
  message, admin, cache-cleaner, HTML-cache and proxy regression tests.
- 13 `SystemRewriteOptionsTest` tests passed.
- 10 isolated Nginx integration tests passed, including gzip, private/304
  responses, concurrent historical reads, worker deduplication and master
  restart/checkpoint recovery.
- Headless Chrome rendered all five archived views, including the existing
  JavaScript-created message controls, without reported uncaught exceptions.
  This was a rendering smoke test, not a full browser interaction suite.

## Enabled Versus Disabled

Raw results: `history_archive_20261112.json`.

Three alternating off/on pairs, ten measured seconds per run, eight keepalive
clients, 64 warm HTML-cache URL variants, 100KB live buffer and **one-second**
snapshots. Both configurations use the same binary and otherwise identical
configuration. Medians:

| Metric | Disabled | Enabled | Change |
| --- | ---: | ---: | ---: |
| Requests/second | 3,476.1 | 3,544.5 | +1.97% |
| p95 latency | 4.562 ms | 4.513 ms | -1.06% |
| p99 latency | 6.275 ms | 6.325 ms | +0.80% |
| Nginx CPU, 100% = one core | 230.77% | 234.37% | +1.56% relative |
| Summed peak RSS | 85.57 MiB | 86.14 MiB | +584 KiB |
| Physical disk reads/10 seconds | 0 | 0 | Warm filesystem cache |
| Physical disk writes/10 seconds | 0 | 972 KiB | Background snapshot writes |

All measured HTTP responses succeeded. These short runs do not establish a
speed improvement or statistical significance. There was no large observed
throughput or tail-latency regression on this workload; CPU, memory and disk
overhead are real. Summed RSS double-counts shared pages. The Python loopback
load generator and server run on the same machine.

The enabled median archived 6,379 existing log lines; the tiny live buffer
overwrote approximately 16.8 million source bytes between snapshots. Those gaps
were detected and reported, not recovered or presented as complete history.
Five-minute snapshots require sizing the existing buffer for the actual logging
rate, or accepting/reporting gaps. Archival does not create an extra event queue.

This exercises warm HTML-cache traffic, not cold image conversion or a real
production trace. The default 300-second cadence was not benchmarked; reproduce
it with `--interval 300 --duration 360`. Do not linearly extrapolate short-run
latency or physical I/O to a production filesystem.

## Populated Directory Smoke Test

`history_archive_density_20261112.json` contains one additional off/on pair with
2,016 pre-created empty segments (seven days of five-minute segment names).
These are synthetic directory entries, not fabricated resource events.
Throughput was 3,785 versus 3,983 requests/second, p99 was 6.200 versus 5.920 ms,
and Nginx CPU was 233.66% versus 236.46%. All responses succeeded. The enabled
archive occupied about 8.94 MiB of allocated blocks. This checks bounded
enumeration with a populated directory, not historical query throughput,
long-term retention or statistical significance.

# Unified Historical Archive

## Source Audit

There is one physical history source, not five independent event buffers:

| View | Existing producer and reader |
| --- | --- |
| Message History | `SystemMessageHandler::AddMessageToBuffer`, `SharedCircularBuffer`, `CircularBuffer`; `AdminSite::MessageHistoryHandler` |
| Images | `ImageRewriteFilter::RewriteLoadedResourceImpl` emits `PS_IMAGE_DETAIL` into that buffer; `ReadRecentImageRewrites` parses it |
| HTML cache | `HtmlPageCacheWriter::Finish` emits `PS_HTML_DETAIL` into that buffer; `ReadRecentHtmlRewrites` parses it |
| JavaScript | Existing diagnostic messages from `javascript_filter.cc` and `javascript_code_block.cc`; no structured successful-rewrite event history |
| CSS | Existing diagnostic messages from `css_filter.cc`; no structured successful-rewrite event history |

`StatisticsLogger` already persists selected aggregate counters for Console and
Graphs, including JavaScript/CSS metrics. It is unchanged and is not copied into
this archive. Histograms and counters are aggregates, not additional event
history buffers. Resource/metadata/property caches are not historical archives.

## Configuration

Set these process-wide options in Nginx's `http` block:

```nginx
pagespeed FileCachePath /var/cache/ngx_pagespeed;
pagespeed MessageBufferSize 100000;
pagespeed HistoryArchive on;                 # Default off
pagespeed HistoryArchiveInterval 300;        # Seconds; default 300
pagespeed HistoryArchiveRetentionDays 7;     # Default 7
pagespeed HistoryArchiveMaxSizeMb 100;        # MiB; default 100
```

All server contexts in an instance must share one FileCachePath. Independent
Nginx master instances must not share an archive directory. An ambiguous path
configuration disables archival and reports an error without disabling rewrites.
A positive MessageBufferSize is required. Archival snapshots support buffers up
to 16 MiB; larger buffers continue logging normally but archival reports an
error. Supported ranges are 1..86400 seconds, 1..3650 days, and 1..102400 MiB.
Invalid archival settings fail the archive, not the HTTP service.

Rebuild complete PSOL and the module with matching headers, then restart Nginx.
The optional shared-memory metadata and virtual interfaces change; do not swap
only the library into an older module/process.

## One Store, One Active Writer

```
<FileCachePath>/history/
    .writer.lock
    checkpoint
    segment-00000000000000000001.psh
    segment-00000000000000000002.psh
    ...
```

One elected worker runs the active background snapshot scheduler and holds a
nonblocking, process-lifetime `flock`. Other workers have passive timed election
waiters, not independent snapshot writers. This permits failover after a leader
exits, with up to one interval of delay. Threads start only in child processes,
not before the server forks. Disk I/O occurs in this dedicated background work,
not in normal HTTP processing or the rewrite worker pool. Archive admin queries
are queued onto the same local background thread; neither the Nginx event loop
nor the rewrite worker pool performs their disk reads. The queue allows four
pending queries per worker and rejects excess requests with HTTP 503. Queued
requests own copies of their options/query data and drain before shutdown.

The disabled path creates no archive object, thread, timer, directory, sequence
segment or files. Existing message formatting, live-buffer bytes and event
collection remain unchanged. When enabled, the existing shared-buffer write
updates one byte-position counter under its existing lock. It does not emit an
additional event, allocate another event buffer or write to disk.

A consistent snapshot copies existing bytes and the generation/position under
the shared-memory mutex, releases that mutex, and only then does persistence.
Each existing newline-delimited history entry has identity
`(shared-memory generation, original byte position)`, including identical text
written twice at the same timestamp. Multiline continuation bytes are preserved.
Content hashes and timestamps are not used as deduplication keys.

Immutable segments hold a small `PSH1` cursor/health header followed by unchanged
message bytes. A single atomic checkpoint applies to all views. Most segments
are at most 256 KiB plus a header; a longer existing line may require a larger
segment, bounded by the snapshot limit and storage budget. Data files and the
checkpoint are written privately, fsynced and atomically renamed, with directory
fsync. A committed segment can recover a checkpoint update interrupted by a
crash. Abandoned `.pending` data is never treated as committed history.

Normal PageSpeed file-cache cleanup explicitly excludes the entire `history/`
subtree from traversal, resource-cache accounting, directory removal and eviction.
Archive retention uses segment creation mtime. The combined limit accounts for
allocated filesystem blocks, directory/control-file space and temporary-write
reservation, not merely JSON/text payload length. Oldest segments are removed
first. Statistics logfiles, cached resources and metadata are not altered.
Retention resumes only while archival is enabled; turning archival off leaves
existing files alone and performs no archive filesystem work.

## Gaps And Failure Handling

Periodic snapshots cannot guarantee complete history. A busy buffer can wrap
before five minutes elapse. Missing byte ranges and discarded partial leading
lines are counted; the types and exact number of overwritten records are
unknown. A fresh shared-memory generation adds a restart/reset uncertainty gap:
the unsaved tail of the previous generation cannot be reconstructed. These are
reported, not presented as complete history. Increase the existing buffer size
or shorten the interval if necessary, within the configured bounds.

Permission errors, full disks, failed writes and invalid metadata do not clear
the live buffer or advance an uncommitted cursor. Health is kept in shared
memory and successful checkpoints preserve cumulative counts/gaps. An error
may itself be impossible to persist on a full disk. The scheduler retries later;
records overwritten before recovery remain unrecoverable and are reported.
Corrupt committed metadata stops archival rather than guessing a cursor.
Directory enumeration is bounded to 65,536 archive segments; exceeding this
limit reports an error instead of allocating unlimited memory. Use a suitable
interval/retention/storage budget, not this as a general-purpose logging system.
Use a local POSIX filesystem with working advisory locks and fsync semantics.

Only archiver-generated names are read or removed. Archive files are not public
web assets; keep FileCachePath outside the document root and retain existing
admin access controls. Original diagnostics can contain sensitive URLs or error
text. Directory mode is 0700 and new archive files are 0600.

## Admin Views

Message History, Images, HTML Cache, JavaScript and CSS retain their existing
interfaces, parsing, filters, comparison behavior and tables. When enabled,
each history request combines a consistent current shared-memory snapshot with
retained archive records. Overlap is removed using the shared-memory generation
and original byte positions, not timestamps or message contents. The newest
live buffer entries remain visible if the archive checkpoint is missing or
unavailable. Existing view filters operate on the combined records; date range
and cursor controls page into older archived records without a source-mode
switch. JavaScript and CSS include only existing diagnostic messages.

JavaScript and CSS pages display **existing diagnostic messages only**. Their
classification recognizes known message wording/source filenames; Message
History remains the authoritative unfiltered record. No successful-rewrite
inventories, per-resource size fields or optimization events are invented.

Each archival page returns up to 100 matching records. Each scan examines at
most 256 segments and 16 MiB of segment data, one bounded segment at a time,
and carries a continuation cursor even when a scan window has no matches. A
single segment may bring the read total up to one segment beyond that budget.
It does not load the entire archive into memory. Dates use original message
timestamps; continuation lines inherit the preceding timestamp when available,
including across chunks.
Entries whose timestamps were lost with an overwritten prefix are available in
the unfiltered archive but not in date-filtered results.

`/pagespeed_admin/history` reports archive status, interval, retention, storage
limit, last successful and next scheduled snapshot, allocated disk usage at the
last successful accounting, cumulative archived record and per-view match
counts, missing source bytes, uncertain generation gaps and archive failures.
Counts are cumulative, not an inventory of retained records. Per-view matches
are classifications of a single stored record; their sum can exceed the number
of archived records. The main dashboard retains the performance overview and a
compact link to archive health.

## Validation And Benchmarking

C++ archive tests are included with `SystemMessageHandlerTest` in
`pagespeed_automatic_test` as `HistoryArchiveTest.*`. They cover all five views,
identical-content events, incremental deduplication, overwrites, generation
changes, interrupted checkpoint updates, partial temporary files, permission /
ENOSPC failures, writer election, pagination, date filtering, retention and size
limits. Cache-cleaner and option-registration tests cover integration boundaries.

Run isolated two-worker Nginx integration tests:

```bash
python3 test/integration/history_archive_nginx_test.py \
  --nginx /path/to/compatible/nginx --module /path/to/ngx_pagespeed.so -v
```

Measure throughput, p95/p99, Nginx CPU, sampled RSS and physical disk I/O:

```bash
python3 benchmark/history_archive_benchmark.py \
  --nginx /path/to/compatible/nginx --module /path/to/ngx_pagespeed.so \
  --interval 1 --duration 10 --repeats 3 --output /tmp/history-archive-bench.json
```

This accelerated stress test is not the production five-minute cadence. For the
actual cadence, use `--interval 300 --duration 360`. Snapshotting has background
CPU, memory, lock-copy and disk costs; do not claim zero overhead or extrapolate
a short loopback benchmark to production. Preserve reports and evaluate the
measured differences and run-to-run noise on the intended workload.

An isolated local validation report and raw enabled/disabled measurements are
in `benchmark/results/history_archive_20261112.md`. That report explicitly
records observed buffer loss and the limits of the synthetic benchmark.

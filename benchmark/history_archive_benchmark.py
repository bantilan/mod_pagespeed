#!/usr/bin/env python3
"""Compare isolated Nginx with archiving off/on; no installed service is changed."""
import argparse
import concurrent.futures
import http.client
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import stat
import statistics
import sys
import threading
import time
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('html_cache_fixture',
    Path(__file__).resolve().parents[1] / 'test/integration/html_cache_nginx_test.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


def process_metrics(pid):
    pids = [pid]
    try:
        pids += [int(x) for x in Path('/proc/%d/task/%d/children' % (pid, pid)).read_text().split()]
    except OSError:
        pass
    result = dict(cpu_seconds=0., rss_bytes=0, read_bytes=0, write_bytes=0)
    for p in pids:
        try:
            fields = Path('/proc/%d/stat' % p).read_text().split(') ', 1)[1].split()
            result['cpu_seconds'] += (int(fields[11]) + int(fields[12])) / os.sysconf('SC_CLK_TCK')
            for line in Path('/proc/%d/status' % p).read_text().splitlines():
                if line.startswith('VmRSS:'):
                    result['rss_bytes'] += int(line.split()[1]) * 1024
            io = dict(line.split(': ') for line in Path('/proc/%d/io' % p).read_text().splitlines())
            result['read_bytes'] += int(io['read_bytes'])
            result['write_bytes'] += int(io['write_bytes'])
        except (OSError, ValueError, KeyError):
            continue
    return result


def percentile(values, fraction):
    values = sorted(values)
    return values[min(len(values) - 1, int((len(values) - 1) * fraction))]


def run(options, enabled):
    fixture.OPTIONS = argparse.Namespace(nginx=options.nginx, module=options.module,
        extra_config='''
  pagespeed HistoryArchive %s;
  pagespeed HistoryArchiveInterval %d;
  pagespeed HistoryArchiveRetentionDays 7;
  pagespeed HistoryArchiveMaxSizeMb 100;
''' % ('on' if enabled else 'off', options.interval))
    cls = fixture.HtmlCacheNginxTest
    cls.setUpClass()
    try:
        if enabled and options.seed_segments:
            history = cls.root / 'cache/history'
            history.mkdir(mode=0o700, exist_ok=True)
            with (history / '.writer.lock').open('a') as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                now = int(time.time() * 1000)
                for i in range(1, options.seed_segments + 1):
                    # Empty synthetic segments exercise directory density;
                    # they do not invent resource rewrite events.
                    header = 'PSH1 %d 1 0 0 %d 0 0 0 0 0 0 0 0 0 0 0\n' % (i, now)
                    (history / ('segment-%020d.psh' % i)).write_text(header)
        headers = {'User-Agent': 'HistoryArchiveBenchmark/1.0', 'Accept': 'text/html'}
        for i in range(64):
            cls.fetch('/?archive-benchmark=%d' % i, headers)
        time.sleep(options.warmup)
        start = process_metrics(cls.process.pid)
        peak = [start['rss_bytes']]
        monitor_stop = threading.Event()

        def monitor():
            while not monitor_stop.wait(.1):
                peak[0] = max(peak[0], process_metrics(cls.process.pid)['rss_bytes'])
        thread = threading.Thread(target=monitor)
        thread.start()
        begin = time.perf_counter()
        deadline = begin + options.duration

        def requests(client):
            latencies = []
            errors = 0
            connection = http.client.HTTPConnection('127.0.0.1', cls.port, timeout=10)
            try:
                i = client
                while time.perf_counter() < deadline:
                    tick = time.perf_counter()
                    try:
                        connection.request('GET', '/?archive-benchmark=%d' % (i % 64), headers=headers)
                        response = connection.getresponse()
                        response.read()
                        if response.status != 200:
                            errors += 1
                    except (OSError, http.client.HTTPException):
                        errors += 1
                        connection.close()
                        connection = http.client.HTTPConnection('127.0.0.1', cls.port, timeout=10)
                    latencies.append((time.perf_counter() - tick) * 1000)
                    i += options.clients
            finally:
                connection.close()
            return latencies, errors
        with concurrent.futures.ThreadPoolExecutor(max_workers=options.clients) as pool:
            results = list(pool.map(requests, range(options.clients)))
        elapsed = time.perf_counter() - begin
        monitor_stop.set()
        thread.join()
        end = process_metrics(cls.process.pid)
        latencies = [n for row, _ in results for n in row]
        history = cls.root / 'cache/history'
        size = allocated = 0
        if history.exists():
            allocated = history.stat().st_blocks * 512
            for p in history.iterdir():
                try:
                    st = p.stat()
                except FileNotFoundError:
                    continue  # Atomic checkpoint replacement or retention.
                allocated += st.st_blocks * 512
                if stat.S_ISREG(st.st_mode):
                    size += st.st_size
        if enabled and not (history / 'checkpoint').exists():
            raise RuntimeError('No successful archive snapshot; benchmark is invalid')
        if not enabled and history.exists():
            raise RuntimeError('Disabled configuration unexpectedly created archive storage')
        checkpoint = (history / 'checkpoint').read_text().split() if enabled else None
        cpu = end['cpu_seconds'] - start['cpu_seconds']
        return dict(enabled=enabled, requests=len(latencies), errors=sum(n for _, n in results),
            elapsed_seconds=elapsed, requests_per_second=len(latencies) / elapsed,
            p95_ms=percentile(latencies, .95), p99_ms=percentile(latencies, .99),
            server_cpu_seconds=cpu, server_cpu_percent=cpu / elapsed * 100,
            server_peak_rss_bytes=peak[0], disk_read_bytes=end['read_bytes'] - start['read_bytes'],
            disk_write_bytes=end['write_bytes'] - start['write_bytes'], archive_bytes=size,
            archive_allocated_bytes=allocated,
            archived_records=int(checkpoint[8]) if checkpoint else 0,
            overwritten_source_bytes=int(checkpoint[6]) if checkpoint else 0)
    finally:
        cls.doClassCleanups()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nginx', required=True)
    parser.add_argument('--module', required=True)
    parser.add_argument('--duration', type=float, default=10)
    parser.add_argument('--warmup', type=float, default=2)
    parser.add_argument('--interval', type=int, default=1,
        help='1s stresses snapshots; use 300 and duration >= 305 for production cadence')
    parser.add_argument('--seed-segments', type=int, default=0,
        help='Synthetic empty segments for testing a populated archive directory')
    parser.add_argument('--clients', type=int, default=8)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--output', required=True)
    options = parser.parse_args()
    if (options.duration < options.interval + 1 or options.interval < 1 or
            options.warmup < 0 or options.seed_segments < 0 or
            options.clients < 1 or options.repeats < 1):
        parser.error('duration must span an archival interval; clients/repeats must be positive')
    runs = []
    for repeat in range(options.repeats):
        # Alternate order to reduce warm-cache and thermal bias.
        for enabled in ([False, True] if repeat % 2 == 0 else [True, False]):
            row = run(options, enabled)
            row['repeat'] = repeat
            runs.append(row)
            print(json.dumps(row), flush=True)
    metrics = ['requests_per_second', 'p95_ms', 'p99_ms', 'server_cpu_percent',
        'server_peak_rss_bytes', 'disk_read_bytes', 'disk_write_bytes', 'archive_bytes',
        'archive_allocated_bytes', 'archived_records', 'overwritten_source_bytes']
    medians = {state: {key: statistics.median(row[key] for row in runs if row['enabled'] == enabled)
        for key in metrics} for state, enabled in [('disabled', False), ('enabled', True)]}
    report = dict(options=vars(options), medians=medians, runs=runs,
        caveats=['Loopback synthetic HTML workload, not a production traffic trace.',
            'RSS is summed across master/workers and double-counts shared pages.',
            'CPU and physical disk I/O are sampled from Linux /proc for Nginx, not the load generator.',
            'Snapshots have background overhead; short-run differences include benchmark noise.',
            '1-second stress cadence is not the default 300-second cadence.',
            'Warm cached synthetic HTML isolates archive overhead; it does not benchmark image conversions.',
            'The 100KB live buffer can wrap under load; overwritten bytes are reported, not recovered.',
            'Optional seeded empty segments stress directory enumeration, not a real historical dataset.'])
    Path(options.output).write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(medians, indent=2))
    if any(row['errors'] for row in runs):
        raise SystemExit('HTTP errors occurred; do not treat results as a passing benchmark')

if __name__ == '__main__':
    main()

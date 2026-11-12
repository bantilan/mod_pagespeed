#!/usr/bin/env python3
"""Exercise the unified archive in isolated, unprivileged two-worker Nginx."""
import argparse
import base64
import concurrent.futures
import importlib.util
from pathlib import Path
import re
import socket
import subprocess
import sys
import time
import unittest
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('html_cache_tests',
    Path(__file__).with_name('html_cache_nginx_test.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)


class HistoryArchiveNginxTest(base.HtmlCacheNginxTest):
    def wait_archive(self, text):
        for _ in range(100):
            chunks = list((self.root / 'cache/history').glob('segment-*.psh'))
            for p in chunks:
                try:
                    data = p.read_bytes()
                except FileNotFoundError:
                    continue
                if text in data:
                    return chunks
                for line in data.splitlines():
                    if b'PS_HTML_DETAIL\t' in line:
                        encoded = line.split(b'\t')[-1]
                        if text in base64.urlsafe_b64decode(encoded + b'=' * (-len(encoded) % 4)):
                            return chunks
            time.sleep(.1)
        self.fail('Snapshot did not contain expected existing log record')

    def test_archive_and_historical_views(self):
        self.fetch('/?archived-page-check=1')
        files = self.wait_archive(b'archived-page-check=1')
        status, headers, body = self.fetch('/pagespeed_admin/html_cache?history=1')
        self.assertEqual(200, status)
        self.assertIn(b'archived-page-check=1', body)
        self.assertIn(b'One chronological history', body)
        self.assertIn('no-store', headers.get('Cache-Control', ''))
        for view in ['message_history', 'images', 'html_cache', 'javascript', 'css']:
            status, _, page = self.fetch('/pagespeed_admin/' + view + '?history=1')
            self.assertEqual(200, status)
            self.assertIn(b'Browse history', page)
            if view in ['javascript', 'css']:
                self.assertIn(b'not a complete per-resource rewrite history', page)
        empty = self.fetch('/pagespeed_admin/html_cache?history=1&from=1970-01-01&until=1970-01-02')[2]
        self.assertNotIn(b'archived-page-check=1', empty)
        invalid = self.fetch('/pagespeed_admin/html_cache?history=1&from=not-a-date')[2]
        self.assertIn(b'Invalid UTC date', invalid)
        dashboard = self.fetch('/pagespeed_admin/dashboard')[2]
        self.assertIn(b'Archive status', dashboard)
        health = self.fetch('/pagespeed_admin/history')[2]
        self.assertIn(b'Detected missing bytes', health)
        self.assertNotIn(b'No successful snapshot yet', health)

    def test_restart_reads_all_views_before_new_traffic_or_snapshot(self):
        cls = type(self)
        config_path = self.root / 'nginx.conf'
        config = config_path.read_text()
        history = self.root / 'cache/history'

        def start():
            cls.process = subprocess.Popen(
                [base.OPTIONS.nginx, '-p', str(cls.root) + '/', '-c',
                 str(config_path), '-e', str(cls.root / 'logs/error.log')],
                stdout=cls.log, stderr=cls.log)
            for _ in range(100):
                if cls.process.poll() is not None:
                    self.fail((self.root / 'logs/error.log').read_text())
                try:
                    with socket.create_connection(('127.0.0.1', cls.port), timeout=1):
                        return
                except OSError:
                    time.sleep(.05)
            self.fail('Isolated Nginx did not restart')

        cls.stop()
        try:
            config_path.write_text(config.replace('HistoryArchiveInterval 1;',
                                                  'HistoryArchiveInterval 300;').replace(
                '  pagespeed FileCachePath',
                '  pagespeed StatisticsLogging on;\n  pagespeed LogDir "%s";\n'
                '  pagespeed FileCachePath' % (self.root / 'logs'), 1))
            (self.root / 'logs/stats_log_dummy_hostname:-1').write_text(
                'timestamp: 1270493486000\nstatistics_instance: 123\n'
                'image_rewrites: 37\nimage_rewrite_total_bytes_saved: 1048576\n'
                'javascript_blocks_minified: 9\njavascript_total_bytes_saved: 1048576\n'
                'css_filter_blocks_rewritten: 4\ncss_filter_total_bytes_saved: 1048576\n'
                'html_cache_hits: 25\nhtml_cache_bytes_served: 1048576\n')
            history.mkdir(exist_ok=True)
            for p in history.iterdir():
                p.unlink()
            prefix = 'I[Mon, 05 Apr 2010 18:51:26 GMT] [Info] [987] '
            encoded = base64.urlsafe_b64encode(b'http://archive.example/restart.html').decode().rstrip('=')
            records = [
                'retained restart diagnostic',
                'PS_IMAGE_DETAIL\t1000\t200\t100\timage/webp\thttp://archive.example/restart.jpg\thttp://archive.example/restart.webp',
                'PS_HTML_DETAIL\t1000\t5000\t100\tdesktop\tWebP capable\toptimized-html-v1:restart\t' + encoded,
                "Script http://archive.example/restart.js didn't shrink.",
                'CSS parsing error in http://archive.example/restart.css',
            ]

            def header(sequence, begin, end, count):
                return ('PSH1 %d 123456789 %d %d 1270493486000 12 2 %d 3 '
                        '1270493486000 1270493486000 %d 20 10 5 3\n' %
                        (sequence, begin, end, count, count)).encode()

            offset = 20000
            for sequence, chunk, count in [(41, records[:3], 98), (42, records[3:], 100)]:
                payload = ''.join(prefix + line + '\n' for line in chunk).encode()
                (history / ('segment-%020d.psh' % sequence)).write_bytes(
                    header(sequence, offset, offset + len(payload), count) + payload)
                offset += len(payload)
            checkpoint = header(42, offset, offset, 100)
            previous = {p.name: p.read_bytes() for p in history.glob('segment-*.psh')}
            expected = {
                'message_history': b'retained restart diagnostic',
                'images': b'http://archive.example/restart.jpg',
                'html_cache': b'http://archive.example/restart.html',
                'javascript': b'http://archive.example/restart.js',
                'css': b'http://archive.example/restart.css',
            }
            for mode in ['valid', 'missing', 'corrupt', 'stale']:
                with self.subTest(checkpoint=mode):
                    if mode == 'missing':
                        (history / 'checkpoint').unlink()
                    elif mode == 'corrupt':
                        (history / 'checkpoint').write_bytes(b'incomplete checkpoint\n')
                    elif mode == 'stale':
                        (history / 'checkpoint').write_bytes(header(0, 0, 0, 0))
                    else:
                        (history / 'checkpoint').write_bytes(checkpoint)
                    start()
                    began = time.monotonic()
                    for view, marker in expected.items():
                        # Repeated connections also cover passive workers during startup.
                        for _ in range(4):
                            status, headers, page = self.fetch('/pagespeed_admin/' + view)
                            self.assertEqual(200, status)
                            self.assertIn(marker, page)
                            self.assertIn('no-store', headers.get('Cache-Control', ''))
                    health = self.fetch('/pagespeed_admin/history')[2]
                    self.assertIn(b'Archived records (cumulative): 100', health)
                    self.assertIn(b'Currently retained records: 5', health)
                    self.assertNotIn(b'No successful snapshot yet', health)
                    usage = re.search(rb'Archive disk usage at last successful snapshot: (\d+) bytes', health)
                    self.assertIsNotNone(usage)
                    allocated = history.stat().st_blocks * 512
                    allocated += sum(p.stat().st_blocks * 512 for p in history.iterdir())
                    self.assertEqual(allocated, int(usage[1]))
                    dashboard = self.fetch('/pagespeed_admin/dashboard')[2]
                    self.assertIn(b'http://archive.example/restart.jpg', dashboard)
                    self.assertIn(b'Recovered saved statistics + current run', dashboard)
                    self.assertIn(b'3.0 MB', dashboard)
                    self.assertIn(b">50</div>", dashboard)
                    self.assertIn(b">37</div>", dashboard)
                    self.assertIn(b">25</div>", dashboard)
                    self.assertLess(time.monotonic() - began, 30)
                    self.assertEqual(previous, {p.name: p.read_bytes()
                                               for p in history.glob('segment-*.psh')})
                    self.assertEqual(42, int((history / 'checkpoint').read_bytes().split()[1]))
                    if mode == 'valid':
                        self.assertEqual(checkpoint, (history / 'checkpoint').read_bytes())
                    cls.stop()
        finally:
            cls.stop()
            config_path.write_text(config)
            start()

    def test_concurrent_archive_browsing_does_not_block_normal_responses(self):
        self.fetch('/?concurrent-archive-reader=1')
        self.wait_archive(b'concurrent-archive-reader=1')
        with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
            tasks = [pool.submit(self.fetch, '/pagespeed_admin/message_history?history=1')
                     for _ in range(16)]
            normal = list(pool.map(lambda _: self.fetch('/?normal-during-archive=1'), range(24)))
        self.assertTrue(all(row[0] == 200 for row in normal))
        self.assertTrue(all(task.result()[0] in (200, 503) for task in tasks))

    def test_multi_worker_identity_deduplication(self):
        for i in range(12):
            self.fetch('/?multi-worker-archive=%d' % i)
        self.wait_archive(b'multi-worker-archive=11')
        before = set()
        for p in (self.root / 'cache/history').glob('segment-*.psh'):
            data = p.read_bytes()
            header, payload = data.split(b'\n', 1)
            fields = header.split()
            generation, offset = fields[2], int(fields[3])
            for line in payload.splitlines(keepends=True):
                identity = generation, offset
                self.assertNotIn(identity, before)
                before.add(identity)
                offset += len(line)
            self.assertEqual(int(fields[4]), offset)
        time.sleep(1.2)
        after = []
        for p in (self.root / 'cache/history').glob('segment-*.psh'):
            header, payload = p.read_bytes().split(b'\n', 1)
            f = header.split()
            offset = int(f[3])
            for line in payload.splitlines(keepends=True):
                after.append((f[2], offset))
                offset += len(line)
        self.assertEqual(len(set(after)), len(after))
        self.assertTrue(before.issubset(set(after)))

    def test_restart_recovers_checkpoint_and_preserves_previous_records(self):
        self.fetch('/?before-archive-restart=1')
        self.wait_archive(b'before-archive-restart=1')
        history = self.root / 'cache/history'
        previous = {p.name: p.read_bytes() for p in history.glob('segment-*.psh')}
        cls = type(self)
        cls.stop()
        cls.process = subprocess.Popen(
            [base.OPTIONS.nginx, '-p', str(cls.root) + '/', '-c',
             str(cls.root / 'nginx.conf'), '-e', str(cls.root / 'logs/error.log')],
            stdout=cls.log, stderr=cls.log)
        for _ in range(100):
            try:
                self.assertEqual(200, self.fetch('/?after-archive-restart=1')[0])
                break
            except OSError:
                time.sleep(.05)
        else:
            self.fail('Isolated Nginx did not restart')
        self.wait_archive(b'after-archive-restart=1')
        for name, data in previous.items():
            self.assertEqual(data, (history / name).read_bytes())
        generations = set()
        identities = set()
        for p in history.glob('segment-*.psh'):
            header, payload = p.read_bytes().split(b'\n', 1)
            fields = header.split()
            generations.add(fields[2])
            offset = int(fields[3])
            for line in payload.splitlines(keepends=True):
                self.assertNotIn((fields[2], offset), identities)
                identities.add((fields[2], offset))
                offset += len(line)
        self.assertGreaterEqual(len(generations), 2)
        self.assertGreaterEqual(int((history / 'checkpoint').read_bytes().split()[7]), 1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nginx', required=True)
    parser.add_argument('--module', required=True)
    options, remaining = parser.parse_known_args()
    options.extra_config = '''
  pagespeed HistoryArchive on;
  pagespeed HistoryArchiveInterval 1;
  pagespeed HistoryArchiveRetentionDays 7;
  pagespeed HistoryArchiveMaxSizeMb 1;
'''
    base.OPTIONS = options
    unittest.main(argv=[__file__, *remaining])

#!/usr/bin/env python3
"""Exercise the unified archive in isolated, unprivileged two-worker Nginx."""
import argparse
import base64
import concurrent.futures
import importlib.util
from pathlib import Path
import re
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
        self.assertIn(b'Archived records only', body)
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
        self.assertIn(b'Historical archive', dashboard)
        self.assertIn(b'Detected missing bytes', dashboard)
        self.assertNotIn(b'No successful snapshot yet', dashboard)

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

#!/usr/bin/env python3
"""Test HtmlCache with a rebuilt module in an isolated, unprivileged Nginx."""

import argparse
import concurrent.futures
import gzip
import http.client
from pathlib import Path
import re
import signal
import socket
import subprocess
import tempfile
import time
import unittest


COMMENT = re.compile(
    rb'<!-- PageSpeed Cache: (HIT|MISS|BYPASS) \| Created: (.*?) \| Age: (.*?) -->$')
UA = 'Mozilla/5.0 Chrome/123.0.0.0 Safari/537.36'


class HtmlCacheNginxTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='pagespeed-html-cache-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name)
        for name in ['www', 'cache', 'logs', 'body']:
            (cls.root / name).mkdir()
        html = '<!doctype html><html><head><title>Cache test</title></head><body>'
        html += '<p>Public HTML caching fixture</p>' * 100
        html += '</body></html>'
        (cls.root / 'www/index.html').write_text(html)
        (cls.root / 'www/off.html').write_text(html)
        (cls.root / 'www/show-off.html').write_text(html)
        (cls.root / 'www/private.html').write_text(html)
        (cls.root / 'www/style.css').write_text('body { color: green; }')
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            cls.port = listener.getsockname()[1]
        config = '''load_module "%s";
daemon off;
worker_processes 2;
pid "%s/nginx.pid";
error_log "%s/logs/error.log" notice;
events { worker_connections 256; }
http {
  access_log off;
  client_body_temp_path "%s/body";
  types { text/html html; text/css css; }
  gzip on;
  gzip_min_length 1;
  gzip_proxied any;
  pagespeed FileCachePath "%s/cache";
  pagespeed MessageBufferSize 100000;
  pagespeed AdminPath /pagespeed_admin;
  pagespeed HtmlCache on;
  pagespeed HtmlCacheTtlSec 4;
  pagespeed ShowCacheTimestamp on;
  pagespeed CriticalImagesBeaconEnabled off;
  server {
    listen 127.0.0.1:%d;
    server_name localhost;
    root "%s/www";
    pagespeed on;
    pagespeed RewriteLevel CoreFilters;
    location / { expires 10m; }
    location = /off.html {
      pagespeed HtmlCache off;
      pagespeed ShowCacheTimestamp off;
    }
    location = /show-off.html {
      expires 10m;
      pagespeed ShowCacheTimestamp off;
    }
    location = /private.html { add_header Cache-Control "private, no-store"; }
    location = /not-modified { return 304; }
  }
}
''' % (str(Path(OPTIONS.module).resolve()), cls.root, cls.root, cls.root,
       cls.root, cls.port, cls.root)
        (cls.root / 'nginx.conf').write_text(config)
        cls.log = (cls.root / 'logs/startup.log').open('w')
        cls.addClassCleanup(cls.log.close)
        cls.process = subprocess.Popen(
            [OPTIONS.nginx, '-p', str(cls.root) + '/', '-c',
             str(cls.root / 'nginx.conf'), '-e', str(cls.root / 'logs/error.log')],
            stdout=cls.log, stderr=cls.log)
        cls.addClassCleanup(cls.stop)
        for attempt in range(100):
            if cls.process.poll() is not None:
                raise RuntimeError((cls.root / 'logs/startup.log').read_text() +
                                   (cls.root / 'logs/error.log').read_text())
            try:
                cls.fetch('/style.css')
                return
            except OSError:
                time.sleep(.05)
        raise RuntimeError('Isolated Nginx did not start')

    @classmethod
    def stop(cls):
        if cls.process.poll() is None:
            cls.process.send_signal(signal.SIGTERM)
            try:
                cls.process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                cls.process.kill()
                cls.process.wait()

    @classmethod
    def fetch(cls, path='/', extra=None):
        headers = {'User-Agent': UA, 'Accept': 'text/html,image/avif,image/webp'}
        headers.update(extra or {})
        connection = http.client.HTTPConnection('127.0.0.1', cls.port, timeout=10)
        try:
            connection.request('GET', path, headers=headers)
            response = connection.getresponse()
            body = response.read()
            result_headers = dict(response.getheaders())
            if response.getheader('Content-Encoding') == 'gzip':
                body = gzip.decompress(body)
            return response.status, result_headers, body
        finally:
            connection.close()

    def info(self, body):
        match = COMMENT.search(body)
        self.assertIsNotNone(match, body[-300:])
        self.assertIn(b'</html>', body[:match.start()])
        self.assertEqual(1, body.count(b'<!-- PageSpeed Cache:'))
        return match.groups()

    def test_hit_expiry_and_gzip(self):
        path = '/?gzip-test=1'
        headers = {'Accept-Encoding': 'gzip'}
        status, response_headers, first = self.fetch(path, headers)
        self.assertEqual(200, status)
        self.assertEqual('gzip', response_headers.get('Content-Encoding'))
        self.assertIn('no-store', response_headers.get('Cache-Control', ''))
        self.assertEqual(b'MISS', self.info(first)[0])
        time.sleep(1.05)
        second = self.info(self.fetch(path, headers)[2])
        self.assertEqual(b'HIT', second[0])
        self.assertEqual(self.info(first)[1], second[1])
        self.assertNotEqual(b'0s', second[2])
        time.sleep(3.1)
        regenerated = self.info(self.fetch(path, headers)[2])
        self.assertEqual(b'MISS', regenerated[0])
        self.assertNotEqual(second[1], regenerated[1])

    def test_accept_and_browser_variant_isolation(self):
        path = '/?variants=1'
        variants = [
            {'User-Agent': UA, 'Accept': 'text/html,image/avif,image/webp'},
            {'User-Agent': 'Mozilla/5.0 Firefox/92.0', 'Accept': 'text/html,image/webp'},
            {'User-Agent': 'Mozilla/5.0 MSIE 9.0', 'Accept': 'text/html'},
            {'User-Agent': UA, 'Accept': 'text/html,image/webp'},
        ]
        for headers in variants:
            self.assertEqual(b'MISS', self.info(self.fetch(path, headers)[2])[0])
        for headers in variants:
            self.assertEqual(b'HIT', self.info(self.fetch(path, headers)[2])[0])

    def test_private_requests_and_responses(self):
        for headers in [{'Cookie': 'session=secret'}, {'Authorization': 'Bearer secret'},
                        {'Cache-Control': 'no-cache'}]:
            self.assertEqual(b'BYPASS', self.info(self.fetch('/', headers)[2])[0])
        self.assertEqual(b'BYPASS', self.info(self.fetch('/private.html')[2])[0])

    def test_disabled_non_html_and_304(self):
        for path in ['/off.html', '/show-off.html', '/style.css', '/not-modified']:
            status, headers, body = self.fetch(path)
            self.assertNotIn(b'<!-- PageSpeed Cache:', body)
            if path == '/not-modified':
                self.assertEqual(304, status)
                self.assertEqual(b'', body)

    def test_concurrent_warm_hits(self):
        path = '/?concurrency=1'
        self.fetch(path)
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            results = list(pool.map(lambda _: self.fetch(path), range(24)))
        for status, headers, body in results:
            self.assertEqual(200, status)
            self.assertEqual(b'HIT', self.info(body)[0])

    def test_html_cache_admin_history_and_dashboard(self):
        path = '/?admin-history-check=1'
        self.assertEqual(b'MISS', self.info(self.fetch(path)[2])[0])
        self.assertEqual(b'HIT', self.info(self.fetch(path)[2])[0])
        self.assertEqual(b'BYPASS', self.info(self.fetch(
            '/?private-admin-check=1', {'Cookie': 'session=private'})[2])[0])
        status, headers, body = self.fetch('/pagespeed_admin/html_cache')
        self.assertEqual(200, status)
        self.assertIn(b'Recent HTML publications', body)
        self.assertIn(b'admin-history-check=1', body)
        self.assertIn(b'data-variant=', body)
        self.assertIn(b'pagespeed-html-device', body)
        self.assertIn(b'not a live cache inventory', body)
        self.assertIn(b'HTML CACHE HITS', body)
        self.assertIn('no-store', headers.get('Cache-Control', ''))
        self.assertNotIn(b'private-admin-check=1', body)
        self.assertNotIn(b'Public HTML caching fixture', body)
        self.assertNotIn(b'<!-- PageSpeed Cache:', body)
        dashboard = self.fetch('/pagespeed_admin/dashboard')[2]
        self.assertIn(b'Browse HTML history', dashboard)
        self.assertIn(b'html_cache', dashboard)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nginx', default='/usr/sbin/nginx')
    parser.add_argument('--module', default='/usr/lib/nginx/modules/ngx_pagespeed.so')
    OPTIONS, remaining = parser.parse_known_args()
    unittest.main(argv=[__file__, *remaining])

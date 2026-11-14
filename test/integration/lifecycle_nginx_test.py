#!/usr/bin/env python3
"""SEC-06 #13/#15/#16 lifecycle tests against isolated nginx.

#13: a failed reload keeps serving from the live cycle's factory (optimized
page before and after; a following good reload succeeds).
#15: `nginx -t` validates without creating cache volumes.
#16: nested locations and if blocks inherit the enclosing location's
pagespeed settings instead of falling back to the server's.
"""
import http.client
import os
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.dont_write_bytecode = True

UA = 'Mozilla/5.0 Chrome/123.0.0.0 Safari/537.36'


def free_port():
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        return listener.getsockname()[1]


class IsolatedNginx:
    def __init__(self, test, extra_server='', extra_http=''):
        self.test = test
        self.temp = tempfile.TemporaryDirectory(prefix='pagespeed-lifecycle-')
        test.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ['www', 'cache', 'logs', 'body']:
            (self.root / name).mkdir()
        self.port = free_port()

    def write_config(self, extra_server='', extra_http=''):
        config = '''load_module "%s";
daemon off;
master_process on;
worker_processes 1;
pid "%s/nginx.pid";
error_log "%s/logs/error.log" notice;
events { worker_connections 256; }
http {
  access_log off;
  client_body_temp_path "%s/body";
  types { text/html html; text/css css; }
  pagespeed FileCachePath "%s/cache";
%s
  server {
    listen 127.0.0.1:%d;
    server_name localhost;
    root "%s/www";
    pagespeed on;
    pagespeed RewriteLevel CoreFilters;
%s
  }
}
''' % (str(Path(OPTIONS.module).resolve()), self.root, self.root, self.root,
       self.root, extra_http, self.port, self.root, extra_server)
        (self.root / 'nginx.conf').write_text(config)
        return self.root / 'nginx.conf'

    def start(self):
        self.log = (self.root / 'logs/startup.log').open('w')
        self.test.addCleanup(self.log.close)
        self.process = subprocess.Popen(
            [OPTIONS.nginx, '-p', str(self.root) + '/', '-c',
             str(self.root / 'nginx.conf'), '-e',
             str(self.root / 'logs/error.log')],
            stdout=self.log, stderr=self.log)
        self.test.addCleanup(self.stop)
        for _ in range(100):
            if self.process.poll() is not None:
                raise RuntimeError((self.root / 'logs/startup.log').read_text() +
                                   (self.root / 'logs/error.log').read_text())
            try:
                self.fetch('/style.css')
                return
            except OSError:
                time.sleep(.05)
        raise RuntimeError('Isolated Nginx did not start')

    def stop(self):
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGQUIT)
            try:
                self.process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()

    def fetch(self, path, extra=None):
        headers = {'User-Agent': UA, 'Accept': '*/*'}
        headers.update(extra or {})
        connection = http.client.HTTPConnection(
            '127.0.0.1', self.port, timeout=10)
        try:
            connection.request('GET', path, headers=headers)
            response = connection.getresponse()
            return response.status, dict(response.getheaders()), \
                response.read()
        finally:
            connection.close()


class FailedReloadTest(unittest.TestCase):
    def test_failed_reload_keeps_serving(self):
        # Case A: the failing cycle parses (bad listen port) with a pagespeed
        # server; the live cycle keeps serving optimized pages.
        env = IsolatedNginx(self)
        (env.root / 'www/a.css').write_text('a { color: red; }\n' * 200)
        (env.root / 'www/index.html').write_text(
            '<!DOCTYPE html><html><head>'
            '<link rel="stylesheet" href="/a.css">'
            '</head><body><p>x</p></body></html>')
        (env.root / 'www/style.css').write_text('x {}')
        conf = env.write_config('    location / { expires 10m; }')
        env.start()
        status, _, body = env.fetch('/index.html')
        self.assertEqual(200, status)
        # Warm until rewritten.
        deadline = time.time() + 60
        while b'.pagespeed.' not in body and time.time() < deadline:
            time.sleep(0.5)
            _, _, body = env.fetch('/index.html')
        self.assertIn(b'.pagespeed.', body, body[:200])

        # Break the config at the listen socket (parses, then fails).
        bad = conf.read_text().replace(
            f'listen 127.0.0.1:{env.port};',
            f'listen 127.0.0.1:{env.port};\n    listen 127.0.0.1:1;')
        conf.write_text(bad)
        reload_proc = subprocess.run(
            [OPTIONS.nginx, '-p', str(env.root) + '/', '-c', str(conf),
             '-s', 'reload'], capture_output=True, text=True, timeout=60)
        self.assertEqual(0, reload_proc.returncode)
        # The reload itself must fail at the busy/invalid socket: the new
        # cycle never goes live.
        time.sleep(2)
        error_log = (env.root / 'logs/error.log').read_text(errors='replace')
        self.assertRegex(error_log, r'bind\(\)|emerg|invalid port')
        # Live cycle still serves optimized pages.
        status, _, body = env.fetch('/index.html')
        self.assertEqual(200, status)
        self.assertIn(b'.pagespeed.', body, body[:200])
        # A following good reload succeeds.
        conf.write_text(conf.read_text().replace(
            f'\n    listen 127.0.0.1:1;', ''))
        reload_proc = subprocess.run(
            [OPTIONS.nginx, '-p', str(env.root) + '/', '-c', str(conf),
             '-s', 'reload'], capture_output=True, text=True, timeout=60)
        self.assertEqual(0, reload_proc.returncode, reload_proc.stderr)
        time.sleep(1)
        status, _, body = env.fetch('/index.html')
        self.assertEqual(200, status)


class ConfigTestNoVolumesTest(unittest.TestCase):
    def test_config_test_creates_no_cache_files(self):
        env = IsolatedNginx(self)
        (env.root / 'www/style.css').write_text('x {}')
        conf = env.write_config('    location / { expires 10m; }')
        fresh = env.root / 'fresh_cache'
        fresh.mkdir()
        text = conf.read_text()
        text, n = re.subn(r'pagespeed\s+FileCachePath\s+[^;]+;',
                          f'pagespeed FileCachePath {fresh};', text)
        self.assertEqual(1, n)
        test_conf = env.root / 'nginx_config_test_only.conf'
        test_conf.write_text(text)
        try:
            proc = subprocess.run(
                [OPTIONS.nginx, '-p', str(env.root) + '/',
                 '-t', '-c', str(test_conf)],
                capture_output=True, text=True, timeout=120)
        finally:
            test_conf.unlink()
        self.assertEqual(0, proc.returncode, proc.stderr)
        self.assertEqual([], [p.name for p in fresh.rglob('*')])


class LocationInheritTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.env = IsolatedNginx.__new__(IsolatedNginx)
        # Manual setup (IsolatedNginx takes the test case for cleanups).
        cls._holder = cls()
        cls._holder._cleanups = []
        env = IsolatedNginx(cls._holder)
        www = env.root / 'www/location_inherit'
        (www / 'nested').mkdir(parents=True)
        page = ('<!DOCTYPE html><html><head>'
                '<link rel="stylesheet" href="/a.css">'
                '</head><body><p>inherit</p></body></html>')
        (env.root / 'www/a.css').write_text('a { color: red; }\n' * 200)
        (www / 'page.html').write_text(page)
        (www / 'nested/page.html').write_text(page)
        (env.root / 'www/style.css').write_text('x {}')
        (env.root / 'www/index.html').write_text(page)
        env.write_config('''    location / { expires 10m; }
    location /location_inherit/ {
      pagespeed off;
      location ~ /location_inherit/nested/.* { }
      if ($arg_ps_if) { set $inherit_probe "1"; }
    }''')
        env.start()
        cls.env = env

    @classmethod
    def tearDownClass(cls):
        for func, args, kwargs in reversed(cls._holder._cleanups):
            func(*args, **kwargs)

    def assert_passes_through(self, path, what):
        status, headers, body = self.env.fetch(path)
        self.assertEqual(200, status, what)
        self.assertNotIn('X-Page-Speed', headers, what)
        self.assertNotIn(b'.pagespeed.', body, what)

    def test_enclosing_location_is_off(self):
        self.assert_passes_through(
            '/location_inherit/page.html', 'enclosing location')

    def test_nested_location_inherits(self):
        self.assert_passes_through(
            '/location_inherit/nested/page.html', 'nested location')

    def test_if_block_inherits(self):
        self.assert_passes_through(
            '/location_inherit/page.html?ps_if=1', 'if block')

    def test_server_optimizes_elsewhere(self):
        deadline = time.time() + 60
        body = b''
        while time.time() < deadline:
            status, _, body = self.env.fetch('/index.html')
            self.assertEqual(200, status)
            if b'.pagespeed.' in body:
                break
            time.sleep(0.5)
        self.assertIn(b'.pagespeed.', body, body[:300])


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nginx', required=True)
    parser.add_argument('--module', required=True)
    OPTIONS, remaining = parser.parse_known_args()
    unittest.main(argv=[__file__, *remaining])

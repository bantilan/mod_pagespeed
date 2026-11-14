#!/usr/bin/env python3
"""SEC-06 #10: in-place optimization serves and records a resource only under
a URL the server and the module read alike.

Spellings the two merely encode differently are optimized in place as before;
spellings they read as different paths or hosts get the server's own answer
and are neither served from nor stored into another entry. Optimized CSS is
smaller than the original, which is the observable.
"""
import http.client
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
CSS = 'body {\n  color: green;\n  /* pad the file so the optimized form is visibly smaller */\n}\n'
CSS += 'p { margin: 0; }\n' * 4
ORIGINAL_SIZE = len(CSS.encode())


class IproUrlNginxTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='pagespeed-ipro-url-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name)
        for name in ['www', 'cache', 'logs', 'body']:
            (cls.root / name).mkdir()
        ipro = cls.root / 'www/ipro/cc200'
        ipro.mkdir(parents=True)
        (ipro / 'example.css').write_text(CSS)
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            cls.port = listener.getsockname()[1]
        config = '''load_module "%s";
daemon off;
worker_processes 1;
pid "%s/nginx.pid";
error_log "%s/logs/error.log" notice;
events { worker_connections 256; }
http {
  access_log off;
  client_body_temp_path "%s/body";
  types { text/css css; }
  pagespeed FileCachePath "%s/cache";
  server {
    listen 127.0.0.1:%d;
    server_name localhost;
    root "%s/www";
    pagespeed on;
    pagespeed RewriteLevel CoreFilters;
    location / { expires 10m; }
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
        for _ in range(100):
            if cls.process.poll() is not None:
                raise RuntimeError((cls.root / 'logs/startup.log').read_text() +
                                   (cls.root / 'logs/error.log').read_text())
            try:
                cls.fetch('/ipro/cc200/example.css')
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
    def fetch(cls, path, extra=None):
        headers = {'User-Agent': UA, 'Accept': '*/*'}
        headers.update(extra or {})
        connection = http.client.HTTPConnection(
            '127.0.0.1', cls.port, timeout=10)
        try:
            connection.request('GET', path, headers=headers)
            response = connection.getresponse()
            return response.status, dict(response.getheaders()), \
                response.read()
        finally:
            connection.close()

    @classmethod
    def fetch_until_small(cls, path, timeout=60.0, extra=None):
        deadline = time.time() + timeout
        while time.time() < deadline:
            status, _, body = cls.fetch(path, extra)
            if status == 200 and len(body) < ORIGINAL_SIZE:
                return status, body
            time.sleep(0.5)
        return status, body

    def test_encoded_spellings_optimized_in_place(self):
        import random
        token = random.randint(1, 100000)
        for path in ('/ipro/cc200/%65xample.css',
                     '/ipro%2Fcc200/example.css'):
            status, body = self.fetch_until_small(f'{path}?r={token}')
            self.assertEqual(200, status, path)
            self.assertLess(len(body), ORIGINAL_SIZE, path)

    def test_doubled_slash_optimized_in_place(self):
        import random
        token = random.randint(1, 100000)
        status, body = self.fetch_until_small(
            f'/ipro//cc200/example.css?r={token}')
        self.assertEqual(200, status)
        self.assertLess(len(body), ORIGINAL_SIZE)

    def test_disagreeing_paths_left_to_server(self):
        import random
        token = random.randint(1, 100000)
        plain = f'/ipro/cc200/example.css?r={token}'
        status, body = self.fetch_until_small(plain)
        self.assertEqual(200, status)
        # Backslash spellings: no such file to the server, and the optimized
        # entry of the slash spelling is not served in their place.
        for path in ('/ipro\\cc200\\example.css',
                     '/ipro/cc200\\example.css'):
            status, _, _ = self.fetch(f'{path}?r={token}')
            self.assertEqual(404, status, path)
        # The server merges then resolves: serves the plain file itself.
        status, _, body = self.fetch(
            f'/ipro//../ipro/cc200/example.css?r={token}')
        self.assertEqual(200, status)
        self.assertEqual(ORIGINAL_SIZE, len(body))

    def test_disagreeing_host_left_to_server(self):
        # The server matches on the host bytes as sent; the module normalizes
        # the host. 0x7f000001 reaches this server but reads differently, so
        # it gets the server's own answer and never the plain host's entry.
        import random
        token = random.randint(1, 100000)
        path = f'/ipro/cc200/example.css?r={token}'
        spelled_host = {'Host': '0x7f000001'}
        status, _, body = self.fetch(path, spelled_host)
        self.assertEqual(200, status)
        self.assertEqual(ORIGINAL_SIZE, len(body))
        # Once the plain host's entry is optimized, the spelled host still
        # gets the original.
        status, _ = self.fetch_until_small(path)
        self.assertEqual(200, status)
        status, _, body = self.fetch(path, spelled_host)
        self.assertEqual(200, status)
        self.assertEqual(ORIGINAL_SIZE, len(body))


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nginx', required=True)
    parser.add_argument('--module', required=True)
    OPTIONS, remaining = parser.parse_known_args()
    unittest.main(argv=[__file__, *remaining])

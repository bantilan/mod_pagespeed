#!/usr/bin/env python3
"""SEC-02 #7: no s-maxage stamped on responses to authenticated requests."""

import argparse
import http.client
from pathlib import Path
import signal
import socket
import struct
import subprocess
import tempfile
import time
import unittest
import zlib


UA = 'Mozilla/5.0 Chrome/123.0.0.0 Safari/537.36'


def tiny_png():
    def chunk(ctype, data):
        out = struct.pack('>I', len(data)) + ctype + data
        return out + struct.pack('>I', zlib.crc32(ctype + data) & 0xffffffff)

    ihdr = struct.pack('>IIBBBBB', 8, 8, 8, 2, 0, 0, 0)
    raw = b''.join(b'\x00' + bytes(8 * 3) for _ in range(8))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) +
            chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


class IproSmaxageAuthNginxTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='pagespeed-smaxage-auth-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name)
        for name in ['www', 'cache', 'logs', 'body']:
            (cls.root / name).mkdir()
        (cls.root / 'www/img.png').write_bytes(tiny_png())
        (cls.root / 'www/style.css').write_text(
            'body { color: green; }\n' * 40)
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
  types { text/css css; image/png png; }
  pagespeed FileCachePath "%s/cache";
  pagespeed InPlaceSMaxAgeSec 300;
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
        headers = {'User-Agent': UA}
        headers.update(extra or {})
        connection = http.client.HTTPConnection('127.0.0.1', cls.port, timeout=10)
        try:
            connection.request('GET', path, headers=headers)
            response = connection.getresponse()
            body = response.read()
            return response.status, dict(response.getheaders()), body
        finally:
            connection.close()

    def test_smaxage_present_without_auth(self):
        # Control: the merge path is armed, unauthenticated IPRO responses
        # carry the configured s-maxage window.
        status, headers, _ = self.fetch('/img.png?ctrl=1')
        self.assertEqual(200, status)
        self.assertIn('s-maxage=300', headers.get('Cache-Control', ''))

    def test_no_smaxage_with_authorization(self):
        # An authenticated request must not receive s-maxage: that directive
        # would let downstream shared caches store the response and reuse it
        # for other clients (RFC 9111 3.5) without the origin's permission.
        status, headers, _ = self.fetch(
            '/img.png?auth=1', {'Authorization': 'Basic dXNlcjpwYXNz'})
        self.assertEqual(200, status)
        self.assertNotIn('s-maxage', headers.get('Cache-Control', ''))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nginx', default='/usr/sbin/nginx')
    parser.add_argument('--module', default='/usr/lib/nginx/modules/ngx_pagespeed.so')
    OPTIONS, remaining = parser.parse_known_args()
    unittest.main(argv=[__file__, *remaining])

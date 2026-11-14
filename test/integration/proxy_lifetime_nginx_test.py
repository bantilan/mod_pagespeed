#!/usr/bin/env python3
"""SEC-06 #9 smoke: HTML transform of a proxied page finishes cleanly.

With proxy_pass in front, the upstream owns the request while the origin
sends the page; the base-fetch callback must not finalize the request out
from under it (nor leak the reference it holds). The page is served
rewritten, repeatedly, with no worker crash.
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


class ProxyLifetimeNginxTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='pagespeed-proxy-life-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name)
        for name in ['www', 'cache', 'logs', 'body']:
            (cls.root / name).mkdir()
        (cls.root / 'www/index.html').write_text(
            '<!DOCTYPE html><html><head><title>t</title>'
            '<link rel="stylesheet" href="/a.css"></head>'
            '<body><p>proxied</p></body></html>')
        (cls.root / 'www/a.css').write_text('a { color: red; }\n' * 200)
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
  types { text/html html; text/css css; }
  pagespeed FileCachePath "%s/cache";
  server {
    listen 127.0.0.1:%d;
    server_name localhost;
    root "%s/www";
    pagespeed on;
    pagespeed RewriteLevel CoreFilters;
    location / { expires 10m; }
    location /proxied/ {
      proxy_pass http://127.0.0.1:%d/;
      proxy_http_version 1.1;
    }
  }
}
''' % (str(Path(OPTIONS.module).resolve()), cls.root, cls.root, cls.root,
       cls.root, cls.port, cls.root, cls.port)
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
                cls.fetch('/a.css')
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
    def fetch(cls, path):
        connection = http.client.HTTPConnection(
            '127.0.0.1', cls.port, timeout=10)
        try:
            connection.request('GET', path, headers={'User-Agent': UA})
            response = connection.getresponse()
            return response.status, response.read()
        finally:
            connection.close()

    def test_proxied_page_rewritten_repeatedly(self):
        for _ in range(5):
            status, body = self.fetch('/proxied/index.html')
            self.assertEqual(200, status)
            self.assertIn(b'a.css', body)
        self.assertIsNone(self.process.poll(), 'worker died')
        self.assertIn(b'.pagespeed.', body, body[:300])


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nginx', required=True)
    parser.add_argument('--module', required=True)
    OPTIONS, remaining = parser.parse_known_args()
    unittest.main(argv=[__file__, *remaining])

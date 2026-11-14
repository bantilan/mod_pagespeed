#!/usr/bin/env python3
"""SEC-06 #5/#6: request-body bounds and draining on module paths.

#5: a spooled beacon POST body past 128 KiB is refused (500 + log line)
instead of being buffered without bound.
#6: module-generated responses discard an unread request body first, so a
pipelined request on the same connection is answered normally (no
smuggling-style desync).
"""
import http.client
import importlib.util
from pathlib import Path
import re
import subprocess
import sys
import time
import unittest

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    'html_cache_tests', Path(__file__).with_name('html_cache_nginx_test.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)


class RequestBodyNginxTest(base.HtmlCacheNginxTest):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        www = cls.root / 'www'
        (www / 'a.css').write_text('a { color: red; }\n' * 200)
        (www / 'linked.html').write_text(
            '<!DOCTYPE html><html><head>'
            '<link rel="stylesheet" href="/a.css">'
            '</head><body><p>x</p></body></html>')
        cls.stop()
        config_path = cls.root / 'nginx.conf'
        config = config_path.read_text().replace(
            '  pagespeed FileCachePath',
            '  pagespeed MessagesPath /ngx_pagespeed_message;\n'
            '  pagespeed FileCachePath', 1)
        config_path.write_text(config)
        cls.process = subprocess.Popen(
            [base.OPTIONS.nginx, '-p', str(cls.root) + '/', '-c',
             str(config_path), '-e', str(cls.root / 'logs/error.log')],
            stdout=cls.log, stderr=cls.log)
        for _ in range(100):
            if cls.process.poll() is not None:
                raise RuntimeError((cls.root / 'logs/error.log').read_text())
            try:
                cls.fetch('/style.css')
                return
            except OSError:
                time.sleep(.05)
        raise RuntimeError('Isolated Nginx did not start')

    @classmethod
    def post_then_get_same_connection(cls, post_path, post_body):
        connection = http.client.HTTPConnection(
            '127.0.0.1', cls.port, timeout=10)
        try:
            connection.request(
                'POST', post_path, body=post_body,
                headers={'User-Agent': base.UA,
                         'Content-Type': 'application/x-www-form-urlencoded'})
            post_response = connection.getresponse()
            post_status = post_response.status
            post_response.read()
            connection.request(
                'GET', '/style.css', headers={'User-Agent': base.UA})
            get_response = connection.getresponse()
            return post_status, get_response.status, get_response.read()
        finally:
            connection.close()

    def test_oversized_beacon_body_refused(self):
        post_status, _, _ = self.post_then_get_same_connection(
            '/ngx_pagespeed_beacon?url=http://127.0.0.1/',
            b'x=1&' + b'y' * 200 * 1024)
        self.assertEqual(500, post_status)
        error_log = (self.root / 'logs/error.log').read_text(errors='replace')
        self.assertIn('post body too large', error_log)

    def test_resource_answer_drains_body(self):
        body = None
        for _ in range(120):
            _, _, body = self.fetch('/linked.html')
            if b'.pagespeed.' in body:
                break
            time.sleep(0.5)
        match = re.search(rb'href="([^"]*\.pagespeed\.[^"]*)"', body)
        self.assertIsNotNone(match, body[:300])
        resource = match.group(1).decode()
        post_status, get_status, get_body = \
            self.post_then_get_same_connection(resource, b'z' * 4096)
        self.assertEqual(200, post_status)
        self.assertEqual(200, get_status)
        self.assertIn(b'color', get_body)

    def test_messages_answer_drains_body(self):
        post_status, get_status, get_body = \
            self.post_then_get_same_connection(
                '/ngx_pagespeed_message', b'z' * 4096)
        self.assertEqual(200, post_status)
        self.assertEqual(200, get_status)
        self.assertIn(b'color', get_body)


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nginx', required=True)
    parser.add_argument('--module', required=True)
    options, remaining = parser.parse_known_args()
    options.extra_config = ''
    base.OPTIONS = options
    unittest.main(argv=[__file__, *remaining])

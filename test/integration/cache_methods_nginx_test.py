#!/usr/bin/env python3
"""SEC-05 #11: only GET and HEAD reach the pagespeed cache paths.

A request with any other method is not answered through the proxy branch:
it falls through to nginx (which rejects a POST to a static resource with
405). HEAD on HTML is normalized to the GET it answers. GET still optimizes
(control).
"""
import http.client
import importlib.util
from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    'html_cache_tests', Path(__file__).with_name('html_cache_nginx_test.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)


class CacheMethodsNginxTest(base.HtmlCacheNginxTest):
    @classmethod
    def request(cls, method, path):
        headers = {'User-Agent': base.UA, 'Accept': '*/*'}
        connection = http.client.HTTPConnection(
            '127.0.0.1', cls.port, timeout=10)
        try:
            connection.request(method, path, headers=headers)
            response = connection.getresponse()
            return response.status, dict(response.getheaders()), \
                response.read()
        finally:
            connection.close()

    def test_post_to_resource_is_not_proxied(self):
        # Falls through to nginx, which allows only GET/HEAD on static
        # content; pagespeed must not proxy or record it.
        status, headers, _ = self.request('POST', '/style.css')
        self.assertEqual(405, status)
        self.assertNotIn('X-PageSpeed', headers)

    def test_head_html_matches_get_headers(self):
        get_status, get_headers, get_body = self.request('GET', '/index.html')
        self.assertEqual(200, get_status)
        head_status, head_headers, head_body = self.request(
            'HEAD', '/index.html')
        self.assertEqual(200, head_status)
        self.assertEqual(b'', head_body)
        self.assertEqual(get_headers.get('Content-Type'),
                         head_headers.get('Content-Type'))

    def test_get_still_caches(self):
        status, _, first = self.fetch('/?method-control=1')
        self.assertEqual(200, status)
        self.assertEqual(b'MISS', self.info(first)[0])
        _, _, second = self.fetch('/?method-control=1')
        self.assertEqual(b'HIT', self.info(second)[0])


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nginx', required=True)
    parser.add_argument('--module', required=True)
    options, remaining = parser.parse_known_args()
    options.extra_config = ''
    base.OPTIONS = options
    unittest.main(argv=[__file__, *remaining])

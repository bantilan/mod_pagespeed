#!/usr/bin/env python3
"""WG-03: LCP candidate selection against an isolated rebuilt Nginx module."""
import argparse
import http.client
import importlib.util
from pathlib import Path
import re
import subprocess
import sys
import time
import unittest
import urllib.parse

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    'html_cache_tests', Path(__file__).with_name('html_cache_nginx_test.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)

# Image bytes are deliberately absent (404 fixtures): selection, lazy hints
# and beacon hashing operate on tags, while unrewritten sources keep preload
# hrefs deterministic. Preload/URL coordination is WG-04 territory.
BEACON_CALL = re.compile(
    rb"pagespeed\.CriticalImages\.Run\('([^']*)','([^']*)','([^']*)',"
    rb"[^,]+,[^,]+,'([^']*)'\)")
HASH_ATTR = re.compile(rb'data-pagespeed-url-hash="([^"]+)"')
PRELOAD = b'<link rel="preload" as="image"'


class LcpCandidateSelectionNginxTest(base.HtmlCacheNginxTest):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.stop()
        www = cls.root / 'www'
        (www / 'lcp-logo.html').write_text(
            '<!DOCTYPE html><html><head><title>logo first</title></head><body>'
            '<img src="/logo.png" width="120" height="40">'
            '<img src="/hero.jpg">'
            '<img src="/below.jpg">'
            '</body></html>')
        (www / 'lcp-hidden.html').write_text(
            '<!DOCTYPE html><html><head><title>hidden</title></head><body>'
            '<img hidden src="/hidden.jpg">'
            '<img style="display: none" src="/styled.jpg">'
            '<img src="/hero.jpg">'
            '</body></html>')
        (www / 'lcp-priority.html').write_text(
            '<!DOCTYPE html><html><head><title>priority</title></head><body>'
            '<img src="/hero.jpg">'
            '<img fetchpriority="high" src="/second.jpg">'
            '<img src="/third.jpg">'
            '</body></html>')
        (www / 'lcp-beacon.html').write_text(
            '<!DOCTYPE html><html><head><title>beacon</title></head><body>'
            '<img src="/beacon-logo.png" width="120" height="40">'
            '<img src="/beacon-hero.jpg">'
            '</body></html>')
        # CriticalImagesBeaconEnabled must be set at server scope or below:
        # the http-scope value does not propagate (pre-existing quirk). It is
        # scoped to the beacon page so the inherited cache tests keep the
        # default beacon-off behavior (beacon responses bypass the HTML cache
        # by design, which would break their MISS/HIT expectations).
        cls.stop()
        config_path = cls.root / 'nginx.conf'
        config = config_path.read_text().replace(
            '    location = /off.html {',
            '    location = /lcp-beacon.html {\n'
            '      pagespeed CriticalImagesBeaconEnabled on;\n'
            '    }\n'
            '    location = /off.html {', 1)
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
        cls.process = subprocess.Popen(
            [base.OPTIONS.nginx, '-p', str(cls.root) + '/', '-c',
             str(cls.root / 'nginx.conf'),
             '-e', str(cls.root / 'logs/error.log')],
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
    def post_beacon(cls, beacon_url, html_url, options_hash, nonce, lcp_hash):
        path = '%s?url=%s' % (beacon_url,
                              urllib.parse.quote(html_url, safe=''))
        body = 'oh=%s&n=%s&lcp=%s' % (
            urllib.parse.quote(options_hash, safe=''),
            urllib.parse.quote(nonce, safe=''),
            urllib.parse.quote(lcp_hash, safe=''))
        connection = http.client.HTTPConnection('127.0.0.1', cls.port,
                                                timeout=10)
        try:
            connection.request(
                'POST', path, body,
                {'Content-Type': 'application/x-www-form-urlencoded',
                 'User-Agent': base.UA})
            response = connection.getresponse()
            response.read()
            return response.status
        finally:
            connection.close()

    def img_tag(self, body, src):
        for match in re.finditer(rb'<img\b[^>]*>', body):
            if src in match.group(0):
                return match.group(0)
        self.fail('img %r not found in %r' % (src, body[-500:]))

    def test_first_image_heuristic_without_evidence(self):
        _, _, body = self.fetch('/lcp-logo.html')
        self.assertIn(b'href="/logo.png"', body)
        self.assertEqual(1, body.count(PRELOAD))
        self.assertIn(b'<img src="/hero.jpg" loading="lazy">', body)

    def test_hidden_images_skipped_end_to_end(self):
        _, _, body = self.fetch('/lcp-hidden.html')
        self.assertIn(b'href="/hero.jpg"', body)
        self.assertEqual(1, body.count(PRELOAD))
        hidden = self.img_tag(body, b'/hidden.jpg')
        self.assertNotIn(b'fetchpriority', hidden)
        self.assertNotIn(b'loading=', hidden)
        styled = self.img_tag(body, b'/styled.jpg')
        self.assertNotIn(b'fetchpriority', styled)
        self.assertNotIn(b'loading=', styled)

    def test_author_high_priority_never_lazy_end_to_end(self):
        _, _, body = self.fetch('/lcp-priority.html')
        second = self.img_tag(body, b'/second.jpg')
        self.assertIn(b'fetchpriority="high"', second)
        self.assertNotIn(b'loading=', second)
        self.assertIn(b'<img src="/third.jpg" loading="lazy">', body)

    def test_beacon_evidence_redirects_hero(self):
        _, _, first = self.fetch('/lcp-beacon.html')
        # Pre-evidence: the logo wins the heuristic slot.
        self.assertIn(b'href="/beacon-logo.png"', first)
        call = BEACON_CALL.search(first)
        self.assertIsNotNone(call, first[-500:])
        beacon_url, html_url, options_hash, nonce = (
            group.decode() for group in call.groups())
        hero = self.img_tag(first, b'/beacon-hero.jpg')
        hashed = HASH_ATTR.search(hero)
        self.assertIsNotNone(hashed, hero)
        status = self.post_beacon(beacon_url, html_url, options_hash, nonce,
                                  hashed.group(1).decode())
        self.assertEqual(204, status)
        # The beacon write lands asynchronously; poll for the learned page.
        learned = None
        for _ in range(30):
            _, _, learned = self.fetch('/lcp-beacon.html')
            if b'href="/beacon-hero.jpg"' in learned:
                break
            time.sleep(.5)
        self.assertIn(b'href="/beacon-hero.jpg"', learned)
        self.assertEqual(1, learned.count(PRELOAD))
        logo = self.img_tag(learned, b'/beacon-logo.png')
        self.assertNotIn(b'fetchpriority', logo)
        self.assertNotIn(b'loading=', logo)
        hero = self.img_tag(learned, b'/beacon-hero.jpg')
        self.assertIn(b'fetchpriority="high"', hero)
        self.assertNotIn(b'loading=', hero)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nginx', required=True)
    parser.add_argument('--module', required=True)
    parser.add_argument('--browser', help='Unused; kept for runner parity')
    options, remaining = parser.parse_known_args()
    options.extra_config = (
        '\n  pagespeed EnableFilters prioritize_lcp_images;\n')
    base.OPTIONS = options
    unittest.main(argv=[__file__, *remaining])

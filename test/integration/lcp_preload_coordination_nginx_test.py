#!/usr/bin/env python3
"""WG-04: hero preload coordination against an isolated rebuilt Nginx module.

Proves with real browser network traffic that the automatic hero preload
matches the final resource: exactly one hero download, never the
pre-rewrite URL alongside the rewritten one.
"""
import argparse
import http.client
import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    'html_cache_tests', Path(__file__).with_name('html_cache_nginx_test.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)

TESTDATA = (Path(__file__).resolve().parents[2] / 'net' / 'instaweb' /
            'rewriter' / 'testdata')
CHROME_FLAGS = ['--headless', '--disable-gpu', '--no-first-run',
                '--no-default-browser-check', '--disable-background-networking',
                '--virtual-time-budget=5000', '--dump-dom']


class LcpPreloadCoordinationNginxTest(base.HtmlCacheNginxTest):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.stop()
        www = cls.root / 'www'
        photo = TESTDATA / 'Puzzle.jpg'
        icon = TESTDATA / 'BikeCrashIcn.png'
        small_photo = TESTDATA / 'Sample.jpg'
        for name in ['net-hero.jpg', 'net-below.jpg', 'net-cors.jpg']:
            shutil.copyfile(photo, www / name)
        for name in ['net-s1.png', 'net-s2.png', 'net-pic1.png',
                     'net-pic2.png']:
            shutil.copyfile(icon, www / name)
        shutil.copyfile(small_photo, www / 'net-fallback.jpg')
        shutil.copyfile(small_photo, www / 'net-picfallback.jpg')
        (www / 'net-page.html').write_text(
            '<!DOCTYPE html><html><head><title>net</title></head><body>'
            '<img src="/net-hero.jpg">'
            '<img src="/net-below.jpg">'
            '</body></html>')
        (www / 'net-srcset.html').write_text(
            '<!DOCTYPE html><html><head><title>srcset</title></head><body>'
            '<img src="/net-fallback.jpg" '
            'srcset="/net-s1.png 480w, /net-s2.png 800w" '
            'sizes="(max-width: 600px) 480px, 800px">'
            '</body></html>')
        (www / 'net-picture.html').write_text(
            '<!DOCTYPE html><html><head><title>picture</title></head><body>'
            '<picture>'
            '<source media="(min-width: 100px)" srcset="/net-pic1.png">'
            '<source srcset="/net-pic2.png">'
            '<img src="/net-picfallback.jpg">'
            '</picture>'
            '</body></html>')
        (www / 'cors-page.html').write_text(
            '<!DOCTYPE html><html><head><title>cors</title></head><body>'
            '<img crossorigin="anonymous" src="/net-cors.jpg">'
            '</body></html>')
        config_path = cls.root / 'nginx.conf'
        config = config_path.read_text().replace(
            'access_log off;',
            'access_log "%s/logs/access.log";' % cls.root)
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
    def load_in_chrome(cls, path):
        if not getattr(base.OPTIONS, 'browser', None):
            raise unittest.SkipTest('Pass --browser /path/to/chrome')
        with tempfile.TemporaryDirectory(
                prefix='pagespeed-wg04-browser-') as profile:
            command = [base.OPTIONS.browser] + CHROME_FLAGS + \
                ['--user-data-dir=' + profile,
                 'http://127.0.0.1:%d%s' % (cls.port, path)]
            result = subprocess.run(command, capture_output=True, timeout=90)
            if result.returncode != 0:
                raise RuntimeError(result.stderr[-2000:].decode(
                    errors='replace'))
            return result.stdout

    @classmethod
    def access_log(cls):
        return (cls.root / 'logs/access.log').read_text()

    @classmethod
    def chrome_log(cls):
        # Only real browser traffic: exclude pagespeed's own Serf rewrite
        # fetches and the python harness fetches (page bodies, no images).
        return '\n'.join(line for line in cls.access_log().splitlines()
                         if 'HeadlessChrome' in line)

    def count_gets(self, body, pattern):
        return len(re.findall(rb'GET ' + pattern + rb' HTTP', body))

    def count_family_gets(self, body, marker):
        # Matches original and rewritten (e.g. x-prefixed, fingerprinted)
        # URLs alike; exact-original assertions use count_gets instead.
        return self.count_gets(body, rb'[^ ]*' + marker + rb'[^ ]*')

    def warm_page_until_rewritten(self, path, marker, timeout=25):
        end = time.time() + timeout
        body = b''
        while time.time() < end:
            body = self.fetch(path)[2]
            if marker in body:
                return body
            time.sleep(.5)
        self.fail('page %s never rewrote %r in %ds: %r'
                  % (path, marker, timeout, body[-300:]))

    def test_hero_downloaded_exactly_once(self):
        self.warm_page_until_rewritten('/net-page.html', b'.pagespeed.ic.')
        dom = self.load_in_chrome('/net-page.html').decode(errors='replace')
        preload = re.search(
            r'<link rel="preload" as="image" href="([^"]+)"', dom)
        self.assertIsNotNone(preload, dom[-500:])
        img = re.search(r'<img src="([^"]+)"', dom)
        self.assertIsNotNone(img, dom[-500:])
        # The stale pre-rewrite URL must be gone from both tags.
        self.assertNotEqual('/net-hero.jpg', preload.group(1))
        self.assertEqual(img.group(1), preload.group(1))
        log = self.chrome_log().encode()
        self.assertEqual(
            1, self.count_gets(log, re.escape(preload.group(1).encode())))
        self.assertEqual(0, self.count_gets(log, b'/net-hero.jpg'))

    def test_srcset_downloads_single_candidate(self):
        self.warm_page_until_rewritten('/net-srcset.html', b'imagesrcset=')
        dom = self.load_in_chrome('/net-srcset.html').decode(errors='replace')
        self.assertIn('imagesrcset=', dom)
        log = self.chrome_log().encode()
        total = 0
        for name in [b'net-s1', b'net-s2', b'net-fallback']:
            total += self.count_family_gets(log, name)
        # One picked candidate; no fallback double-fetch.
        self.assertEqual(1, total)

    def test_multi_source_picture_no_wasted_preload(self):
        self.warm_page_until_rewritten('/net-picture.html', b'<picture>')
        dom = self.load_in_chrome('/net-picture.html').decode(
            errors='replace')
        self.assertNotIn('rel="preload"', dom)
        log = self.chrome_log().encode()
        total = 0
        for name in [b'net-pic1', b'net-pic2', b'net-picfallback']:
            total += self.count_family_gets(log, name)
        self.assertEqual(1, total)

    def test_crossorigin_single_download(self):
        self.warm_page_until_rewritten('/cors-page.html', b'.pagespeed.ic.')
        dom = self.load_in_chrome('/cors-page.html').decode(errors='replace')
        self.assertIn('crossorigin="anonymous"', dom)
        log = self.chrome_log().encode()
        self.assertEqual(1, self.count_family_gets(log, b'net-cors'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nginx', required=True)
    parser.add_argument('--module', required=True)
    parser.add_argument('--browser', required=True,
                        help='Chrome/Chromium binary for network assertions')
    options, remaining = parser.parse_known_args()
    options.extra_config = (
        '\n  pagespeed EnableFilters prioritize_lcp_images;\n')
    base.OPTIONS = options
    unittest.main(argv=[__file__, *remaining])

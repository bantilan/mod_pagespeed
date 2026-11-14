#!/usr/bin/env python3
"""WG-03: LCP candidate selection against an isolated rebuilt Nginx module."""
import argparse
import http.client
import importlib.util
from pathlib import Path
import re
import shutil
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
cdp_spec = importlib.util.spec_from_file_location(
    'cdp_chrome', Path(__file__).with_name('cdp_chrome.py'))
cdp_chrome = importlib.util.module_from_spec(cdp_spec)
cdp_spec.loader.exec_module(cdp_chrome)

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
        testdata = (Path(__file__).resolve().parents[2] / 'net' / 'instaweb' /
                    'rewriter' / 'testdata')
        shutil.copyfile(testdata / 'BikeCrashIcn.png', www / 'lcp-logo.png')
        shutil.copyfile(testdata / 'Puzzle.jpg', www / 'lcp-hero.jpg')
        (www / 'lcp-real.html').write_text(
            '<!DOCTYPE html><html><head><title>real beacon</title></head>'
            '<body>'
            '<img src="/lcp-logo.png" width="120" height="40">'
            '<img src="/lcp-hero.jpg">'
            '</body></html>')
        # CriticalImagesBeaconEnabled must be set at server scope or below:
        # the http-scope value does not propagate (pre-existing quirk). It is
        # scoped to the beacon pages so the inherited cache tests keep the
        # default beacon-off behavior (beacon responses bypass the HTML cache
        # by design, which would break their MISS/HIT expectations).
        cls.stop()
        config_path = cls.root / 'nginx.conf'
        config = config_path.read_text().replace(
            '    location = /off.html {',
            '    location = /lcp-beacon.html {\n'
            '      pagespeed CriticalImagesBeaconEnabled on;\n'
            '    }\n'
            '    location = /lcp-real.html {\n'
            # PassThrough keeps lazyload off so the beacon fires at onload
            # (with lazyload it waits for the lazy-settle path), and keeps
            # inline_images off so the logo stays a URL for the heuristic.
            '      pagespeed RewriteLevel PassThrough;\n'
            '      pagespeed EnableFilters prioritize_lcp_images;\n'
            '      pagespeed CriticalImagesBeaconEnabled on;\n'
            '    }\n'
            '    location = /off.html {', 1)
        config = config.replace(
            'access_log off;',
            'log_format beacon_body \'$remote_addr "$request" $status '
            '"$request_body"\';\n'
            '  access_log "%s/logs/access.log" beacon_body;' % cls.root, 1)
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

    @classmethod
    def beacon_posts(cls):
        log = (cls.root / 'logs/access.log').read_text()
        return [line for line in log.splitlines()
                if 'POST /' in line and '_pagespeed_beacon' in line]

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

    def test_real_beacon_lcp_round_trip(self):
        # End-to-end LCP learning with a real browser beacon (not forged).
        # Chrome's load must be the page's first touch: the beacon is
        # once-per-rebeacon-interval, so a python prefetch would consume the
        # slot and the browser load would get no beacon script.
        if not getattr(base.OPTIONS, 'browser', None):
            raise unittest.SkipTest('Pass --browser /path/to/chrome')
        chrome = cdp_chrome.CdpChrome(base.OPTIONS.browser)
        try:
            chrome.load('http://127.0.0.1:%d/lcp-real.html' % self.port)
            dom = chrome.cdp.evaluate('document.documentElement.outerHTML')
            # Pre-evidence: the heuristic preloads the logo.
            preload = re.search(r'<link rel="preload"[^>]*>', dom)
            self.assertIsNotNone(preload, dom[-500:])
            self.assertIn('href="/lcp-logo.png"', preload.group(0))
            self.assertIn('CriticalImages.Run(', dom)
            hero_tag = re.search(r'<img[^>]*lcp-hero\.jpg[^>]*>', dom)
            self.assertIsNotNone(hero_tag, dom[-500:])
            hero_hash = re.search(
                r'data-pagespeed-url-hash="(\d+)"',
                hero_tag.group(0)).group(1)
            # Wait until the browser actually observes the hero as LCP,
            # then navigate away: pagehide flushes the deferred beacon.
            hero_lcp = (
                '(function(){try{var o=new PerformanceObserver('
                'function(){});o.observe({type:"largest-contentful-paint",'
                'buffered:true});var e=o.takeRecords();o.disconnect();'
                'for(var i=0;i<e.length;i++){var el=e[i].element;'
                'if(el&&el.getAttribute&&el.getAttribute('
                '"data-pagespeed-url-hash")=="%s")return true;}'
                'return false;}catch(err){return "ERR:"+err;}})()'
                % hero_hash)
            for _ in range(150):
                if chrome.cdp.evaluate(hero_lcp) is True:
                    break
                time.sleep(.1)
            else:
                self.fail('browser never observed hero LCP')
            chrome.cdp.call('Page.navigate', {'url': 'about:blank'})
            chrome.cdp.wait_for('Page.loadEventFired')
            posts = []
            for _ in range(40):
                posts = self.beacon_posts()
                if any('lcp=' in line for line in posts):
                    break
                time.sleep(.5)
            self.assertTrue(
                any('lcp=%s' % hero_hash in line for line in posts),
                '\n'.join(posts[-5:]) or 'no beacon posts logged')
        finally:
            chrome.stop()
        # The beacon write lands asynchronously; poll for the learned page.
        learned = None
        for _ in range(40):
            _, _, learned = self.fetch('/lcp-real.html')
            if b'href="/lcp-hero.jpg"' in learned:
                break
            time.sleep(.5)
        self.assertIn(b'href="/lcp-hero.jpg"', learned)
        self.assertEqual(1, learned.count(PRELOAD))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nginx', required=True)
    parser.add_argument('--module', required=True)
    parser.add_argument('--browser',
                        help='Chrome/Chromium binary (real-beacon test only)')
    options, remaining = parser.parse_known_args()
    options.extra_config = (
        '\n  pagespeed EnableFilters prioritize_lcp_images;\n')
    base.OPTIONS = options
    unittest.main(argv=[__file__, *remaining])

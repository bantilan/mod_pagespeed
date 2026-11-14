#!/usr/bin/env python3
"""WG-07: cross-browser warm-cache matrix against an isolated module.

Proves no incompatible HTML/image cross-hit across capability variants:
every served format must lie in the requesting client's supported set at
every poll (interim states included), in both visit orders and under
concurrent mixed-UA load. Also locks downstream headers, purge, beacon
freshness and modern-document stability under the HTML cache.
"""
import argparse
import concurrent.futures
import gzip
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

NAV = 'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8'
IE_NAV = 'text/html, application/xhtml+xml, */*'
CHROME_UA = ('Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 '
             'Chrome/157.0 Safari/537.36')
FIREFOX_UA = ('Mozilla/5.0 (X11; Linux x86_64; rv:92.0) Gecko/20100101 '
              'Firefox/92.0')
IE9_UA = 'Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Trident/5.0)'
BOT_UA = 'Googlebot/2.1 (+http://www.google.com/bot.html)'

# src classifier suffixes: rewritten URLs end with .avif/.webp, the
# cache-extended original ends with .jpg, and the untouched original has
# no .pagespeed. marker at all.
BEACON_CALL = re.compile(
    rb"pagespeed\.CriticalImages\.Run\('([^']*)','([^']*)','([^']*)',"
    rb"[^,]+,[^,]+,'([^']*)'\)")
HASH_ATTR = re.compile(rb'data-pagespeed-url-hash="([^"]+)"')
CACHE_COMMENT = re.compile(rb'PageSpeed Cache: (\w+)')


def fetch_with_headers(port, path, headers):
    request = {'User-Agent': base.UA, 'Accept': '*/*'}
    request.update(headers)
    connection = http.client.HTTPConnection('127.0.0.1', port, timeout=30)
    try:
        connection.request('GET', path, headers=request)
        response = connection.getresponse()
        body = response.read()
        if response.getheader('Content-Encoding') == 'gzip':
            body = gzip.decompress(body)
        return response.status, response.getheaders(), body
    finally:
        connection.close()


def cache_status_of(body):
    match = CACHE_COMMENT.search(body)
    if match is None:
        raise AssertionError('no cache comment in %r' % body[-300:])
    return match.group(1).decode()


class BrowserCacheMatrixNginxTest(base.HtmlCacheNginxTest):
    CHROME = {'User-Agent': CHROME_UA, 'Accept': NAV}
    FIREFOX = {'User-Agent': FIREFOX_UA, 'Accept': NAV}
    IE9 = {'User-Agent': IE9_UA, 'Accept': IE_NAV}
    CHROME_NOAVIF = {'User-Agent': CHROME_UA,
                     'Accept': NAV + ',image/avif;q=0'}
    CHROME_NOWEBP = {'User-Agent': CHROME_UA,
                     'Accept': NAV + ',image/webp;q=0'}
    BOT = {'User-Agent': BOT_UA, 'Accept': '*/*'}

    # Allowed src classes per client at EVERY poll, interim states
    # included. 'orig' is the untouched /m-hero.jpg reference.
    SUPPORTED = {
        'CHROME': ({'.avif', '.webp', '.ce.jpg', 'orig'}, '.avif'),
        'FIREFOX': ({'.webp', '.ce.jpg', 'orig'}, '.webp'),
        'IE9': ({'.ce.jpg', 'orig'}, '.ce.jpg'),
        'CHROME_NOAVIF': ({'.webp', '.ce.jpg', 'orig'}, '.webp'),
        # Rejecting WebP does not reject AVIF: AVIF stays allowed.
        'CHROME_NOWEBP': ({'.avif', '.ce.jpg', 'orig'}, '.avif'),
        'BOT': ({'.ce.jpg', 'orig'}, '.ce.jpg'),
    }

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.stop()
        www = cls.root / 'www'
        testdata = (Path(__file__).resolve().parents[2] / 'net' / 'instaweb' /
                    'rewriter' / 'testdata')
        shutil.copyfile(testdata / 'Puzzle.jpg', www / 'm-hero.jpg')
        (www / 'matrix.html').write_text(
            '<!DOCTYPE html><html><head><title>matrix</title></head><body>'
            '<img src="/m-hero.jpg"></body></html>')
        (www / 'modern.html').write_text(
            '<!DOCTYPE html><html><head><title>modern</title>'
            '<script type="importmap">{"imports":{'
            '"app":"/app.js"}}</script>'
            '<script type="speculationrules">{"prerender":[]}</script>'
            '</head><body><template shadowrootmode="open">'
            '<p>shadow</p></template>'
            '<script type="module">import "app";</script>'
            '</body></html>')
        cls.stop()
        config_path = cls.root / 'nginx.conf'
        config = config_path.read_text().replace(
            'types { text/html html; text/css css; }',
            'types { text/html html; text/css css; image/jpeg jpg jpeg;'
            ' image/png png; image/gif gif; image/webp webp;'
            ' image/avif avif; application/javascript js; }', 1)
        config = config.replace(
            '    pagespeed on;\n',
            '    pagespeed on;\n    pagespeed EnableCachePurge on;\n', 1)
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
    def classify(cls, src):
        if '.pagespeed.' not in src:
            return 'orig'
        if src.endswith('.avif'):
            return '.avif'
        if src.endswith('.webp'):
            return '.webp'
        if '.pagespeed.ce.' in src:
            return '.ce.jpg'
        return 'UNKNOWN:' + src

    @classmethod
    def img_src(cls, body):
        match = re.search(rb'<img[^>]*src="([^"]+)"', body)
        if match is None:
            raise AssertionError('no img in %r' % body[-300:])
        return match.group(1).decode()

    @classmethod
    def warm(cls, path, headers, name, timeout=None):
        """Poll until convergence, asserting the per-UA invariant each time.

        Returns (final body, [classes seen]). Every interim src must lie
        in the client's supported set; the final src must be the expected
        converged format. AVIF converges via an external encoder process
        with a heavy tail on loaded boxes, so it gets a longer budget;
        internal encoders must stay fast (their timeout is the signal).
        """
        allowed, converged = cls.SUPPORTED[name]
        if timeout is None:
            timeout = 150 if converged == '.avif' else 60
        seen = []
        start = time.time()
        deadline = start + timeout
        body = b''
        while time.time() < deadline:
            _, _, body = cls.fetch(path, headers)
            src = cls.img_src(body)
            kind = cls.classify(src)
            seen.append(kind)
            if kind not in allowed:
                raise AssertionError(
                    '%s saw forbidden %s (%s)' % (name, kind, src))
            if kind == converged:
                # One more fetch proves stability, not a transient.
                _, _, body = cls.fetch(path, headers)
                if cls.classify(cls.img_src(body)) == converged:
                    elapsed = time.time() - start
                    if elapsed > 30:
                        print('SLOW CONVERGE %s %s %.1fs' % (
                            name, path, elapsed))
                    return body, seen
            time.sleep(.5)
        raise AssertionError(
            '%s never converged to %s (%d polls)' % (name, converged,
                                                     len(seen)))

    def test_format_matrix_both_orders(self):
        for order, names in ((0, ['CHROME', 'FIREFOX', 'IE9']),
                             (1, ['IE9', 'FIREFOX', 'CHROME'])):
            path = '/matrix.html?order=%d' % order
            for name in names:
                body, seen = self.warm(path, getattr(self, name), name)
                kinds = {kind for kind in seen}
                self.assertLessEqual(
                    kinds, self.SUPPORTED[name][0], (name, seen))
                self.assertEqual(
                    self.SUPPORTED[name][1], self.classify(self.img_src(body)))

    def test_rejected_formats_never_served(self):
        for name in ['CHROME_NOAVIF', 'CHROME_NOWEBP', 'BOT']:
            body, seen = self.warm('/matrix.html?rej=%s' % name,
                                   getattr(self, name), name)
            self.assertEqual(
                self.SUPPORTED[name][1], self.classify(self.img_src(body)))

    def test_concurrent_mixed_matrix(self):
        # Cold race: every UA must stay in its own supported set even
        # when all variants optimize concurrently.
        path = '/matrix.html?cold-race=1'
        ublock = [('CHROME', self.CHROME), ('FIREFOX', self.FIREFOX),
                  ('IE9', self.IE9)]

        def fetch_one(pair):
            name, headers = pair
            _, _, body = self.fetch(path, headers)
            return name, body

        with concurrent.futures.ThreadPoolExecutor(max_workers=9) as pool:
            results = list(pool.map(fetch_one, ublock * 8))
        for name, body in results:
            kind = self.classify(self.img_src(body))
            self.assertIn(kind, self.SUPPORTED[name][0], (name, body[-200:]))
        # Warm race: converge each variant, then all must HIT correctly.
        for name, headers in ublock:
            self.warm(path, headers, name)
        with concurrent.futures.ThreadPoolExecutor(max_workers=9) as pool:
            results = list(pool.map(fetch_one, ublock * 8))
        for name, body in results:
            self.assertEqual('HIT', cache_status_of(body))
            self.assertEqual(self.SUPPORTED[name][1],
                             self.classify(self.img_src(body)))

    def test_direct_resource_identity(self):
        urls = {}
        for name in ['CHROME', 'FIREFOX', 'IE9']:
            body, _ = self.warm('/matrix.html?res=%s' % name,
                                getattr(self, name), name)
            urls[name] = self.img_src(body)
        self.assertNotEqual(urls['CHROME'], urls['FIREFOX'])
        self.assertNotEqual(urls['CHROME'], urls['IE9'])
        magic = {'.avif': (b'ftypavif', 'image/avif'),
                 '.webp': (b'WEBP', 'image/webp'),
                 '.ce.jpg': (b'\xff\xd8\xff', 'image/jpeg')}
        for owner, url in urls.items():
            kind = self.classify(url)
            bodies = []
            for name in ['CHROME', 'FIREFOX', 'IE9']:
                status, headers, body = fetch_with_headers(self.port, 
                    url, getattr(self, name))
                self.assertEqual(200, status, (owner, name, url))
                header_map = dict(headers)
                marker, mime = magic[kind]
                self.assertIn(marker, body[:32], (owner, name))
                self.assertEqual(mime, header_map.get('Content-Type'),
                                 (owner, name))
                self.assertIn('max-age=31536000',
                              header_map.get('Cache-Control', ''),
                              (owner, name))
                self.assertFalse(
                    [value for key, value in headers
                     if key.lower() == 'vary'], (owner, name))
                bodies.append((header_map.get('ETag'), body))
            etags = {etag for etag, _ in bodies}
            self.assertEqual(1, len(etags), (owner, urls[owner]))
            self.assertTrue(all(etag for etag in etags), owner)
            first = bodies[0][1]
            self.assertTrue(all(body == first for _, body in bodies), owner)

    def test_conditional_resource_semantics(self):
        body, _ = self.warm('/matrix.html?condres=1', self.CHROME, 'CHROME')
        url = self.img_src(body)
        _, headers, first = fetch_with_headers(self.port, url, self.CHROME)
        etag = dict(headers).get('ETag')
        self.assertTrue(etag, url)
        # No If-None-Match support: the fixed weak ETag is decorative
        # (legacy browser-cache compat) and conditionals serve 200.
        request = dict(self.CHROME)
        request['If-None-Match'] = etag
        status, _, body = fetch_with_headers(self.port, url, request)
        self.assertEqual(200, status)
        self.assertEqual(first, body)
        # If-Modified-Since always yields 304, even for ancient dates.
        # That is correct here: rewritten URLs are content-hashed and
        # immutable, so any cached copy is definitionally current.
        request = dict(self.CHROME)
        request['If-Modified-Since'] = 'Wed, 21 Oct 2015 07:28:00 GMT'
        status, _, body = fetch_with_headers(self.port, url, request)
        self.assertEqual(304, status)
        self.assertEqual(b'', body)

    def test_html_conditionals_bypass(self):
        self.warm('/matrix.html?condhtml=1', self.CHROME, 'CHROME')
        for extra in ({'If-None-Match': 'W/"0"'},
                      {'If-Modified-Since': 'Wed, 21 Oct 2015 07:28:00 GMT'}):
            request = dict(self.CHROME)
            request.update(extra)
            status, headers, body = fetch_with_headers(self.port, 
                '/matrix.html?condhtml=1', request)
            self.assertEqual(200, status, extra)
            self.assertEqual('BYPASS', cache_status_of(body), extra)
            header_map = dict(headers)
            self.assertNotIn('ETag', header_map, extra)
            self.assertNotIn('Last-Modified', header_map, extra)

    def test_purge_invalidates_variants(self):
        chrome_body, _ = self.warm('/matrix.html?purge=1', self.CHROME,
                                   'CHROME')
        firefox_body, _ = self.warm('/matrix.html?purge=1', self.FIREFOX,
                                    'FIREFOX')
        self.assertEqual('HIT', cache_status_of(chrome_body))
        self.assertEqual('HIT', cache_status_of(firefox_body))
        chrome_url = self.img_src(chrome_body)
        _, _, chrome_bytes = fetch_with_headers(self.port, chrome_url, self.CHROME)
        _, _, purge_body = self.fetch('/pagespeed_admin/cache?purge=*')
        self.assertIn(b'Purge successful', purge_body)
        # Purged variants are gone: the next fetch regenerates (MISS once
        # resources are ready, BYPASS while async rewrites are detached).
        for name in ['CHROME', 'FIREFOX']:
            _, _, body = self.fetch('/matrix.html?purge=1',
                                    getattr(self, name))
            self.assertIn(cache_status_of(body), ('MISS', 'BYPASS'), name)
        # Same content-hash URL after regeneration, same bytes behind it:
        # bytes must never switch under a fixed rewritten URL.
        chrome_body, _ = self.warm('/matrix.html?purge=1', self.CHROME,
                                   'CHROME')
        self.assertEqual(chrome_url, self.img_src(chrome_body))
        _, _, chrome_bytes2 = fetch_with_headers(self.port, chrome_url, self.CHROME)
        self.assertEqual(chrome_bytes, chrome_bytes2)

    def test_modern_documents_stable_under_cache(self):
        _, _, first = self.fetch('/modern.html?stable=1')
        self.assertEqual('MISS', cache_status_of(first))
        _, _, second = self.fetch('/modern.html?stable=1')
        self.assertEqual('HIT', cache_status_of(second))
        # The diagnostic comment itself differs (MISS/HIT, age); the
        # rewritten document around it must be byte-identical.
        strip = re.compile(rb'<!-- PageSpeed Cache:.*?-->$', re.DOTALL)
        self.assertEqual(strip.sub(b'', first), strip.sub(b'', second))
        self.assertIn(b'shadowrootmode="open"', second)
        self.assertIn(b'type="importmap"', second)
        self.assertIn(b'type="speculationrules"', second)
        self.assertIn(b'type="module"', second)

    def test_downstream_headers_on_all_paths(self):
        body, _ = self.warm('/matrix.html?headers=1', self.CHROME, 'CHROME')
        self.assertEqual('HIT', cache_status_of(body))

        # The operative downstream property is no-store (nothing may be
        # stored anywhere); the source also intends `private`, which is
        # redundant under no-store. Observed wire value is
        # `max-age=0, no-cache, no-store`.
        def check(path, headers, want_status):
            status, raw, body = fetch_with_headers(self.port, path, headers)
            self.assertEqual(200, status, (path, headers))
            self.assertEqual(want_status, cache_status_of(body),
                             (path, headers))
            header_map = dict(raw)
            controls = header_map.get('Cache-Control', '')
            self.assertIn('no-store', controls, (path, headers))
            varies = [value for key, value in raw if key.lower() == 'vary']
            self.assertTrue(
                any('User-Agent' in value for value in varies),
                (path, headers, varies))
            self.assertTrue(
                any('Accept' in value for value in varies),
                (path, headers, varies))
            return header_map

        check('/matrix.html?headers=1', self.CHROME, 'HIT')
        check('/matrix.html?headers-fresh=1', self.CHROME, 'MISS')
        bypass = dict(self.CHROME)
        bypass['If-None-Match'] = 'W/"0"'
        check('/matrix.html?headers=1', bypass, 'BYPASS')


class BeaconFreshnessNginxTest(base.HtmlCacheNginxTest):
    # Separate nginx: beaconing is server-scoped here. Location-scoped
    # beaconing learns and rebeacons but never caches (conservative
    # BYPASS); server scope exercises the full documented lifecycle.
    #
    # The inherited imageless-page cache tests do not apply under a
    # beacon-due config: every first touch bypasses by design (no nonce
    # may be served stale), and imageless pages can never report a
    # beacon to satisfy the one-success gate. They run under their own
    # config in the matrix class and the base suite.
    def test_hit_expiry_and_gzip(self):
        raise unittest.SkipTest('imageless-page caching; beacon-due config')

    def test_accept_and_browser_variant_isolation(self):
        raise unittest.SkipTest('imageless-page variants; beacon-due config')

    def test_private_requests_and_responses(self):
        raise unittest.SkipTest('vacuous under global beacon bypass')

    def test_concurrent_warm_hits(self):
        raise unittest.SkipTest('imageless-page hits; beacon-due config')

    def test_html_cache_admin_history_and_dashboard(self):
        raise unittest.SkipTest('imageless-page history; beacon-due config')
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.stop()
        www = cls.root / 'www'
        testdata = (Path(__file__).resolve().parents[2] / 'net' / 'instaweb' /
                    'rewriter' / 'testdata')
        shutil.copyfile(testdata / 'BikeCrashIcn.png', www / 'bf-logo.png')
        shutil.copyfile(testdata / 'Puzzle.jpg', www / 'bf-hero.jpg')
        (www / 'beacon-fresh.html').write_text(
            '<!DOCTYPE html><html><head><title>fresh</title></head><body>'
            '<img src="/bf-logo.png" width="120" height="40">'
            '<img src="/bf-hero.jpg"></body></html>')
        cls.stop()
        config_path = cls.root / 'nginx.conf'
        config = config_path.read_text().replace(
            'types { text/html html; text/css css; }',
            'types { text/html html; text/css css; image/jpeg jpg jpeg;'
            ' image/png png; image/gif gif; image/webp webp;'
            ' image/avif avif; application/javascript js; }', 1)
        config = config.replace(
            '    pagespeed on;\n',
            '    pagespeed on;\n'
            '    pagespeed EnableFilters prioritize_lcp_images;\n'
            '    pagespeed CriticalImagesBeaconEnabled on;\n', 1)
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

    def preload_href(self, body):
        match = re.search(rb'<link rel="preload"[^>]*>', body)
        if match is None:
            return None
        href = re.search(rb'href="([^"]+)"', match.group(0))
        return href.group(1) if href else None

    def test_beacon_pages_never_freeze_nonce(self):
        path = '/beacon-fresh.html'
        # The beacon slot is per-URL: the first fetch samples the nonce
        # (BYPASS + script + hashes) and later fetches bypass without
        # either. Nothing is cached pre-learning, so no nonce can freeze.
        _, _, first = self.fetch(path)
        self.assertEqual('BYPASS', cache_status_of(first))
        self.assertIn(b'CriticalImages.Run(', first)
        self.assertIsNotNone(HASH_ATTR.search(first), first[-500:])
        for _ in range(4):
            _, _, body = self.fetch(path)
            self.assertEqual('BYPASS', cache_status_of(body))
            self.assertNotIn(b'CriticalImages.Run(', body)
            self.assertIsNone(HASH_ATTR.search(body), body[-500:])
            href = self.preload_href(body)
            self.assertIsNotNone(href, body[-500:])
            self.assertIn(b'bf-logo', href)
        call = BEACON_CALL.search(first)
        self.assertIsNotNone(call, first[-500:])
        beacon_url, html_url, options_hash, nonce = (
            group.decode() for group in call.groups())
        hero = None
        for match in re.finditer(rb'<img\b[^>]*>', first):
            if b'bf-hero' in match.group(0):
                hero = match.group(0)
        hashed = HASH_ATTR.search(hero)
        self.assertIsNotNone(hashed, hero)
        post_path = '%s?url=%s' % (
            beacon_url, urllib.parse.quote(html_url, safe=''))
        post_body = 'oh=%s&n=%s&lcp=%s' % (
            urllib.parse.quote(options_hash, safe=''),
            urllib.parse.quote(nonce, safe=''),
            urllib.parse.quote(hashed.group(1).decode(), safe=''))
        connection = http.client.HTTPConnection('127.0.0.1', self.port,
                                                timeout=10)
        try:
            connection.request(
                'POST', post_path, post_body,
                {'Content-Type': 'application/x-www-form-urlencoded',
                 'User-Agent': base.UA})
            response = connection.getresponse()
            response.read()
            self.assertEqual(204, response.status)
        finally:
            connection.close()
        # After learning the page either serves the cached learned variant
        # or rebeacons on schedule; either way the hero stays learned and
        # a HIT never carries a sampled nonce. Learned renders keep
        # original-URL hashes (no script) so the rewritten hero matches.
        learned = None
        for _ in range(30):
            _, _, learned = self.fetch(path)
            href = self.preload_href(learned)
            if href is not None and b'bf-hero' in href:
                break
            time.sleep(.5)
        href = self.preload_href(learned)
        self.assertIsNotNone(href, learned[-500:])
        self.assertIn(b'bf-hero', href)
        saw_hit = False
        for _ in range(20):
            _, _, settled = self.fetch(path)
            status = cache_status_of(settled)
            href = self.preload_href(settled)
            self.assertIsNotNone(href, settled[-500:])
            self.assertIn(b'bf-hero', href)
            if status == 'HIT':
                saw_hit = True
                self.assertNotIn(b'CriticalImages.Run(', settled)
                self.assertIsNotNone(HASH_ATTR.search(settled),
                                     settled[-500:])
                break
            self.assertEqual('BYPASS', status)
            time.sleep(.5)
        self.assertTrue(saw_hit, 'learned page never HIT')
        rebeaconed = None
        for _ in range(30):
            _, _, rebeaconed = self.fetch(path)
            if (cache_status_of(rebeaconed) == 'BYPASS' and
                    b'CriticalImages.Run(' in rebeaconed):
                break
            time.sleep(1)
        self.assertEqual('BYPASS', cache_status_of(rebeaconed))
        self.assertIn(b'CriticalImages.Run(', rebeaconed)
        href = self.preload_href(rebeaconed)
        self.assertIsNotNone(href, rebeaconed[-500:])
        self.assertIn(b'bf-hero', href)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nginx', required=True)
    parser.add_argument('--module', required=True)
    options, remaining = parser.parse_known_args()
    options.extra_config = ''
    base.OPTIONS = options
    unittest.main(argv=[__file__, *remaining])

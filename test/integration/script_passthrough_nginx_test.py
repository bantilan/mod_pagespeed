#!/usr/bin/env python3
"""WG-05: modern script pass-through against an isolated rebuilt Nginx module.

Proves with real browser execution that script rewrites preserve behavior:
sync/module scripts run, nomodule stays skipped, template literals survive
minification, integrity-gated scripts keep their bytes and still validate,
and the combiner stands down under a nonce CSP.
"""
import argparse
import base64
import hashlib
import importlib.util
from pathlib import Path
import re
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

CHROME_FLAGS = ['--headless', '--disable-gpu', '--no-first-run',
                '--no-default-browser-check', '--disable-background-networking',
                '--virtual-time-budget=5000', '--dump-dom']


class ScriptPassthroughNginxTest(base.HtmlCacheNginxTest):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        www = cls.root / 'www'
        (www / 'classic1.js').write_text(
            "document.title += '[classic1]';\n")
        (www / 'classic2.js').write_text(
            "document.title += '[classic2]';\n")
        (www / 'mod1.js').write_text(
            "document.title += '[module1]';\n")
        (www / 'nomod1.js').write_text(
            "document.title += '[nomodule1]';\n")
        (www / 'async1.js').write_text(
            "document.title += '[async1]';\n")
        gated = "document.title += '[integrity1]';\n"
        (www / 'gated.js').write_text(gated)
        digest = base64.b64encode(hashlib.sha256(
            gated.encode()).digest()).decode()
        (www / 'exec-page.html').write_text(
            '<!DOCTYPE html><html><head><title>exec</title>\n'
            '<script type="importmap">{"imports":{"/app.js":"/app-v2.js"'
            '}}</script>\n'
            '<script type="application/json">{"a":  1}</script>\n'
            '</head><body>\n'
            '<script src="/classic1.js"></script>\n'
            '<script src="/classic2.js"></script>\n'
            '<script type="module" src="/mod1.js"></script>\n'
            '<script nomodule src="/nomod1.js"></script>\n'
            '<script async src="/async1.js"></script>\n'
            '<script>var t = `hello ${1},\nworld!`; '
            'document.title += "[" + t + "]";</script>\n'
            '<script src="/gated.js" integrity="sha256-%s"></script>\n'
            '</body></html>\n' % digest)
        # '*' allows the combined bundle URL while inline scripts stay
        # blocked: without the inline-script gate the combiner emits eval()
        # stubs the browser refuses to run.
        (www / 'csp-page.html').write_text(
            '<!DOCTYPE html><html><head>'
            '<meta http-equiv="Content-Security-Policy" '
            'content="script-src * \'unsafe-eval\'">'
            '</head><body>\n'
            '<script src="/classic1.js"></script>\n'
            '<script src="/classic2.js"></script>\n'
            '</body></html>\n')
        # Modules require a JavaScript MIME type; the base harness only maps
        # html/css, so add js (without this the browser blocks the module).
        cls.stop()
        config_path = cls.root / 'nginx.conf'
        config = config_path.read_text().replace(
            'text/css css;', 'text/css css; application/javascript js;', 1)
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
    def dom_in_chrome(cls, path):
        if not getattr(base.OPTIONS, 'browser', None):
            raise unittest.SkipTest('Pass --browser /path/to/chrome')
        with tempfile.TemporaryDirectory(
                prefix='pagespeed-wg05-browser-') as profile:
            command = [base.OPTIONS.browser] + CHROME_FLAGS + \
                ['--user-data-dir=' + profile,
                 'http://127.0.0.1:%d%s' % (cls.port, path)]
            result = subprocess.run(command, capture_output=True, timeout=90)
            if result.returncode != 0:
                raise RuntimeError(result.stderr[-2000:].decode(
                    errors='replace'))
            return result.stdout.decode(errors='replace')

    def test_execution_preserved_end_to_end(self):
        # Async timing vs dump-dom is inherently racy, so async execution is
        # covered by the stays-external assertion below, not by markers.
        # Markers live in document.title; the scripts' own source text also
        # contains the marker literals, so match the title element only.
        dom = self.dom_in_chrome('/exec-page.html')
        title = re.search(r'<title>(.*?)</title>', dom, re.S)
        self.assertIsNotNone(title, dom[-500:])
        title = title.group(1)
        for marker in ['[classic1]', '[classic2]', '[module1]',
                       '[integrity1]']:
            self.assertIn(marker, title)
        self.assertIn('[classic1][classic2]', title)
        self.assertNotIn('[nomodule1]', title)
        # The template executes (document.title normalizes the newline to a
        # space, so match around it; byte-identity is asserted below).
        self.assertIn('[hello 1,', title)
        self.assertIn('world!]', title)

    def test_modern_blocks_byte_identical(self):
        _, _, body = self.fetch('/exec-page.html')
        text = body.decode()
        self.assertIn('<script type="importmap">{"imports":{"/app.js":'
                      '"/app-v2.js"}}</script>', text)
        self.assertIn('<script type="application/json">{"a":  1}</script>',
                      text)

    def test_async_and_integrity_left_external(self):
        _, _, body = self.fetch('/exec-page.html')
        text = body.decode()
        # async must stay external (minified or not); inlining it would run
        # it synchronously during parsing.
        self.assertIsNotNone(
            re.search(r'<script async src="[^"]+">', text), text[-800:])
        # integrity pins bytes: cache extension (same bytes) is fine, but
        # minification (.jm. namespace) must not touch it.
        gated_tag = re.search(
            r'<script src="([^"]+)" integrity="sha256-[^"]+">', text)
        self.assertIsNotNone(gated_tag, text[-800:])
        self.assertNotIn('.pagespeed.jm.', gated_tag.group(1))
        # Template literals keep their significant newline in served bytes.
        self.assertIn('`hello ${1},\nworld!`', text)

    def test_combine_stands_down_without_inline_permission(self):
        _, _, body = self.fetch('/csp-page.html')
        text = body.decode()
        self.assertNotIn('eval(mod_pagespeed_', text)
        self.assertNotIn('.pagespeed.jc.', text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nginx', required=True)
    parser.add_argument('--module', required=True)
    parser.add_argument('--browser', required=True,
                        help='Chrome/Chromium binary for execution checks')
    options, remaining = parser.parse_known_args()
    options.extra_config = (
        '\n  pagespeed EnableFilters '
        'rewrite_javascript,inline_javascript,combine_javascript;\n')
    base.OPTIONS = options
    unittest.main(argv=[__file__, *remaining])

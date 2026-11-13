#!/usr/bin/env python3
"""WG-01: explicit polyfill removal against an isolated rebuilt Nginx module."""
import argparse
import importlib.util
from pathlib import Path
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


class PolyfillRemovalNginxTest(base.HtmlCacheNginxTest):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.stop()
        www = cls.root / 'www'
        (www / 'polyfills.min.js').write_text(
            'window.requiredFeature = function() { return 42; };')
        (www / 'approved.js').write_text('window.approvedRan = true;')
        (www / 'fallback.js').write_text('window.legacyFallback = true;')
        filename_html = '''<!DOCTYPE html><html><head><title>Approved filenames</title>
<script id="wp" src="/wp-includes/vendor/wp-polyfill.min.js?ver=6.9"></script>
<script id="extra" src="legacy-extra.js?ver=1#hash"></script>
<script id="relative" src="../vendor/wp-polyfill.min.js?ver=2"></script>
<script id="cdn" src="//cdn.example.test/wp-polyfill.min.js?ver=3"></script>
<script id="absolute" src="https://cdn.example.test/legacy-extra.js"></script>
<script id="prefix" src="/prefix-wp-polyfill.min.js"></script>
<script id="suffix" src="/wp-polyfill.min.js.backup"></script>
<script id="query-only" src="/app.js?file=wp-polyfill.min.js"></script>
<script id="host-only" src="https://wp-polyfill.min.js/app.js"></script>
<script id="fragment-only" src="#wp-polyfill.min.js"></script>
<script id="encoded" src="/%77p-polyfill.min.js"></script>
<script id="case" src="/WP-polyfill.min.js"></script>
<script id="data-source" src="data:application/javascript,wp-polyfill.min.js"></script>
<script id="listed-module" type="module" src="/wp-polyfill.min.js"></script>
<script id="listed-nomodule" nomodule src="/wp-polyfill.min.js"></script>
<script id="listed-protected" data-pagespeed-no-transform src="/wp-polyfill.min.js"></script>
<script id="local" src="/location-only.js"></script>
</head><body>Filename approval fixture</body></html>'''
        html = '''<!DOCTYPE html><html><head><title>Polyfill dependency</title>
<script id="required" src="/polyfills.min.js" nonce="test-nonce"
        crossorigin="anonymous" referrerpolicy="no-referrer"></script>
<script id="approved" src="/approved.js" data-pagespeed-remove-polyfill="true"></script>
<script id="fallback" src="/fallback.js" nomodule data-pagespeed-remove-polyfill="true"></script>
<script id="protected" src="/fallback.js" data-pagespeed-no-transform
        data-pagespeed-remove-polyfill="true"></script>
<script id="module" type="module" src="/fallback.js"
        data-pagespeed-remove-polyfill="true"></script>
<script id="data" type="application/json" data-pagespeed-remove-polyfill="true">{}</script>
</head><body><p id="runtime-result">NOT RUN</p><script>
document.getElementById('runtime-result').textContent =
    typeof window.requiredFeature === 'function' && requiredFeature() === 42 &&
    !window.approvedRan ? 'PASS' : 'FAIL';
</script></body></html>'''
        (www / 'polyfill.html').write_text(html)
        (www / 'polyfill-off.html').write_text(html)
        (www / 'filenames.html').write_text(filename_html)
        (www / 'filenames-local.html').write_text(filename_html)
        (www / 'filenames-off.html').write_text(filename_html)
        config_path = cls.root / 'nginx.conf'
        config = config_path.read_text().replace(
            'text/css css;', 'text/css css; application/javascript js;')
        config = config.replace('    location = /off.html {',
            '    location = /polyfill-off.html {\n'
            '      pagespeed DisableFilters strip_legacy_polyfills;\n'
            '    }\n    location = /off.html {')
        config = config.replace('    location = /off.html {',
            '    location = /filenames-local.html {\n'
            '      pagespeed StripLegacyPolyfillFilename "location-only.js";\n'
            '    }\n'
            '    location = /filenames-off.html {\n'
            '      pagespeed DisableFilters strip_legacy_polyfills;\n'
            '    }\n    location = /off.html {')
        config_path.write_text(config)
        cls.process = subprocess.Popen(
            [base.OPTIONS.nginx, '-p', str(cls.root) + '/', '-c', str(config_path),
             '-e', str(cls.root / 'logs/error.log')], stdout=cls.log, stderr=cls.log)
        for _ in range(100):
            if cls.process.poll() is not None:
                raise RuntimeError((cls.root / 'logs/error.log').read_text())
            try:
                cls.fetch('/style.css')
                return
            except OSError:
                time.sleep(.05)
        raise RuntimeError('Isolated Nginx did not start')

    def assert_safe_scripts(self, body):
        self.assertIn(b'id="required"', body)
        self.assertIn(b'/polyfills.min.js', body)
        self.assertIn(b'nonce="test-nonce"', body)
        self.assertIn(b'id="fallback"', body)
        self.assertIn(b'id="protected"', body)
        self.assertIn(b'id="module"', body)
        self.assertIn(b'id="data"', body)

    def test_marked_removal_and_dependencies_survive_html_cache_hit(self):
        first = self.fetch('/polyfill.html')[2]
        self.assertEqual(b'MISS', self.info(first)[0])
        self.assert_safe_scripts(first)
        self.assertNotIn(b'id="approved"', first)
        second = self.fetch('/polyfill.html')[2]
        self.assertEqual(b'HIT', self.info(second)[0])
        self.assert_safe_scripts(second)
        self.assertNotIn(b'id="approved"', second)

    def test_disabled_filter_preserves_even_approved_scripts(self):
        for _ in range(2):
            body = self.fetch('/polyfill-off.html')[2]
            self.assert_safe_scripts(body)
            self.assertIn(b'id="approved"', body)

    def test_real_chrome_executes_preserved_dependency(self):
        if not base.OPTIONS.browser:
            self.skipTest('Pass --browser /path/to/chrome for real browser validation')
        with tempfile.TemporaryDirectory(prefix='pagespeed-wg01-browser-') as profile:
            command = [base.OPTIONS.browser, '--headless', '--disable-gpu',
                '--no-first-run', '--no-default-browser-check',
                '--disable-background-networking', '--user-data-dir=' + profile,
                '--virtual-time-budget=1000', '--dump-dom',
                'http://127.0.0.1:%d/polyfill.html' % self.port]
            result = subprocess.run(command, capture_output=True, timeout=45)
            self.assertEqual(0, result.returncode, result.stderr.decode(errors='replace'))
            self.assertIn(b'id="runtime-result">PASS</p>', result.stdout)

    def test_multiple_quoted_filenames_match_only_final_url_component(self):
        for attempt in range(2):
            body = self.fetch('/filenames.html')[2]
            self.assertEqual(b'MISS' if attempt == 0 else b'HIT', self.info(body)[0])
            for removed in ['wp', 'extra', 'relative', 'cdn', 'absolute']:
                self.assertNotIn(('id="%s"' % removed).encode(), body)
            for kept in ['prefix', 'suffix', 'query-only', 'host-only', 'fragment-only',
                         'encoded', 'case', 'data-source', 'listed-module',
                         'listed-nomodule', 'listed-protected', 'local']:
                self.assertIn(('id="%s"' % kept).encode(), body)

    def test_location_inherits_filenames_and_can_disable_filter(self):
        body = self.fetch('/filenames-local.html')[2]
        for removed in ['wp', 'extra', 'local']:
            self.assertNotIn(('id="%s"' % removed).encode(), body)
        body = self.fetch('/filenames-off.html')[2]
        for kept in ['wp', 'extra', 'local']:
            self.assertIn(('id="%s"' % kept).encode(), body)

    def test_filename_directive_rejects_paths_and_wildcards(self):
        original = (self.root / 'nginx.conf').read_text()
        for invalid in ['*.js', '/path/polyfill.js', 'one.js,two.js', 'file.js?ver=1']:
            with tempfile.NamedTemporaryFile(mode='w', suffix='.conf',
                                             dir=self.root, delete=False) as config:
                config.write(original.replace('"wp-polyfill.min.js"',
                                               '"%s"' % invalid, 1))
                name = config.name
            try:
                result = subprocess.run([base.OPTIONS.nginx, '-t', '-p',
                    str(self.root) + '/', '-c', name], capture_output=True, timeout=15)
                self.assertNotEqual(0, result.returncode, invalid)
                self.assertIn(b'filename', result.stderr.lower())
            finally:
                Path(name).unlink()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nginx', required=True)
    parser.add_argument('--module', required=True)
    parser.add_argument('--browser', help='Optional Chrome/Chromium binary; default headers retained')
    options, remaining = parser.parse_known_args()
    options.extra_config = '''
  pagespeed EnableFilters strip_legacy_polyfills;
  pagespeed StripLegacyPolyfillFilename "wp-polyfill.min.js";
  pagespeed StripLegacyPolyfillFilename "legacy-extra.js";
  pagespeed DisableFilters rewrite_javascript,inline_javascript,combine_javascript,defer_javascript;
'''
    base.OPTIONS = options
    unittest.main(argv=[__file__, *remaining])

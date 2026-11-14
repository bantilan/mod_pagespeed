#!/usr/bin/env python3
"""SEC-05 #8: script-variable option evaluation on the request path.

The lane enables ProcessScriptVariables and serves a page through a location
whose EnableFilters value comes from the request's own query argument. A value
the configuration rejects must leave the request served without optimization,
not failed; a valid value must reach the rewrite pipeline.
"""
import importlib.util
from pathlib import Path
import subprocess
import sys
import time
import unittest

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    'html_cache_tests', Path(__file__).with_name('html_cache_nginx_test.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)


class ScriptVariablesNginxTest(base.HtmlCacheNginxTest):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        www = cls.root / 'www'
        # Large enough to stay out of inline_css (core): the control case
        # must combine (`.pagespeed.cc.`), not inline.
        (www / 'a.css').write_text('a { color: red; }\n' * 200)
        (www / 'b.css').write_text('b { color: blue; }\n' * 200)
        scripted = www / 'script_variables'
        scripted.mkdir()
        (scripted / 'combine_css.html').write_text(
            '<!DOCTYPE html><html><head>'
            '<link rel="stylesheet" href="/a.css">'
            '<link rel="stylesheet" href="/b.css">'
            '</head><body><p>combine</p></body></html>')
        cls.stop()
        config_path = cls.root / 'nginx.conf'
        config = config_path.read_text()
        config = config.replace(
            '  pagespeed FileCachePath',
            '  pagespeed ProcessScriptVariables all;\n'
            '  pagespeed FileCachePath', 1)
        config = config.replace(
            '    location / { expires 10m; }',
            '    location / { expires 10m; }\n'
            '    location ^~ /script_variables/ {\n'
            '      pagespeed EnableFilters $arg_filters;\n'
            '    }', 1)
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

    def fetch_until_contains(self, path, pattern, timeout=60.0):
        deadline = time.time() + timeout
        last = b''
        while time.time() < deadline:
            status, _, last = self.fetch(path)
            self.assertEqual(200, status)
            if pattern in last:
                return last
            time.sleep(0.5)
        return last

    def test_invalid_value_served_without_optimization(self):
        """An invalid filter name answers 200 with the page untouched."""
        status, _, body = self.fetch(
            '/script_variables/combine_css.html?filters=not_a_filter')
        self.assertEqual(200, status, body[:200])
        self.assertNotIn(b'.pagespeed.', body, body[:200])

    def test_valid_value_optimizes(self):
        """A valid filter name reaches the rewrite pipeline (control)."""
        body = self.fetch_until_contains(
            '/script_variables/combine_css.html?filters=combine_css',
            b'a.css+b.css')
        self.assertIn(b'a.css+b.css', body, body[:200])
        self.assertIn(b'.pagespeed.', body, body[:200])


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nginx', required=True)
    parser.add_argument('--module', required=True)
    options, remaining = parser.parse_known_args()
    options.extra_config = ''
    base.OPTIONS = options
    unittest.main(argv=[__file__, *remaining])

#!/usr/bin/env python3
"""WG-02: CSS document-kind safety against an isolated rebuilt Nginx module."""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys
import time
import unittest
import xml.etree.ElementTree as ElementTree

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    'html_cache_tests', Path(__file__).with_name('html_cache_nginx_test.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)


SHARED_CSS = '@media (width < 600px){a{margin:0}}'


class CssDocumentKindNginxTest(base.HtmlCacheNginxTest):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.stop()
        www = cls.root / 'www'
        (www / 'shared.css').write_text(SHARED_CSS)
        link = '<link rel="stylesheet" href="/shared.css">'
        html_page = ('<!DOCTYPE html><html><head><title>CSS doc kind</title>' +
                     link + '</head><body>html</body></html>')
        # XHTML served as application/xhtml+xml; the '<' in the shared
        # stylesheet must keep it external so the document stays well-formed.
        xhtml_link = '<link rel="stylesheet" href="/shared.css" />'
        xhtml_page = ('<?xml version="1.0" encoding="utf-8"?>'
                      '<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN"'
                      ' "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">'
                      '<html xmlns="http://www.w3.org/1999/xhtml"><head>'
                      '<title>CSS doc kind</title>' + xhtml_link + '</head>'
                      '<body><p>xhtml</p></body></html>')
        (www / 'order-html.html').write_text(html_page)
        (www / 'order-html.xhtml').write_text(xhtml_page)
        (www / 'order-xhtml.xhtml').write_text(xhtml_page)
        (www / 'order-xhtml.html').write_text(html_page)
        (www / 'escape.html').write_text(
            '<!DOCTYPE html><html><head><title>escape</title><style>'
            'a[title="\\3C/style\\3E"] { color: blue; }</style></head>'
            '<body>escape</body></html>')
        (www / 'inline.xhtml').write_text(
            '<?xml version="1.0" encoding="utf-8"?>'
            '<html xmlns="http://www.w3.org/1999/xhtml"><head>'
            '<title>inline</title><style>'
            'li[title="1\\3C 2\\3E 0"]{color:blue}</style></head>'
            '<body><p>inline</p></body></html>')
        (www / 'min.html').write_text(
            '<!DOCTYPE html><html><head><title>min</title><style>'
            'p { margin: 0; }</style></head><body>min</body></html>')
        config_path = cls.root / 'nginx.conf'
        config = config_path.read_text().replace(
            'text/css css;', 'text/css css; application/xhtml+xml xhtml;')
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

    def assertWellFormedXml(self, body):
        # The HTML-cache diagnostic comment is appended after </html>; the
        # document itself must parse as XML.
        head = body.split(b'<!-- PageSpeed Cache:')[0]
        ElementTree.fromstring(head)

    def test_order_html_before_xhtml(self):
        status, headers, html = self.fetch('/order-html.html')
        self.assertEqual(200, status)
        self.assertIn('text/html', headers.get('Content-Type', ''))
        self.assertIn(('<style>' + SHARED_CSS + '</style>').encode(), html)
        self.assertNotIn(b'<link', html)
        status, headers, xhtml = self.fetch('/order-html.xhtml')
        self.assertEqual(200, status)
        self.assertIn('application/xhtml+xml',
                      headers.get('Content-Type', ''))
        self.assertIn(b'<link', xhtml)
        self.assertNotIn(b'<style>', xhtml)
        self.assertWellFormedXml(xhtml)

    def test_order_xhtml_before_html(self):
        status, headers, xhtml = self.fetch('/order-xhtml.xhtml')
        self.assertEqual(200, status)
        self.assertIn(b'<link', xhtml)
        self.assertNotIn(b'<style>', xhtml)
        self.assertWellFormedXml(xhtml)
        status, headers, html = self.fetch('/order-xhtml.html')
        self.assertEqual(200, status)
        self.assertIn(('<style>' + SHARED_CSS + '</style>').encode(), html)
        self.assertNotIn(b'<link', html)

    def test_html_inline_style_breakout_is_declined(self):
        status, _, body = self.fetch('/escape.html')
        self.assertEqual(200, status)
        # Serialization would decode the escape to a literal "</style>",
        # so the block must be left byte-identical.
        self.assertIn(b'<style>a[title="\\3C/style\\3E"] { color: blue; }'
                      b'</style>', body)

    def test_xhtml_inline_style_keeps_no_literal_markup(self):
        status, headers, body = self.fetch('/inline.xhtml')
        self.assertEqual(200, status)
        self.assertIn(b'<style>li[title="1\\3C 2\\3E 0"]{color:blue}</style>',
                      body)
        self.assertWellFormedXml(body)

    def test_html_inline_style_still_minified(self):
        status, _, body = self.fetch('/min.html')
        self.assertEqual(200, status)
        self.assertIn(b'<style>p{margin:0}</style>', body)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nginx', required=True)
    parser.add_argument('--module', required=True)
    parser.add_argument('--browser',
                        help='Optional Chrome/Chromium binary (unused here; '
                             'XHTML is validated with a real XML parser)')
    options, remaining = parser.parse_known_args()
    options.extra_config = ''
    base.OPTIONS = options
    unittest.main(argv=[__file__, *remaining])

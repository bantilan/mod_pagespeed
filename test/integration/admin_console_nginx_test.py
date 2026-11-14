#!/usr/bin/env python3
"""WG-08: admin console accessibility/runtime against an isolated module.

Drives the real admin pages in real Chrome over CDP using the built
(minified) scripts: keyboard-only operation, focus containment and
restoration, tab/tabpanel semantics, chart text equivalents, error and
no-script states, hidden-tab polling, contrast and narrow widths.
"""
import argparse
import http.client
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import unittest

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    'html_cache_tests', Path(__file__).with_name('html_cache_nginx_test.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
cdp_spec = importlib.util.spec_from_file_location(
    'cdp_chrome', Path(__file__).with_name('cdp_chrome.py'))
cdp_chrome = importlib.util.module_from_spec(cdp_spec)
cdp_spec.loader.exec_module(cdp_chrome)

KEY_CODES = {'Tab': 9, 'Escape': 27, 'ArrowLeft': 37, 'ArrowUp': 38,
             'ArrowRight': 39, 'ArrowDown': 40, 'Home': 36, 'End': 35,
             'Enter': 13, ' ': 32}
SHIFT_MODIFIER = 8


def open_chrome(test):
    if not getattr(base.OPTIONS, 'browser', None):
        raise unittest.SkipTest('Pass --browser /path/to/chrome')
    chrome = cdp_chrome.CdpChrome(base.OPTIONS.browser)
    test.addCleanup(chrome.stop)
    return chrome


def admin_goto(chrome, port, leaf):
    chrome.cdp.call('Page.navigate', {'url': 'http://127.0.0.1:%d/pagespeed_admin/%s' % (port, leaf)})
    chrome.cdp.wait_for('Page.loadEventFired')
    time.sleep(1.5)


def press_key(chrome, key, shift=False):
    params = {'type': 'keyDown', 'key': key, 'code': key,
              'windowsVirtualKeyCode': KEY_CODES[key]}
    if shift:
        params['modifiers'] = SHIFT_MODIFIER
    chrome.cdp.call('Input.dispatchKeyEvent', params)
    params['type'] = 'keyUp'
    chrome.cdp.call('Input.dispatchKeyEvent', params)
    time.sleep(.15)


def active_label(chrome):
    return chrome.cdp.evaluate(
        '(function(){var el=document.activeElement;'
        'if(!el)return "none";'
        'var cls=el.className&&el.className.baseVal!==undefined?'
        'el.className.baseVal:el.className;'
        'return el.tagName+"."+cls+"#"+el.id;})()')


def warm_admin_images(test, cls):
    # Sequential warms: hero1 (Puzzle, large savings) completes strictly
    # before hero2 (small file, small savings), so both savings order
    # [hero1, hero2] and recency order [hero2, hero1] are deterministic.
    # Idempotent: cached rewrites complete instantly on repeat runs.
    for page in ('/admin-imgs-a.html', '/admin-imgs-b.html'):
        for _ in range(60):
            _, _, body = cls.fetch(page)
            if b'.pagespeed.' in body:
                break
            time.sleep(.5)
        else:
            test.fail('rewrite never completed for %s: %r'
                      % (page, body[-300:]))

PAGES = ['dashboard', 'history', 'images', 'html_cache', 'javascript',
         'css', 'statistics', 'config', 'histograms', 'cache', 'console',
         'message_history', 'graphs']


class AdminConsoleNginxTest(base.HtmlCacheNginxTest):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.stop()
        config_path = cls.root / 'nginx.conf'
        config = config_path.read_text().replace(
            'types { text/html html; text/css css; }',
            'types { text/html html; text/css css; image/jpeg jpg jpeg;'
            ' image/png png; image/gif gif; image/webp webp;'
            ' image/avif avif; application/javascript js; }', 1)
        config = config.replace(
            '  pagespeed FileCachePath',
            '  pagespeed LogDir "%s/logs/pslog";\n  pagespeed FileCachePath'
            % cls.root, 1)
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

    def test_opt_js_loads_without_errors(self):
        chrome = open_chrome(self)
        chrome.cdp.call('Runtime.enable')
        chrome.cdp.call('Log.enable')
        for page in PAGES:
            chrome.cdp.events.clear()
            chrome.cdp.call('Page.navigate', {'url': 'http://127.0.0.1:%d/pagespeed_admin/%s' % (self.port, page)})
            chrome.cdp.wait_for('Page.loadEventFired')
            time.sleep(2)
            # Pump the socket: passive windows never deliver events.
            chrome.cdp.evaluate('1+1')
            problems = []
            for event in chrome.cdp.events:
                method = event.get('method')
                params = event.get('params', {})
                if method == 'Runtime.exceptionThrown':
                    problems.append('EXC ' + str(
                        params['exceptionDetails'].get('text', ''))[:150])
                elif method == 'Runtime.consoleAPICalled' and params.get('type') == 'error':
                    problems.append('CONSOLE ' + str(
                        [a.get('value', '') for a in params['args']])[:150])
                elif method == 'Log.entryAdded':
                    entry = params.get('entry', {})
                    if entry.get('level') == 'error' and 'favicon' not in entry.get('url', ''):
                        problems.append('LOG ' + entry.get('text', '')[:150])
            self.assertEqual([], problems, page)


    def sub_tabs(self, chrome, page, nav_label):
        admin_goto(chrome, self.port, page)
        nav = chrome.cdp.evaluate(
            '(function(){var n=document.getElementById("nav-bar");'
            'return n?n.outerHTML.slice(0,400):"none";})()')
        self.assertNotEqual('none', nav, page)
        tabs = chrome.cdp.evaluate(
            '[...document.querySelectorAll("#nav-bar [role=tab]")].map('
            'function(t){return t.id+":"+t.getAttribute("aria-selected")+":"'
            '+t.getAttribute("tabindex");})')
        self.assertGreater(len(tabs), 1, nav)
        self.assertEqual(1, sum(1 for t in tabs if t.endswith(':true:0')))
        # ArrowRight moves focus and selection to the next tab.
        chrome.cdp.evaluate(
            'document.querySelector("#nav-bar [role=tab][aria-selected=true]").focus()')
        focused_before = active_label(chrome)
        press_key(chrome, 'ArrowRight')
        focused_after = active_label(chrome)
        self.assertNotEqual(focused_before, focused_after)
        selected = chrome.cdp.evaluate(
            '[...document.querySelectorAll("#nav-bar [role=tab][aria-selected=true]")]'
            '.map(function(t){return t.id;})')
        self.assertEqual(1, len(selected))
        # The selected panel is the visible one.
        panel = chrome.cdp.evaluate(
            'document.querySelector("#nav-bar [role=tab][aria-selected=true]")'
            '.getAttribute("aria-controls")')
        self.assertTrue(panel)
        visible = chrome.cdp.evaluate(
            '(function(){var el=document.getElementById("%s");'
            'return el&&!el.classList.contains("pagespeed-hidden-offscreen");})()' % panel)
        self.assertTrue(visible, panel)

    def test_cache_tabs_semantics_and_arrows(self):
        self.sub_tabs(open_chrome(self), 'cache', 'Cache views')

    def test_graphs_tabs_semantics_and_arrows(self):
        self.sub_tabs(open_chrome(self), 'graphs', 'Graph views')

    def test_statistics_tabs_arrows_and_panels(self):
        chrome = open_chrome(self)
        admin_goto(chrome, self.port, 'statistics')
        chrome.cdp.evaluate(
            'document.querySelector("#pagespeed-stat-category-tabs [data-category=all]").focus()')
        press_key(chrome, 'ArrowRight')
        focused = active_label(chrome)
        self.assertIn('BUTTON', focused)
        selected = chrome.cdp.evaluate(
            '[...document.querySelectorAll("#pagespeed-stat-category-tabs [data-category][aria-selected=true]")]'
            '.map(function(b){return b.getAttribute("data-category");})')
        self.assertEqual(['cache'], selected)
        panel = chrome.cdp.evaluate(
            'document.querySelector("#pagespeed-stat-category-tabs [data-category=cache]")'
            '.getAttribute("aria-controls")')
        self.assertTrue(panel)
        role = chrome.cdp.evaluate(
            '(function(){var el=document.getElementById("%s");'
            'return el?el.getAttribute("role"):"none";})()' % panel)
        self.assertEqual('tabpanel', role)

    def test_charts_have_text_equivalents(self):
        chrome = open_chrome(self)
        admin_goto(chrome, self.port, 'graphs')
        time.sleep(3)
        # Bar charts expose their values as navigable text, not a flat image.
        flat = chrome.cdp.evaluate(
            'document.querySelectorAll("svg.pagespeed-graphs-bars[role=img]").length')
        self.assertEqual(0, flat)
        names = chrome.cdp.evaluate(
            'document.querySelectorAll("svg.pagespeed-graphs-bars text").length')
        self.assertGreater(names, 0)
        # Line charts carry a data table with the plotted values.
        tables = chrome.cdp.evaluate(
            'document.querySelectorAll(".pagespeed-graphs-workspace table").length')
        self.assertGreater(tables, 0)
        cells = chrome.cdp.evaluate(
            'document.querySelector(".pagespeed-graphs-workspace table")'
            '.textContent.length')
        self.assertGreater(cells, 20)

    def test_severity_filter_announces_selection(self):
        chrome = open_chrome(self)
        admin_goto(chrome, self.port, 'message_history')
        chrome.cdp.evaluate(
            'document.querySelector(".pagespeed-message-levels [data-level=error]").click()')
        time.sleep(.5)
        pressed = chrome.cdp.evaluate(
            '[...document.querySelectorAll(".pagespeed-message-levels button")]'
            '.map(function(b){return b.getAttribute("data-level")+":"+'
            'b.getAttribute("aria-pressed");})')
        self.assertIn('error:true', pressed)
        self.assertIn('all:false', pressed)


    def test_message_date_contrast(self):
        chrome = open_chrome(self)
        admin_goto(chrome, self.port, 'message_history')
        ratio = chrome.cdp.evaluate(
            '(function(){'
            'function lum(rgb){var m=rgb.match(/[\\d.]+/g).map(Number);'
            'var c=m.slice(0,3).map(function(v){v/=255;'
            'return v<=0.03928?v/12.92:Math.pow((v+0.055)/1.055,2.4);});'
            'return 0.2126*c[0]+0.7152*c[1]+0.0722*c[2];}'
            'var el=document.querySelector(".pagespeed-message-date,.pagespeed-message-date-range");'
            'if(!el)return -1;'
            'var fg=getComputedStyle(el).color,bg=getComputedStyle(el).backgroundColor;'
            'if(bg==="rgba(0, 0, 0, 0)")bg="rgb(255,255,255)";'
            'var l1=lum(fg),l2=lum(bg);'
            'return (Math.max(l1,l2)+0.05)/(Math.min(l1,l2)+0.05);})()')
        self.assertGreaterEqual(ratio, 4.5, ratio)

    def test_narrow_search_input_height(self):
        chrome = open_chrome(self)
        chrome.cdp.call('Emulation.setDeviceMetricsOverride', {
            'width': 360, 'height': 640, 'deviceScaleFactor': 2,
            'mobile': True})
        admin_goto(chrome, self.port, 'message_history')
        height = chrome.cdp.evaluate(
            'document.getElementById("text-filter").getBoundingClientRect().height')
        self.assertLess(height, 60, height)

    def test_autorefresh_pauses_when_hidden(self):
        chrome = open_chrome(self)
        cdp = chrome.cdp
        cdp.call('Page.enable')
        admin_goto(chrome, self.port, 'graphs')
        time.sleep(4)
        # Observe sends page-side: passive network-event windows never
        # drain the CDP socket, so count XHRs from inside the page.
        cdp.evaluate(
            'window.__polls=0;'
            '(function(){var s=XMLHttpRequest.prototype.send;'
            'XMLHttpRequest.prototype.send=function(){'
            'window.__polls++;return s.apply(this,arguments);};})()')
        cdp.evaluate('document.getElementById("auto-refresh").click()')
        time.sleep(12)
        visible_polls = cdp.evaluate('window.__polls')
        self.assertGreater(visible_polls, 0)
        # Open a second tab so this one hides; polling must stop.
        connection = http.client.HTTPConnection('127.0.0.1', chrome.debug_port, timeout=10)
        connection.request('PUT', '/json/new?about:blank')
        response = connection.getresponse()
        target = json.loads(response.read().decode())
        connection.close()
        ws_path = target['webSocketDebuggerUrl'].split(
            'ws://127.0.0.1:%d' % chrome.debug_port, 1)[1]
        other = cdp_chrome.CdpConnection('127.0.0.1', chrome.debug_port, ws_path)
        try:
            other.call('Page.enable')
            other.call('Page.navigate', {'url': 'about:blank'})
            other.wait_for('Page.loadEventFired')
            self.assertEqual('hidden', cdp.evaluate('document.visibilityState'))
            cdp.evaluate('window.__polls=0')
            time.sleep(12)
            self.assertEqual(0, cdp.evaluate('window.__polls'))
            # Foregrounding refreshes deliberately.
            cdp.call('Page.bringToFront')
            time.sleep(7)
            self.assertGreater(cdp.evaluate('window.__polls'), 0)
        finally:
            other.close()

    def test_noscript_chart_note(self):
        chrome = open_chrome(self)
        chrome.cdp.call('Emulation.setScriptExecutionDisabled', {'value': True})
        try:
            chrome.cdp.call('Page.navigate', {'url': 'http://127.0.0.1:%d/pagespeed_admin/graphs' % self.port})
            chrome.cdp.wait_for('Page.loadEventFired')
            time.sleep(1)
            text = chrome.cdp.evaluate('document.body.textContent')
            self.assertIn('Charts require JavaScript', text)
        finally:
            chrome.cdp.call('Emulation.setScriptExecutionDisabled', {'value': False})

class ImagesAdminNginxTest(base.HtmlCacheNginxTest):
    # Own nginx: the images history reads the bounded message buffer, so
    # these tests need a fresh buffer plus fresh rewrite caches. Warms
    # run inside each test (idempotent) for selective runs too.
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.stop()
        www = cls.root / 'www'
        testdata = (Path(__file__).resolve().parents[2] / 'net' / 'instaweb' /
                    'rewriter' / 'testdata')
        shutil.copyfile(testdata / 'Puzzle.jpg', www / 'c-hero1.jpg')
        shutil.copyfile(testdata / 'ExplosiveEgress.png', www / 'c-hero2.png')
        (www / 'admin-imgs-a.html').write_text(
            '<!DOCTYPE html><html><head><title>cia</title></head><body>'
            '<img src="/c-hero1.jpg"></body></html>')
        (www / 'admin-imgs-b.html').write_text(
            '<!DOCTYPE html><html><head><title>cib</title></head><body>'
            '<img src="/c-hero2.png"></body></html>')
        cls.stop()
        config_path = cls.root / 'nginx.conf'
        config = config_path.read_text().replace(
            'types { text/html html; text/css css; }',
            'types { text/html html; text/css css; image/jpeg jpg jpeg;'
            ' image/png png; image/gif gif; image/webp webp;'
            ' image/avif avif; application/javascript js; }', 1)
        # The images history reads the bounded message buffer; size it so
        # this fixture's own logging cannot rotate the completion lines.
        config = config.replace(
            '  pagespeed MessageBufferSize 100000;',
            '  pagespeed MessageBufferSize 1000000;', 1)
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

    def test_modal_contains_tab_and_restores_focus(self):
        warm_admin_images(self, type(self))
        chrome = open_chrome(self)
        admin_goto(chrome, self.port, 'images')
        count = chrome.cdp.evaluate(
            'document.querySelectorAll(".pagespeed-image-preview-button").length')
        self.assertGreater(count, 0)
        chrome.cdp.evaluate(
            'document.querySelector(".pagespeed-image-preview-button").focus()')
        before = active_label(chrome)
        chrome.cdp.evaluate(
            'document.querySelector(".pagespeed-image-preview-button").click()')
        time.sleep(.5)
        self.assertTrue(chrome.cdp.evaluate(
            'document.getElementById("pagespeed-image-modal")'
            '.classList.contains("is-open")'))
        # Focus moves into the modal on open.
        opened = active_label(chrome)
        self.assertIn('modal', opened)
        # Tab cycles inside the modal instead of escaping to the page.
        trail = []
        for _ in range(8):
            press_key(chrome, 'Tab')
            trail.append(active_label(chrome))
        for label in trail:
            self.assertIn('modal', label.lower(), trail)
        # Shift+Tab wraps from the first control to the last.
        chrome.cdp.evaluate(
            'document.querySelector(".pagespeed-image-modal-close").focus()')
        press_key(chrome, 'Tab', shift=True)
        wrapped = active_label(chrome)
        self.assertIn('modal', wrapped.lower(), wrapped)
        # Escape closes and restores focus to the invoking control.
        press_key(chrome, 'Escape')
        time.sleep(.3)
        self.assertFalse(chrome.cdp.evaluate(
            'document.getElementById("pagespeed-image-modal")'
            '.classList.contains("is-open")'))
        self.assertEqual(before, active_label(chrome))
    def test_images_sort_reorders_rows(self):
        warm_admin_images(self, type(self))
        chrome = open_chrome(self)

        def row_count():
            return chrome.cdp.evaluate(
                'document.querySelectorAll("#pagespeed-images-rows tr").length')

        # Completions land asynchronously; reload (cheap admin fetch)
        # until at least two rows exist. Bounded: the buffer only grows
        # here since nothing else logs image completions.
        admin_goto(chrome, self.port, 'images')
        for _ in range(30):
            if row_count() >= 2:
                break
            time.sleep(1)
            chrome.cdp.call('Page.reload', {})
            chrome.cdp.wait_for('Page.loadEventFired')
            time.sleep(.5)
        self.assertGreaterEqual(row_count(), 2)

        def visible_keys():
            return chrome.cdp.evaluate(
                '(function(){return [...document.querySelectorAll('
                '"#pagespeed-images-rows tr")].filter(function(r){'
                'return r.style.display!="none";}).map(function(r){'
                'return [Number(r.getAttribute("data-timestamp-ms")),'
                'Number(r.getAttribute("data-saved-bytes"))];});})()')

        def is_desc(values):
            return all(a >= b for a, b in zip(values, values[1:]))

        chrome.cdp.evaluate(
            '(function(){var s=document.getElementById("pagespeed-images-sort");'
            's.value="saved";s.dispatchEvent(new Event("change",{bubbles:true}));})()')
        time.sleep(.5)
        rows = visible_keys()
        self.assertGreater(len(rows), 1)
        self.assertTrue(is_desc([r[1] for r in rows]), rows)
        chrome.cdp.evaluate(
            '(function(){var s=document.getElementById("pagespeed-images-sort");'
            's.value="newest";s.dispatchEvent(new Event("change",{bubbles:true}));})()')
        time.sleep(.5)
        rows = visible_keys()
        self.assertGreater(len(rows), 1)
        self.assertTrue(is_desc([r[0] for r in rows]), rows)


class AdminNoLoggingNginxTest(base.HtmlCacheNginxTest):
    # Separate nginx without statistics logging: data fetches fail and
    # pages must show error states instead of eternal loading.
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.stop()
        www = cls.root / 'www'
        (www / 'plain.html').write_text(
            '<!DOCTYPE html><html><head><title>p</title></head><body>'
            '<p>hi</p></body></html>')
        cls.stop()
        # Strip the module-level Statistics lines: this class must run
        # without statistics logging.
        config_path = cls.root / 'nginx.conf'
        config = config_path.read_text()
        config = config.replace('  pagespeed Statistics on;\n', '', 1)
        config = config.replace('  pagespeed StatisticsLogging on;\n', '', 1)
        config_path.write_text(config)
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

    def test_graphs_failure_shows_error_state(self):
        if not getattr(base.OPTIONS, 'browser', None):
            raise unittest.SkipTest('Pass --browser /path/to/chrome')
        chrome = cdp_chrome.CdpChrome(base.OPTIONS.browser)
        try:
            chrome.cdp.call('Page.navigate', {'url': 'http://127.0.0.1:%d/pagespeed_admin/graphs' % self.port})
            chrome.cdp.wait_for('Page.loadEventFired')
            time.sleep(2)
            # No chart divs at all: a setup section replaces the page.
            count = chrome.cdp.evaluate(
                'document.querySelectorAll("#cache_applied").length')
            self.assertEqual(0, count)
            text = chrome.cdp.evaluate('document.body.textContent')
            self.assertIn('Graphs need a little setup', text)
            self.assertIn('StatisticsLogging', text)
        finally:
            chrome.stop()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nginx', required=True)
    parser.add_argument('--module', required=True)
    parser.add_argument('--browser',
                        help='Chrome/Chromium binary (browser tests only)')
    options, remaining = parser.parse_known_args()
    options.extra_config = ('\n  pagespeed Statistics on;\n'
                            '  pagespeed StatisticsLogging on;\n')
    base.OPTIONS = options
    unittest.main(argv=[__file__, *remaining])

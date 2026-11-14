#!/usr/bin/env python3
"""WG-06: trace owned (pagespeed-generated) runtime JS under interactions.

Serves a fixture page through isolated Nginx with the owned-runtime filters
on, drives real Chrome over CDP, samples JS execution with the CDP Profiler
while scrolling and clicking, and attributes self time to functions owned
by PageSpeed. Long tasks and Event Timing entries are reported as honestly
labeled context: raw event durations are NOT INP, and unsupported
observations are reported as unavailable rather than zero.

Usage:
  python3 runtime_trace.py --nginx .../objs/nginx \
      --module .../objs/ngx_pagespeed.so --browser /opt/google/chrome/chrome \
      [--runs 5]
"""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location(
    'html_cache_tests', HERE / 'html_cache_nginx_test.py')
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
cdp_spec = importlib.util.spec_from_file_location(
    'cdp_chrome', HERE / 'cdp_chrome.py')
cdp_chrome = importlib.util.module_from_spec(cdp_spec)
cdp_spec.loader.exec_module(cdp_chrome)

FILTERS = ('lazyload_images,defer_javascript,rewrite_javascript,'
           'delay_images,responsive_images,prioritize_lcp_images,'
           'insert_image_dimensions')

LONGTASK_PROBE = (
    '(function(){try{var o=new PerformanceObserver(function(){});'
    'o.observe({type:"longtask",buffered:true});var e=o.takeRecords();'
    'o.disconnect();return e.map(function(x){return x.duration;});}'
    'catch(err){return "UNSUPPORTED:"+err;}})()')

# The 'event' entry type yields no entries for CDP-synthesized input, so
# first-input delay is the honestly labeled interaction context instead.
EVENT_PROBE = (
    '(function(){try{var o=new PerformanceObserver(function(){});'
    'o.observe({type:"first-input",buffered:true});'
    'var e=o.takeRecords();'
    'o.disconnect();return e.map(function(x){return x.duration;});}'
    'catch(err){return "UNSUPPORTED:"+err;}})()')

# Basename fragments of PageSpeed-owned runtime scripts. Attribution matches
# these (plus function names containing 'pagespeed' for inline frames) so
# rewritten-URL application scripts are not credited to owned runtime.
OWNED_SCRIPTS = ('js_defer', 'lazyload', 'delay_images', 'responsive',
                 'critical_images_beacon', 'critical_css_loader',
                 'critical_css_beacon', 'add_instrumentation',
                 'client_domain_rewriter', 'dedup_inlined', 'defer_iframe',
                 'deterministic', 'extended_instrumentation',
                 'local_storage_cache')


class RuntimeTrace(base.HtmlCacheNginxTest):
    def runTest(self):
        pass

    @classmethod
    def setup_fixture(cls):
        cls.stop()
        www = cls.root / 'www'
        testdata = (HERE.parents[1] / 'net' / 'instaweb' / 'rewriter' /
                    'testdata')
        shutil.copyfile(testdata / 'BikeCrashIcn.png', www / 't-logo.png')
        shutil.copyfile(testdata / 'Puzzle.jpg', www / 't-hero.jpg')
        (www / 't-one.js').write_text('window.traceOne = 1;\n')
        (www / 't-two.js').write_text('window.traceTwo = 2;\n')
        imgs = ['<img src="/t-logo.png" width="120" height="40">']
        imgs += ['<img src="/t-hero.jpg" width="480" height="320">']
        imgs += ['<div style="height:1200px"></div>']
        for index in range(6):
            imgs.append('<img src="/t-hero.jpg?%d" width="480" height="320">'
                        % index)
        (www / 'trace.html').write_text(
            '<!DOCTYPE html><html><head><title>trace</title>'
            '<script src="/t-one.js"></script>'
            '<script src="/t-two.js"></script></head><body>'
            '<button id="tap">tap</button>'
            '<script>document.getElementById("tap").addEventListener('
            '"click",function(){window.tapCount='
            '(window.tapCount||0)+1;});</script>'
            '%s</body></html>' % ''.join(imgs))
        config_path = cls.root / 'nginx.conf'
        config = config_path.read_text().replace(
            '    location = /off.html {',
            '    location = /trace.html {\n'
            '      pagespeed RewriteLevel PassThrough;\n'
            '      pagespeed EnableFilters %s;\n'
            '      pagespeed CriticalImagesBeaconEnabled on;\n'
            '    }\n'
            '    location = /off.html {' % FILTERS, 1)
        config_path.write_text(config)
        cls.process = subprocess.Popen(
            [base.OPTIONS.nginx, '-p', str(cls.root) + '/', '-c',
             str(config_path), '-e', str(cls.root / 'logs/error.log')],
            stdout=cls.log, stderr=cls.log)
        for _ in range(100):
            if cls.process.poll() is not None:
                raise RuntimeError(
                    (cls.root / 'logs/error.log').read_text())
            try:
                cls.fetch('/style.css')
                return
            except OSError:
                time.sleep(.05)
        raise RuntimeError('Isolated Nginx did not start')

    @classmethod
    def owned_self_ms(cls, profile):
        """Aggregate profiler self time for owned (PageSpeed) frames."""
        nodes = {node['id']: node for node in profile['nodes']}
        owned = {}
        by_script = {}
        wall_usec = 0
        for sample_id, count in zip(profile.get('samples', []),
                                    profile.get('timeDeltas', [])):
            wall_usec += count
            node = nodes.get(sample_id, {})
            frame = node.get('callFrame', {})
            name = frame.get('functionName', '')
            url = frame.get('url', '')
            script = url.rsplit('/', 1)[-1].split('?', 1)[0] or url
            if ('pagespeed' in name.lower() or
                    any(token in script for token in OWNED_SCRIPTS)):
                key = name or '(anonymous)'
                owned[key] = owned.get(key, 0) + count
                by_script[script] = by_script.get(script, 0) + count
        scale = 1000.0  # timeDeltas are microseconds.
        return ({name: usec / scale for name, usec in owned.items()},
                {name: usec / scale for name, usec in by_script.items()},
                wall_usec / scale)

    @classmethod
    def trace_one_run(cls, chrome, run):
        chrome.cdp.call('Profiler.enable')
        chrome.cdp.call('Profiler.setSamplingInterval',
                        {'interval': 100})
        chrome.cdp.call('Profiler.start')
        chrome.load('http://127.0.0.1:%d/trace.html?run=%d'
                    % (cls.port, run))
        for _ in range(150):
            if chrome.cdp.evaluate('document.readyState') == 'complete':
                break
            time.sleep(.1)
        else:
            raise RuntimeError('page never reached readyState complete')
        # Click first while the button is still in the initial viewport,
        # then scroll (which moves it out of view). The button's layout
        # position is measured, never assumed.
        rect = chrome.cdp.evaluate(
            'JSON.stringify(document.getElementById("tap").'
            'getBoundingClientRect())')
        if rect is None:
            raise RuntimeError('fixture button missing from DOM')
        rect = json.loads(rect)
        x, y = rect['x'] + rect['width'] / 2, rect['y'] + rect['height'] / 2
        chrome.cdp.call('Input.dispatchMouseEvent', {
            'type': 'mousePressed', 'x': x, 'y': y,
            'button': 'left', 'clickCount': 1})
        chrome.cdp.call('Input.dispatchMouseEvent', {
            'type': 'mouseReleased', 'x': x, 'y': y,
            'button': 'left', 'clickCount': 1})
        for _ in range(3):
            chrome.cdp.call('Input.synthesizeScrollGesture', {
                'x': 400, 'y': 300, 'yDistance': -800,
                'speed': 1200, 'repeatCount': 1,
                'repeatDelayMs': 200})
            time.sleep(.4)
        time.sleep(1.0)
        profile = chrome.cdp.call('Profiler.stop')
        taps = chrome.cdp.evaluate('window.tapCount || 0')
        if taps != 1:
            raise RuntimeError('click missed the fixture button: %r' % taps)
        longtasks = chrome.cdp.evaluate(LONGTASK_PROBE)
        events = chrome.cdp.evaluate(EVENT_PROBE)
        chrome.cdp.call('Page.navigate', {'url': 'about:blank'})
        chrome.cdp.wait_for('Page.loadEventFired')
        owned, by_script, wall = cls.owned_self_ms(profile['profile'])
        return {'owned': owned, 'by_script': by_script, 'wall': wall,
                'longtasks': longtasks, 'events': events}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nginx', required=True)
    parser.add_argument('--module', required=True)
    parser.add_argument('--browser', required=True)
    parser.add_argument('--runs', type=int, default=5)
    options, _ = parser.parse_known_args()
    options.extra_config = ''
    base.OPTIONS = options
    RuntimeTrace.setUpClass()
    try:
        RuntimeTrace.setup_fixture()
        chrome = cdp_chrome.CdpChrome(options.browser)
        try:
            runs = [RuntimeTrace.trace_one_run(chrome, run)
                    for run in range(options.runs)]
        finally:
            chrome.stop()
    finally:
        RuntimeTrace.stop()
    names = sorted({name for run in runs for name in run['owned']})
    print('WG-06 runtime trace: %d runs, filters=%s' % (
        options.runs, FILTERS))
    print('browser=%s' % options.browser)
    print('%-50s %10s %10s' % ('owned function (self time)', 'mean_ms',
                               'max_ms'))
    for name in names:
        samples = [run['owned'].get(name, 0.0) for run in runs]
        print('%-50s %10.3f %10.3f' % (
            name[:50], sum(samples) / len(samples), max(samples)))
    scripts = sorted({name for run in runs for name in run['by_script']})
    print('%-50s %10s %10s' % ('owned script (self time)', 'mean_ms',
                               'max_ms'))
    for name in scripts:
        samples = [run['by_script'].get(name, 0.0) for run in runs]
        print('%-50s %10.3f %10.3f' % (
            name[:50], sum(samples) / len(samples), max(samples)))
    owned_totals = [sum(run['owned'].values()) for run in runs]
    walls = [run['wall'] for run in runs]
    print('owned self total: mean=%.3fms max=%.3fms' % (
        sum(owned_totals) / len(owned_totals), max(owned_totals)))
    print('profile wall: mean=%.1fms (owned share=%.3f%%)' % (
        sum(walls) / len(walls),
        100.0 * sum(owned_totals) / sum(walls)))
    for label, key in (('longtask durations (ms)', 'longtasks'),
                       ('first-input delay, NOT INP (ms)', 'events')):
        values = runs[0][key]
        if isinstance(values, str):
            print('%s: %s' % (label, values))
            continue
        merged = [entry for run in runs for entry in run[key]]
        print('%s: count=%d max=%s' % (
            label, len(merged),
            ('%.3f' % max(merged)) if merged else 'none observed'))


if __name__ == '__main__':
    main()

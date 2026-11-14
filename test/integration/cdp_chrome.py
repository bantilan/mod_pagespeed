#!/usr/bin/env python3
"""Minimal stdlib-only driver for headless Chrome over CDP (WG-06).

Used by integration tests that need real browser timing the --dump-dom
one-shot cannot provide: staying alive past window load so async beacon
XHRs flush, navigating away to trigger pagehide handlers, and reading
back JS state. No third-party websocket package required.
"""
import base64
import http.client
import json
import socket
import struct
import subprocess
import tempfile
import time


class CdpError(RuntimeError):
    pass


class CdpConnection:
    """Blocking JSON-over-websocket client for one CDP target."""

    def __init__(self, host, port, ws_path):
        self.sock = socket.create_connection((host, port), timeout=30)
        key = base64.b64encode(b'pagespeed-cdp-driver').decode()
        self.sock.sendall((
            'GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\n'
            'Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n'
            'Sec-WebSocket-Version: 13\r\n\r\n'
            % (ws_path, host, port, key)).encode())
        response = b''
        while b'\r\n\r\n' not in response:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise CdpError('CDP handshake failed')
            response += chunk
        if b'101 ' not in response.split(b'\r\n', 1)[0]:
            raise CdpError('CDP handshake rejected: %r' % response[:100])
        self.sock.settimeout(30)
        self.next_id = 1
        self.buf = b''
        self.events = []

    def _recv_frame(self):
        while len(self.buf) < 2:
            self.buf += self.sock.recv(4096)
        opcode = self.buf[0] & 0x0F
        length = self.buf[1] & 0x7F
        offset = 2
        if length == 126:
            while len(self.buf) < 4:
                self.buf += self.sock.recv(4096)
            length = struct.unpack('>H', self.buf[2:4])[0]
            offset = 4
        elif length == 127:
            while len(self.buf) < 10:
                self.buf += self.sock.recv(4096)
            length = struct.unpack('>Q', self.buf[2:10])[0]
            offset = 10
        while len(self.buf) < offset + length:
            self.buf += self.sock.recv(65536)
        payload = self.buf[offset:offset + length]
        self.buf = self.buf[offset + length:]
        return opcode, payload

    def _send_text(self, text):
        data = text.encode()
        header = bytes([0x81])
        if len(data) < 126:
            header += bytes([0x80 | len(data)])
        elif len(data) < 65536:
            header += bytes([0x80 | 126]) + struct.pack('>H', len(data))
        else:
            header += bytes([0x80 | 127]) + struct.pack('>Q', len(data))
        mask = b'\x01\x02\x03\x04'
        header += mask
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
        self.sock.sendall(header + masked)

    def _pong(self):
        self.sock.sendall(b'\x8a\x80\x01\x02\x03\x04')

    def _next_message(self):
        opcode, payload = self._recv_frame()
        if opcode == 0x9:  # ping
            self._pong()
            return self._next_message()
        if opcode == 0x8:  # close
            raise CdpError('CDP connection closed')
        return json.loads(payload.decode())

    def call(self, method, params=None):
        call_id = self.next_id
        self.next_id += 1
        self._send_text(json.dumps(
            {'id': call_id, 'method': method, 'params': params or {}}))
        while True:
            message = self._next_message()
            if message.get('id') == call_id:
                if 'error' in message:
                    raise CdpError(message['error'])
                return message.get('result', {})
            self.events.append(message)

    def wait_for(self, method, timeout=30):
        deadline = time.time() + timeout
        while time.time() < deadline:
            for index, event in enumerate(self.events):
                if event.get('method') == method:
                    del self.events[index]
                    return event.get('params', {})
            self.sock.settimeout(max(0.1, deadline - time.time()))
            try:
                message = self._next_message()
            except socket.timeout:
                continue
            if message.get('method') == method:
                return message.get('params', {})
            if 'method' in message:
                self.events.append(message)
        raise CdpError('timeout waiting for ' + method)

    def evaluate(self, expression):
        result = self.call(
            'Runtime.evaluate',
            {'expression': expression, 'returnByValue': True})
        return result['result'].get('value')

    def close(self):
        try:
            self.sock.sendall(b'\x88\x80\x01\x02\x03\x04')
        except OSError:
            pass
        self.sock.close()


class CdpChrome:
    """Launches headless Chrome with remote debugging and owns its target."""

    def __init__(self, browser):
        self.browser = browser
        self.profile = tempfile.mkdtemp(prefix='pagespeed-cdp-')
        self.process = subprocess.Popen(
            [browser, '--headless', '--disable-gpu', '--no-first-run',
             '--no-default-browser-check', '--disable-background-networking',
             '--remote-debugging-port=0', '--remote-allow-origins=*',
             '--user-data-dir=' + self.profile, 'about:blank'],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.debug_port = None
        deadline = time.time() + 30
        while time.time() < deadline and self.debug_port is None:
            line = self.process.stdout.readline()
            if 'DevTools listening on ws://' in line:
                self.debug_port = int(line.rsplit(':', 1)[1].split('/')[0])
        if self.debug_port is None:
            self.stop()
            raise CdpError('Chrome printed no DevTools port')
        connection = http.client.HTTPConnection(
            '127.0.0.1', self.debug_port, timeout=10)
        connection.request('PUT', '/json/new?about:blank')
        response = connection.getresponse()
        body = response.read().decode()
        connection.close()
        if response.status != 200 or not body.strip():
            self.stop()
            raise CdpError('cannot create CDP target: %d %r' % (
                response.status, body[:200]))
        target = json.loads(body)
        ws_path = target['webSocketDebuggerUrl'].split(
            'ws://127.0.0.1:%d' % self.debug_port, 1)[1]
        self.cdp = CdpConnection('127.0.0.1', self.debug_port, ws_path)
        self.cdp.call('Page.bringToFront')
        self.cdp.call('Page.enable')

    def load(self, url):
        self.cdp.call('Page.navigate', {'url': url})
        self.cdp.wait_for('Page.loadEventFired')

    def stop(self):
        try:
            self.cdp.close()
        except AttributeError:
            pass
        try:
            self.process.stdout.close()
        except (AttributeError, OSError):
            pass
        self.process.terminate()
        self.process.wait(timeout=15)

"""Ask the running Strata server one question about a picture: the shortest proof that images work.

    python tools/vision_check.py [--port 9797] [--image path.png]

Sends an OpenAI /v1/chat/completions request with an image_url data part and prints the answer with the wall time.
With no --image, a small gradient PNG is generated in the temp directory.
"""

import argparse
import base64
import json
import os
import struct
import time
import urllib.request
import zlib


def gradient_png(w: int = 320, h: int = 240) -> bytes:
    """A plain PNG without any image library: a colour gradient, enough for 'what is in this picture?'."""
    def row(y: int) -> bytes:
        return b'\x00' + b''.join(bytes([(x * 3) % 256, (y * 5) % 256, (x + y) % 256]) for x in range(w))

    raw = b''.join(row(y) for y in range(h))

    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF)

    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw))
            + chunk(b'IEND', b''))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=9797)
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--model', default=None)
    ap.add_argument('--image', default=None)
    ap.add_argument('--question', default='What do you see in this picture? Answer in one short sentence.')
    a = ap.parse_args()

    if a.image:
        with open(a.image, 'rb') as f:
            data = f.read()
        name = os.path.basename(a.image)
    else:
        data = gradient_png()
        name = 'gradient.png'

    body = {
        'messages': [{
            'role': 'user',
            'content': [
                {'type': 'text', 'text': a.question},
                {'type': 'image_url',
                 'image_url': {'url': 'data:image/png;base64,' + base64.b64encode(data).decode('ascii')}},
            ],
        }],
        'max_tokens': 200,
        'temperature': 0.2,
    }
    if a.model:
        body['model'] = a.model

    req = urllib.request.Request(
        f'http://{a.host}:{a.port}/v1/chat/completions',
        data=json.dumps(body).encode('utf-8'),
        headers={'Content-Type': 'application/json', 'Authorization': 'Bearer x'},
        method='POST')

    t0 = time.time()
    with urllib.request.urlopen(req, timeout=900) as r:
        out = json.load(r)
    secs = time.time() - t0

    text = (out.get('choices') or [{}])[0].get('message', {}).get('content') or ''
    usage = out.get('usage') or {}
    print(f'{name}: {len(data)} bytes, {secs:.1f} s total')
    print('prompt tokens:', usage.get('prompt_tokens'), ' completion tokens:', usage.get('completion_tokens'))
    print('answer:', (text or '').strip()[:400])
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

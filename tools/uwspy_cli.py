"""Small UWPSpy IPC client. Python 3, Windows, standard library only."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import os
import struct
import time
import uuid
from pathlib import Path

K = C.WinDLL('kernel32', use_last_error=True)
K.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p, W.DWORD, W.DWORD, W.HANDLE]
K.CreateFileW.restype = W.HANDLE
K.ReadFile.argtypes = [W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.c_void_p]
K.WriteFile.argtypes = K.ReadFile.argtypes
K.CloseHandle.argtypes = [W.HANDLE]
K.WaitNamedPipeW.argtypes = [W.LPCWSTR, W.DWORD]


def endpoints():
    return sorted('\\\\.\\pipe\\' + n for n in os.listdir('\\\\.\\pipe\\') if n.startswith('UWPSpy-'))


def parse_dump(dump):
    """Retain property sources separately: duplicate default values are NOT effective values."""
    nodes = []
    node = None
    section = None
    for line in dump.splitlines():
        if line.startswith('Path: '):
            node = {'path': line[6:], 'local': {}, 'other': {}, 'states': []}
            nodes.append(node)
            section = None
        elif node is not None:
            if line == 'Local properties:': section = 'local'
            elif line == 'Other Properties:': section = 'other'
            elif line == 'Visual states:': section = 'states'
            elif line.startswith('- ') and section in ('local', 'other'):
                key, sep, value = line[2:].partition(': ')
                if sep: node[section].setdefault(key, []).append(value)
            elif section == 'states': node['states'].append(line)
            elif ': ' in line:
                key, value = line.split(': ', 1)
                node[key.lower().replace(' ', '_')] = value
    return nodes


class Client:
    def __init__(self, endpoint): self.endpoint = endpoint

    def call(self, op, **fields):
        request = {'op': op, 'request_id': str(uuid.uuid4()), **fields}
        data = json.dumps(request).encode('utf-8')
        if len(data) > 65536: raise ValueError('request too large')
        deadline = time.monotonic() + 20
        while True:
            handle = K.CreateFileW(self.endpoint, 0xC0000000, 0, None, 3, 0, None)
            if handle != W.HANDLE(-1).value: break
            error = C.get_last_error()
            if error != 231 or time.monotonic() >= deadline: raise C.WinError(error)
            K.WaitNamedPipeW(self.endpoint, 500)
        try:
            def transfer(data=None, size=None):
                output = bytearray()
                remaining = len(data) if data is not None else size
                offset = 0
                while remaining:
                    n = W.DWORD()
                    buffer = C.create_string_buffer(data[offset:] if data is not None else remaining)
                    fn = K.WriteFile if data is not None else K.ReadFile
                    if not fn(handle, buffer, remaining, C.byref(n), None): raise C.WinError(C.get_last_error())
                    if not n.value: raise ConnectionError('pipe closed')
                    if data is None: output.extend(buffer.raw[:n.value])
                    remaining -= n.value
                    offset += n.value
                return bytes(output)
            transfer(struct.pack('<I', len(data)) + data)
            size, = struct.unpack('<I', transfer(size=4))
            if size > 16 * 1024 * 1024: raise ValueError('response too large')
            reply = json.loads(transfer(size=size))
            transfer(b'\x01')
        finally:
            K.CloseHandle(handle)
        if 'error' in reply: raise RuntimeError(reply['error'])
        if reply.get('request_id') != request['request_id']: raise RuntimeError('request ID mismatch')
        result = reply.get('result', reply)
        if 'error' in result: raise RuntimeError(result['error'])
        if 'dump' in result: result['nodes'] = parse_dump(result['dump'])
        return result

    def find(self, **criteria):
        matches = []
        for tree in self.call('trees')['trees']:
            matches += self.call('find', tree=tree['tree'], **criteria)['matches']
        return matches


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('op', choices=['endpoints', 'trees', 'find', 'get', 'capture', 'watch', 'events', 'stop', 'label'])
    p.add_argument('--endpoint')
    p.add_argument('--json', action='store_true', help='emit endpoints as a JSON array instead of literal pipe paths')
    p.add_argument('--tree')
    p.add_argument('--handle')
    p.add_argument('--generation')
    p.add_argument('--type', default='Taskbar.TaskListButton')
    p.add_argument('--contains', default='')
    p.add_argument('--name', default='')
    p.add_argument('--label')
    p.add_argument('--output', type=Path)
    p.add_argument('--screenshots', action='store_true')
    p.add_argument('--follow', action='store_true', help='poll event history and print NDJSON')
    p.add_argument('--after', default='0')
    a = p.parse_args()
    if a.op == 'endpoints':
        found = endpoints()
        if a.json: print(json.dumps(found, indent=2))
        else:
            for endpoint in found: print(endpoint)
        return
    if not a.endpoint: p.error('--endpoint is required (run endpoints first)')
    client = Client(a.endpoint)
    fields = {k: getattr(a, k) for k in ('tree', 'handle', 'generation', 'label') if getattr(a, k) is not None}
    if a.output: fields['output'] = str(a.output.resolve())
    if a.op in ('capture', 'watch'): fields['screenshots'] = a.screenshots
    if a.op == 'find':
        fields.update(type=a.type, automation_contains=a.contains, automation_name=a.name)
        reply = client.call('find', **fields) if a.tree else {'matches': client.find(**fields)}
    elif a.op == 'events':
        cursor = a.after
        while True:
            reply = client.call('events', after=cursor)
            print(json.dumps(reply, ensure_ascii=False), flush=True)
            cursor = reply['cursor']
            if not a.follow: return
            time.sleep(.25)
    else: reply = client.call(a.op, **fields)
    print(json.dumps(reply, indent=2, ensure_ascii=False))

if __name__ == '__main__': main()

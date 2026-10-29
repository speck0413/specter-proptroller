"""A small client for `solidcraft-cli mcp` (JSON-RPC over stdio). Set SOLIDCRAFT_CLI if it isn't on PATH."""
import json, os, subprocess, itertools

CLI = os.environ.get('SOLIDCRAFT_CLI', 'solidcraft-cli')   # SolidCraft 0.15

class SolidCraftError(RuntimeError): pass

class SolidCraft:
    def __init__(self, allow=None, log=None):
        args = [CLI, 'mcp'] + (['--allow-path', allow] if allow else ['--allow-any-path'])
        self.p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1)
        self.ids = itertools.count(1); self.log = open(log, 'w') if log else None
        self._rpc('initialize', {'protocolVersion': '2024-11-05', 'capabilities': {}, 'clientInfo': {'name': 'fusion-shim', 'version': '1'}})
        self.p.stdin.write(json.dumps({'jsonrpc': '2.0', 'method': 'notifications/initialized'}) + '\n')
    def _rpc(self, method, params):
        i = next(self.ids)
        self.p.stdin.write(json.dumps({'jsonrpc': '2.0', 'id': i, 'method': method, 'params': params}) + '\n'); self.p.stdin.flush()
        while True:
            line = self.p.stdout.readline()
            if not line: raise SolidCraftError('solidcraft-cli exited')
            m = json.loads(line)
            if m.get('id') == i: break
        if 'error' in m: raise SolidCraftError(json.dumps(m['error']))
        return m['result']
    def tool(self, name, args):
        r = self._rpc('tools/call', {'name': name, 'arguments': args})
        txt = ''.join(c.get('text', '') for c in r.get('content', []) if c.get('type') == 'text')
        try: out = json.loads(txt)
        except Exception: out = txt
        if r.get('isError'): raise SolidCraftError(f'{name} {json.dumps(args)[:600]} -> {txt[:1500]}')
        return out
    def x(self, command, **params):
        if self.log: self.log.write(json.dumps({'command': command, 'params': params}) + '\n'); self.log.flush()
        return self.tool('execute', {'command': command, 'params': params})
    def close(self):
        try: self.p.stdin.close(); self.p.wait(10)
        except Exception: self.p.kill()

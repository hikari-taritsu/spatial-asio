"""Native Spatial pip adapter: bounded owned packets -> NSP1 -> native renderer.

No driver, game-process hook, automatic security changes, or PCM/property writes.
The live capture ABI is deliberately hash gated. Only live native input is exposed.
"""
import argparse
import ctypes
import hashlib
import json
import os
import pathlib
import queue
import struct
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'deps'))
HEADER = struct.Struct('<IIQQQIIII')
SUPPORTED_SHA256 = '3dd24fe6faf0b56f99d5b00f24687b5f3b3e76e15b219e23f0963114a0dce9a5'
PROFILES = {
    SUPPORTED_SHA256: {'getter':0x31930, 'vtables':[0x9f4f8,0xa2d58], 'validated':True},
    # Microsoft public PDB + offline getter/constructor disassembly on this host.
    # Runtime fixture must pass before the normal UI may use this build.
    'b97353300a4e0c68257bb5a2554afaa3733609c739368febc36a5f63ef47a44f':
        {'getter':0x3ac70, 'vtables':[0x9f4e8,0xa2cb8], 'validated':False},
}

def encode(record, data):
    count = record['count']
    frames = record['frames']
    if not isinstance(count, int) or not 1 <= count <= 160 or len(frames) != count:
        raise ValueError('Invalid object count')
    if not frames or len(set(frames)) != 1 or not 1 <= frames[0] <= 4096:
        raise ValueError('Only equal, nonempty 48 kHz object blocks are supported')
    if len(data) != count * (32 + frames[0] * 4):
        raise ValueError('Invalid packet length')
    stamp = int(record['captureQpcNs'])
    if stamp <= 0 or record['sequence'] < 0:
        raise ValueError('Invalid packet clock or sequence')
    block = int(record['block'], 0) if isinstance(record['block'], str) else record['block']
    return HEADER.pack(0x3150534e, len(data), block, record['sequence'], stamp,
                       48000, count, frames[0], 0) + data

class Owner:
    def __init__(self, pid):
        self.k = ctypes.WinDLL('kernel32', use_last_error=True)
        self.k.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
        self.k.OpenProcess.restype = ctypes.c_void_p
        self.k.OpenEventW.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_wchar_p]
        self.k.OpenEventW.restype = ctypes.c_void_p
        self.k.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        self.k.CloseHandle.argtypes = [ctypes.c_void_p]
        self.process = self.k.OpenProcess(0x100000, False, pid) if pid else None
        self.event = self.k.OpenEventW(0x100000, False, f'Local\\SpatialASIO.Stop.{pid}') if pid else None
        if pid and (not self.process or not self.event):
            self.close()
            raise RuntimeError('Cannot monitor renderer owner; refusing an orphan capture')
    def stopped(self):
        return bool((self.process and self.k.WaitForSingleObject(self.process, 0) == 0) or
                    (self.event and self.k.WaitForSingleObject(self.event, 0) == 0))
    def close(self):
        for name in ('process', 'event'):
            handle = getattr(self, name, None)
            if handle: self.k.CloseHandle(handle); setattr(self, name, None)

def preflight(pid=None):
    image = pathlib.Path(os.environ['SystemRoot']) / 'System32/audiodg.exe'
    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    profile = PROFILES.get(digest)
    fixture = ROOT / 'validated-build.json'
    runtime_validated = profile and profile['validated']
    if fixture.exists():
        evidence = json.loads(fixture.read_text(encoding='utf-8-sig'))
        runtime_validated = runtime_validated or (evidence.get('sha256') == digest and evidence.get('passed') is True)
    report = {'image': str(image), 'sha256': digest,
              'versionSupported': bool(profile), 'runtimeValidated': bool(runtime_validated),
              'administrator': bool(ctypes.windll.shell32.IsUserAnAdmin()),
              'driverRequired': False, 'gameProcessInjection': False}
    if not profile or not runtime_validated:
        raise RuntimeError('本机 audiodg.exe 尚未通过 Native Spatial 捕获版本验证。'
                           f' SHA256={digest}；不允许套用其他版本的内部地址。')
    if not report['administrator']:
        raise RuntimeError('请以管理员身份运行工作室；Native Spatial 用户态捕获需要管理员权限。')
    import frida
    if frida.__version__ != '17.23.3':
        raise RuntimeError('Native Spatial requires the packaged Frida 17.23.3 runtime')
    hosts = [p for p in frida.get_local_device().enumerate_processes() if p.name.lower() == 'audiodg.exe']
    if pid:
        hosts = [p for p in hosts if p.pid == pid]
    if len(hosts) != 1:
        raise RuntimeError('Expected exactly one audiodg host; specify --pid when multiple hosts exist')
    pid = hosts[0].pid
    k = ctypes.WinDLL('kernel32', use_last_error=True)
    k.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
    k.OpenProcess.restype = ctypes.c_void_p
    k.CloseHandle.argtypes = [ctypes.c_void_p]
    k.QueryFullProcessImageNameW.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_uint32)]
    k.GetProcessInformation.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32]
    handle = k.OpenProcess(0x1000, False, pid)
    if not handle: raise OSError(ctypes.get_last_error(), 'Cannot query audiodg')
    try:
        path = ctypes.create_unicode_buffer(32768); length = ctypes.c_uint32(len(path))
        if not k.QueryFullProcessImageNameW(handle, 0, path, ctypes.byref(length)) or pathlib.Path(path.value).resolve() != image.resolve():
            raise RuntimeError('Target is not the verified Windows audio host')
        level = ctypes.c_uint32()
        if not k.GetProcessInformation(handle, 7, ctypes.byref(level), 4) or level.value != 0xfffffffe:
            raise RuntimeError('Protected audio host is unsupported; no security settings were changed')
    finally:
        k.CloseHandle(handle)
    report['pid'] = pid
    report['profile'] = profile
    return report

def live(args, owner, output, report):
    import frida
    checked = preflight(args.pid)
    report['preflight'] = checked
    packets = queue.Queue(64)
    ready, failed = threading.Event(), threading.Event()
    report.update(received=0, sent=0, queueDropped=0, errors=[])
    session = script = None
    def send_packets():
        try:
            while True:
                packet = packets.get()
                if packet is None: break
                record, data, binary = packet
                output.write(binary); output.flush(); report['sent'] += 1
        except Exception as error:
            report['errors'].append(str(error)); failed.set()
    worker = threading.Thread(target=send_packets, daemon=True, name='native-pipe-writer')
    worker.start()
    def on_message(message, data):
        try:
            if message.get('type') != 'send': raise RuntimeError(str(message))
            payload = message['payload']
            if payload['kind'] == 'ready': report['ready'] = payload; ready.set()
            elif payload['kind'] == 'snapshot':
                packet = encode(payload['record'], data)
                try: packets.put_nowait((payload['record'],data,packet))
                except queue.Full: report['queueDropped'] += 1; raise RuntimeError('Native capture output queue overflow')
                report['received'] += 1
            elif payload['kind'] == 'stopped': report['agentStopped'] = payload; failed.set()
        except Exception as error:
            report['errors'].append(str(error)); failed.set()
    try:
        session = frida.attach(checked['pid'])
        source = 'const CAPTURE_PROFILE=' + json.dumps(checked['profile']) + ';\n'
        source += 'const STREAM_DURATION_MS=28800000;const STREAM_DRAIN_MS=1;\n' + (ROOT/'capture.js').read_text(encoding='utf-8-sig')
        script = session.create_script(source)
        script.on('message', on_message); script.load()
        if not ready.wait(5): raise RuntimeError('Native capture did not become ready')
        sys.stderr.write('Native Spatial 已就绪：请向已启用 Windows Sonic 的端点播放空间音频。\n'); sys.stderr.flush()
        health_due = time.monotonic() + 1
        while not owner.stopped() and not failed.wait(.1):
            if args.seconds and time.monotonic() - report['startMonotonic'] >= args.seconds: break
            if time.monotonic() >= health_due:
                health = script.exports_sync.health(); health_due = time.monotonic() + 1
                if health['queueDropped'] or health['budgetRejected'] or health['errors']:
                    raise RuntimeError('Capture loss or agent error: ' + json.dumps(health))
    finally:
        if script:
            try: report['capture'] = script.exports_sync.stop()
            except Exception as error: report['errors'].append('stop: '+str(error))
            try: script.unload(); report['scriptUnloaded'] = True
            except Exception as error: report['errors'].append('unload: '+str(error))
        if session:
            try: session.detach(); report['sessionDetached'] = True
            except Exception as error: report['errors'].append('detach: '+str(error))
        try: packets.put(None, timeout=2)
        except queue.Full: report['errors'].append('Writer queue could not drain')
        worker.join(3)
        report['writerStopped'] = not worker.is_alive()
    capture = report.get('capture', {})
    if (report['errors'] or not report['writerStopped'] or report['received'] != report['sent'] or
        not capture.get('hookBytesRestored') or capture.get('queueDropped') or capture.get('budgetRejected') or capture.get('errors')):
        raise RuntimeError('Native capture failed integrity/cleanup checks; see capture report')

def main():
    sys.stderr.reconfigure(encoding='utf-8')
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--pid', type=int)
    parser.add_argument('--parent-pid', type=int, default=0)
    parser.add_argument('--seconds', type=float, default=0)
    parser.add_argument('--report', type=pathlib.Path)
    args = parser.parse_args()
    report = {'scope': 'owned basic object PCM; local slot lifecycle; no driver', 'startMonotonic': time.monotonic()}
    owner = None
    try:
        if args.check:
            print(json.dumps(preflight(args.pid), ensure_ascii=False)); return 0
        if not 0 <= args.seconds <= 28800: raise ValueError('Invalid duration')
        if not args.parent_pid: raise ValueError('Live mode requires a renderer owner')
        owner = Owner(args.parent_pid)
        import msvcrt
        msvcrt.setmode(sys.stdout.fileno(), os.O_BINARY)
        live(args, owner, sys.stdout.buffer, report)
        report['passed'] = True
        return 0
    except Exception as error:
        report['passed'] = False; report['error'] = str(error)
        print(str(error), file=sys.stderr, flush=True); return 1
    finally:
        if owner: owner.close()
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')

if __name__ == '__main__': sys.exit(main())

"""Read-only, process-specific validation of the 3527 reader layout."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import datetime
import json
import math
from pathlib import Path
import struct


class Reader:
    def __init__(self, pid):
        self.k = c.WinDLL('kernel32', use_last_error=True)
        self.k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
        self.k.OpenProcess.restype = w.HANDLE
        self.k.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
        self.k.ReadProcessMemory.restype = w.BOOL
        self.k.CloseHandle.argtypes = [w.HANDLE]
        self.handle = self.k.OpenProcess(0x10, False, pid)  # PROCESS_VM_READ only
        if not self.handle:
            raise c.WinError(c.get_last_error())

    def read(self, addr, size):
        buf = c.create_string_buffer(size)
        got = c.c_size_t()
        if not self.k.ReadProcessMemory(self.handle, addr, buf, size, c.byref(got)) or got.value != size:
            raise OSError(f'unreadable 0x{addr:X}+0x{size:X}')
        return buf.raw

    def unpack(self, addr, fmt):
        return struct.unpack(fmt, self.read(addr, struct.calcsize(fmt)))

    def ptr(self, addr):
        return self.unpack(addr, '<Q')[0]

    def close(self):
        self.k.CloseHandle(self.handle)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--pid', type=int, required=True)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    r = Reader(args.pid)
    base = 0x140000000
    try:
        nt = base + r.unpack(base + 0x3C, '<I')[0]
        stamp = r.unpack(nt + 8, '<I')[0]
        size = r.unpack(nt + 0x50, '<I')[0]
        if (stamp, size) not in ((0x6AEA07A6, 0x13EB2000), (0x6AEB7D59, 0x13CFF000)):
            raise ValueError('not build 3527 or 3549')
        report = dict(timestamp=f'0x{stamp:X}', image_size=f'0x{size:X}', pid=args.pid,
                      captured_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
        objects = {}
        def hop(name, addr):
            ptr = r.ptr(addr)
            objects[name] = dict(address=f'0x{ptr:X}', vtable=f'0x{r.ptr(ptr)-base:X}')
            return ptr
        world = r.ptr(base+0x71BDEF0)  # WorldSystem does not start with a vtable.
        manager = hop('manager', world+0x30)
        actor = hop('actor', manager+0x50)
        components = r.ptr(actor+0x68)
        control = hop('control', components+0x40)
        inner = r.ptr(control+0x140)
        physics = hop('physics', inner+0x2C0)
        status = hop('status', components+0x20)
        container = r.ptr(status+0x18)
        entries = r.ptr(container+0x58)
        count = r.unpack(container+0x60, '<I')[0]
        report['objects'] = objects
        report['physics'] = {f'0x{off:X}': r.unpack(physics+off, '<3f') for off in (0x60,0x70,0x80,0x90)}
        report['origin'] = r.unpack(base+0x71AA150, '<3f')
        report['static_position'] = r.unpack(base+0x71CD0B0, '<3f')
        report['stats_count'] = count
        report['stats'] = [dict(zip(('id','current','regen','maximum'),r.unpack(entries+i*0x90,'<Qqqq')))
                           for i in range(min(count,64))]
        camera = hop('camera',base+0x70DA028)
        # Locate the known atmosphere sentinel in the camera and immediate pointer fields.
        cam_bytes = r.read(camera,0x1000)
        sentinel = struct.pack('<f',6360000.0)
        matches = []
        for off in range(0,0x1000,8):
            candidate = struct.unpack_from('<Q',cam_bytes,off)[0]
            if not 0x10000 < candidate < 0x7FFFFFFFFFFF or candidate & 7:
                continue
            try:
                block = r.read(candidate,0x1200)
            except OSError:
                continue
            positions = [i for i in range(0,len(block)-4,4) if block[i:i+4]==sentinel]
            if positions:
                matches.append(dict(pointer_offset=f'0x{off:X}',address=f'0x{candidate:X}',
                                    sentinel_offsets=[f'0x{i:X}' for i in positions],
                                    position=r.unpack(candidate+0x80,'<3f'),forward=r.unpack(candidate+0x90,'<3f'),
                                    view=r.unpack(candidate+0x3E0,'<16f'),projection=r.unpack(candidate+0x4E0,'<16f')))
        report['camera_candidates'] = matches
        expected = dict(manager=0x5944B38, actor=0x59127C8, control=0x59438F0,
                        physics=0x60CB4D0, status=0x591A710, camera=0x60EB0D8)
        checks = {f'{name}_vtable': int(objects[name]['vtable'],16)==vt for name,vt in expected.items()}
        axes = [report['physics'][f'0x{off:X}'] for off in (0x60,0x70,0x80)]
        dot = lambda a,b: sum(x*y for x,y in zip(a,b))
        checks['physics_basis'] = all(abs(dot(a,a)-1)<0.05 for a in axes) and all(
            abs(dot(axes[i],axes[j]))<0.05 for i,j in ((0,1),(0,2),(1,2)))
        position = [a+b for a,b in zip(report['physics']['0x90'],report['origin'])]
        report['static_position_error'] = math.dist(position,report['static_position'])
        checks['world_position'] = report['static_position_error'] < 1
        checks['stats_count'] = 0 < count <= 64
        checks['health_and_stamina'] = all(any(s['id']==idx and 0 <= s['current'] <= s['maximum']
            and s['maximum'] > 0 for s in report['stats']) for idx in (0,22))
        camera_match = next((m for m in matches if m['pointer_offset']=='0x430' and
                            m['sentinel_offsets']==['0xAC0']),None)
        checks['camera_layout'] = camera_match is not None
        if camera_match:
            checks['camera_near_player'] = math.dist(camera_match['position'],position)<50
            checks['camera_direction'] = abs(dot(camera_match['forward'],camera_match['forward'])-1)<0.01
            checks['camera_projection'] = all(math.isfinite(camera_match['projection'][i]) and
                                             camera_match['projection'][i]>0 for i in (0,5))
        report['checks'] = checks
        report['status'] = 'pass' if all(checks.values()) else 'fail'
        args.out.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(dict(status=report['status'],checks=checks,static_position_error=report['static_position_error']),indent=2))
        if report['status'] != 'pass':
            raise SystemExit(1)
    finally:
        r.close()


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
import json, os, re, shutil, socket, subprocess, tempfile, time
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
BUILD=ROOT/'build/m10-pmm-contiguous'
ISO=Path(os.environ.get('ISO_PATH',ROOT/'dist/ProZestOS-M10-PMM-Contiguous-UEFI-x86_64.iso')).resolve()
OVMF=Path('/usr/share/OVMF')
PAGE=4096
asm=subprocess.check_output(['objdump','-d','--disassemble=transition_checkpoint_halt',str(BUILD/'kernel.elf')],text=True)
halts={int(a,16)+1 for a in re.findall(r'^\s*([0-9a-f]+):\s+f4\s+hlt\b',asm,re.M)}
assert halts, 'No checkpoint HLT found'

def number(log,label):
    matches=re.findall(r'^'+re.escape(label)+r'\s+(?:0x)?([0-9a-f]+)\s*$',log,re.M|re.I)
    assert len(matches)==1,(label,matches)
    return int(matches[0],16)

def rows(log,label,columns):
    return [tuple(int(v,16) for v in line.split()[1:]) for line in log.splitlines()
            if line.startswith(label+' ') and len(line.split())==columns+1]

def verify(log):
    for marker in ('DESCRIPTOR ACTIVATION: PASS','ALLOCATOR-SAFE INVENTORY: PASS',
        'PMM METADATA PLAN: PASS','METADATA MAPPING VALIDATION: PASS','PMM PREINIT GUARD: PASS',
        'PMM INITIALIZATION: PASS','PMM BITMAP READBACK: PASS','PMM COUNTERS: PASS',
        'PMM METADATA EXCLUDED: PASS','PMM REINITIALIZATION GUARD: PASS',
        'PHYSICAL MEMORY ALLOCATOR: INITIALIZED','PMM ALLOC PREINIT: PASS','PMM NULL OUTPUT GUARD: PASS','PMM SINGLE PAGE ALLOCATION: PASS','PMM LIVE REINIT GUARD: PASS','PMM FREE PREINIT: PASS','PMM SINGLE PAGE FREE: PASS','PMM FREE GUARDS: PASS','PMM PAGE REUSE: PASS','PMM OUTPUT ALIAS GUARDS: PASS','PMM PROTECTED ADDRESS GUARDS: PASS','PMM HOLE GUARDS: PASS','PMM LIVE OOM GUARD: PASS','PMM EXHAUSTION RESTORE: PASS','PMM FINAL REINIT GUARD: PASS','PMM CONTIG PREINIT: PASS','PMM CONTIG INVALID GUARDS: PASS','PMM CONTIG ALIGNED ALLOC: PASS','PMM CONTIG SECOND ALLOC: PASS','PMM CONTIG RELEASE: PASS','PMM CONTIG REUSE: PASS','PMM CONTIG ATOMIC REJECTION: PASS','PMM CONTIG FINAL STATE: PASS','M10.8 COMPLETE'):
        assert log.splitlines().count(marker)==1,marker
    assert not re.search(r': FAIL\b|FATAL CPU EXCEPTION|NESTED CPU EXCEPTION',log)
    base=number(log,'PMM BITMAP BASE:')
    size=number(log,'PMM BITMAP BYTES:')
    managed=number(log,'PMM MANAGED PAGES:')
    eligible=rows(log,'ELIGIBLE',2)
    protected=rows(log,'PROTECTED',3)
    ranges=rows(log,'MANAGED',3)
    assert eligible and protected and ranges and base%PAGE==size%PAGE==0 and size>0
    pre=sum((b-a)//PAGE for a,b in eligible)
    assert pre==number(log,'Pre-metadata eligible pages:')
    assert size==((pre+32767)//32768)*PAGE
    assert managed==pre-size//PAGE
    assert number(log,'PMM FREE PAGES:')==managed
    assert number(log,'PMM ALLOCATED PAGES:')==0
    assert number(log,'PMM UNAVAILABLE BITS:')==size*8-managed
    assert any(a<=base and base+size<=b for a,b in eligible)
    assert all(not (base<b and a<base+size) for a,b,_ in protected)
    expected_ranges=[]
    index=0
    for a,b in eligible:
        assert a<b and a%PAGE==b%PAGE==0
        assert all(not (a<d and c<b) for c,d,_ in protected)
        parts=[(a,b)] if b<=base or a>=base+size else [(a,base),(base+size,b)]
        for c,d in parts:
            if c<d:
                expected_ranges.append((c,d,index)); index+=(d-c)//PAGE
    assert ranges==expected_ranges and index==managed
    full,tail=divmod(managed,8)
    expected=b'\x00'*full+(bytes([(0xff<<tail)&0xff]) if tail else b'')
    expected+=b'\xff'*(size-len(expected))
    allocated=rows(log,'ALLOCATED',2)
    assert len(allocated)==32 and len({pa for _,pa in allocated})==32
    assert number(log,'PMM AFTER FREE PAGES:')==managed-32
    assert number(log,'PMM AFTER ALLOCATED PAGES:')==32
    expected=bytearray(expected)
    for i,(index,pa) in enumerate(allocated):
        assert index==i and pa%PAGE==0 and pa!=0
        matches=[a+(index-first)*PAGE for a,b,first in ranges if first<=index<first+(b-a)//PAGE]
        assert matches==[pa],(index,pa,matches)
        assert not base<=pa<base+size
        assert all(not (a<=pa<b) for a,b,_ in protected)
        expected[index//8]|=1<<(index%8)
    freed=rows(log,'FREED',2)
    reused=rows(log,'REUSED',2)
    assert freed==[allocated[i] for i in range(31,0,-2)]
    assert reused==[allocated[i] for i in range(1,16,2)]
    for index,pa in freed:
        assert expected[index//8]&(1<<(index%8))
        expected[index//8]&=~(1<<(index%8))
    assert number(log,'PMM RELEASED FREE PAGES:')==managed-16
    assert number(log,'PMM RELEASED ALLOCATED PAGES:')==16
    for index,pa in reused:
        assert not expected[index//8]&(1<<(index%8))
        expected[index//8]|=1<<(index%8)
    assert number(log,'PMM FINAL FREE PAGES:')==managed-24
    assert number(log,'PMM FINAL ALLOCATED PAGES:')==24
    assert number(log,'PMM PROTECTED REJECTIONS:')==2*len(protected)
    assert number(log,'PMM HOLE REJECTIONS:')==sum(a[1]<b[0] for a,b in zip(ranges,ranges[1:]))
    assert number(log,'PMM EXHAUSTION ADDITIONAL PAGES:')==managed-24
    # Independent first-fit reference over physical ranges, not dense-index adjacency.
    def claim(count,align,label):
        pa=number(log,label)
        for a,b,first in ranges:
            for candidate in range(a,b-count*PAGE+1,PAGE):
                if candidate%(align*PAGE): continue
                i=first+(candidate-a)//PAGE
                if any(expected[k//8]&(1<<(k%8)) for k in range(i,i+count)): continue
                assert candidate==pa,(label,candidate,pa)
                for k in range(i,i+count): expected[k//8]|=1<<(k%8)
                return i
        raise AssertionError('No suitable physical run for '+label)
    claim(8,8,'PMM CONTIG BLOCK:')
    small=claim(3,1,'PMM CONTIG SMALL:')
    for k in range(small,small+3): expected[k//8]&=~(1<<(k%8))
    assert number(log,'PMM CONTIG FINAL FREE:')==managed-32
    assert number(log,'PMM CONTIG FINAL ALLOCATED:')==32
    assert log.splitlines().count('PMM CONTIG RANGE BOUNDARY: PASS')==len(ranges) or log.splitlines().count('PMM CONTIG RANGE BOUNDARY')==len(ranges)
    return base,size,managed,bytes(expected)

def boot(ram,accel):
    run=Path(tempfile.mkdtemp(prefix='run-',dir=BUILD))
    sock=run/'qmp.sock'
    assert len(str(sock).encode())<108,'QMP socket path too long'
    shutil.copyfile(OVMF/'OVMF_VARS_4M.fd',run/'vars.fd')
    log=run/'kernel.log'
    args=['qemu-system-x86_64','-machine','q35','-accel',accel,'-cpu','host' if accel=='kvm' else 'max',
        '-smp','1','-m',str(ram),'-display','none','-vga','std','-serial','none','-monitor','none',
        '-drive',f'if=pflash,format=raw,unit=0,readonly=on,file={OVMF}/OVMF_CODE_4M.fd',
        '-drive',f'if=pflash,format=raw,unit=1,file={run}/vars.fd','-cdrom',str(ISO),'-boot','order=d',
        '-chardev',f'file,id=dbg,path={log}','-device','isa-debugcon,iobase=0xe9,chardev=dbg',
        '-qmp',f'unix:{sock},server=on,wait=off','-no-reboot','-d','cpu_reset,guest_errors','-D',str(run/'qemu.log')]
    print(f'=== QEMU {ram} MiB {accel} | {run} ===',flush=True)
    with (run/'stderr.log').open('w') as err:
        proc=subprocess.Popen(args,stdout=err,stderr=err)
        try:
            deadline=time.monotonic()+75
            while time.monotonic()<deadline:
                if proc.poll() is not None: raise RuntimeError(f'QEMU exited: {proc.returncode}')
                if sock.exists() and log.exists() and 'M10.8 COMPLETE\n' in log.read_text(errors='replace'): break
                time.sleep(.2)
            else: raise RuntimeError('Boot timeout; inspect '+str(log))
            text=log.read_text(errors='replace')
            base,size,managed,expected=verify(text)
            with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as conn:
                conn.settimeout(8); conn.connect(str(sock))
                with conn.makefile('rwb') as f:
                    assert 'QMP' in json.loads(f.readline())
                    def cmd(name,arguments=None):
                        req={'execute':name}
                        if arguments is not None: req['arguments']=arguments
                        f.write(json.dumps(req).encode()+b'\n'); f.flush()
                        while True:
                            line=f.readline()
                            if not line: raise RuntimeError('QMP closed')
                            response=json.loads(line)
                            if 'error' in response: raise RuntimeError(response['error'])
                            if 'return' in response: return response['return']
                    cmd('qmp_capabilities')
                    for _ in range(30):
                        regs=cmd('human-monitor-command',{'command-line':'info registers'})
                        if 'HLT=1' in regs: break
                        time.sleep(.1)
                    (run/'monitor.txt').write_text(regs)
                    assert 'HLT=1' in regs and 'CPL=0' in regs,regs
                    rip=int(re.search(r'\bRIP=([0-9a-f]+)',regs,re.I)[1],16)
                    flags=int(re.search(r'\bRFL=([0-9a-f]+)',regs,re.I)[1],16)
                    assert rip in halts and not flags&0x200,regs
                    for name,val in [('CS',8),('TR',24)]:
                        assert int(re.search(r'\b'+name+r'\s*=\s*([0-9a-f]+)',regs,re.I)[1],16)==val,regs
                    for reg in ('CR3','CR4'):
                        assert number(text,'BEFORE '+reg)==number(text,'AFTER '+reg)
                        assert int(re.search(r'\b'+reg+r'=([0-9a-f]+)',regs,re.I)[1],16)==number(text,'AFTER '+reg)
                    assert cmd('query-status')['running']
                    cmd('pmemsave',{'val':base,'size':size,'filename':str(run/'bitmap.bin')})
                    assert (run/'bitmap.bin').read_bytes()==expected,'Physical bitmap mismatch'
                    cmd('screendump',{'filename':str(run/'screen.ppm')})
            print(f'PASS: {managed:,} managed, {managed-32:,} free, 32 allocated (8-page aligned block retained); {size:,} bitmap bytes verified physically; RIP=0x{rip:016X}; IF=0 HLT=1 CPL=0')
            resets=(run/'qemu.log').read_text(errors='replace').count('CPU Reset')
            assert resets==2, f'Unexpected reset count: {resets}'
            print('CPU reset records:',resets)
            print('Evidence:',run)
            return managed
        finally:
            if proc.poll() is None:
                proc.terminate()
                try: proc.wait(timeout=5)
                except subprocess.TimeoutExpired: proc.kill(); proc.wait()

results=[]
for ram,accel in [(256,'kvm'),(512,'kvm'),(1024,'kvm'),(256,'tcg')]:
    results.append((ram,accel,boot(ram,accel)))
assert results[0][2]<results[1][2]<results[2][2]
print('=== M10.8 RUNTIME SUMMARY ===')
for ram,accel,pages in results: print(f'{ram:4} MiB {accel}: {pages:,} managed, {pages-32:,} free, 32 allocated')
print('M10.8 QEMU: 4/4 PASS')

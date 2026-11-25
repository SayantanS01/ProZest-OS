#!/usr/bin/env python3
import json, os, re, shutil, socket, subprocess, tempfile, time
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
BUILD=ROOT/'build/rc2'
ISO=Path(os.environ.get('ISO_PATH',ROOT/'dist/ProZestOS-v0.0.7-rc2-x86_64.iso')).resolve()
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
    markers=('DESCRIPTOR ACTIVATION: PASS','MEMORY INVENTORY: PASS','ALLOCATOR-SAFE INVENTORY: PASS',
        'METADATA MAPPING VALIDATION: PASS','PMM METADATA PLAN: PASS','PMM PREINIT GUARD: PASS',
        'PMM INITIALIZATION: PASS','PMM BITMAP READBACK: PASS','PMM COUNTERS: PASS','PMM METADATA EXCLUDED: PASS',
        'PHYSICAL MEMORY ALLOCATOR: INITIALIZED','RC2 SINGLE ALLOCATION: PASS','RC2 CONTIGUOUS ALLOCATION: PASS',
        'RC2 LIVE COUNTERS: PASS','RC2 REINIT GUARD: PASS','RC2 CONTIGUOUS RELEASE: PASS','RC2 SINGLE RELEASE: PASS',
        'RC2 SINGLE REUSE: PASS','RC2 FINAL PMM STATE: PASS','KERNEL READY - M10','M10.10 COMPLETE')
    lines=log.splitlines()
    assert all(lines.count(m)==1 for m in markers), [m for m in markers if lines.count(m)!=1]
    assert [lines.index(m) for m in markers]==sorted(lines.index(m) for m in markers)
    assert not re.search(r': FAIL\b|FATAL CPU EXCEPTION|NESTED CPU EXCEPTION|STRESS_EVENT|KERNEL READY - M09',log)
    base=number(log,'PMM BITMAP BASE:'); size=number(log,'PMM BITMAP BYTES:'); managed=number(log,'PMM MANAGED PAGES:')
    eligible=rows(log,'ELIGIBLE',2); protected=rows(log,'PROTECTED',3); ranges=rows(log,'MANAGED',3)
    regions=rows(log,'REGION',5); tables=rows(log,'TABLE',2)
    assert regions and eligible and protected and ranges and tables
    assert len(regions)==number(log,'Memory map entries:')
    assert [i for i,*_ in regions]==list(range(len(regions)))
    assert all(a<b for _,a,b,_,_ in regions)
    assert all(a[2]<=b[1] for a,b in zip(regions,regions[1:]))
    def merge(intervals):
        result=[]
        for a,b in sorted(intervals):
            if a>=b: continue
            if result and a<=result[-1][1]: result[-1]=(result[-1][0],max(b,result[-1][1]))
            else: result.append((a,b))
        return result
    def subtract(intervals,blocked):
        out=[]
        for a,b in intervals:
            for c,d in blocked:
                if d<=a: continue
                if c>=b: break
                if a<c: out.append((a,c))
                a=max(a,d)
                if a>=b: break
            if a<b: out.append((a,b))
        return out
    candidates=merge([((a+PAGE-1)//PAGE*PAGE,b//PAGE*PAGE) for _,a,b,t,_ in regions if t==0])
    blocked=merge([(a,b) for a,b,_ in protected])
    assert all(a<b and a%PAGE==b%PAGE==0 for a,b,_ in protected)
    assert all(a<b and a%PAGE==b%PAGE==0 for a,b in eligible)
    assert all(a[1]<=b[0] for a,b in zip(eligible,eligible[1:]))
    assert merge(eligible)==merge(subtract(candidates,blocked)), 'Independent eligible reconstruction mismatch'
    assert all(any(a<=pa and pa+PAGE<=b for a,b in blocked) for pa,_ in tables)
    assert any(a==0 and b>=PAGE for a,b in blocked)
    pre=sum((b-a)//PAGE for a,b in eligible)
    assert pre==number(log,'Pre-metadata eligible pages:')==number(log,'Allocator-eligible pages:')
    assert size==((pre+32767)//32768)*PAGE and size>0 and base%PAGE==0
    assert base==next(a for a,b in eligible if b-a>=size), 'Metadata first-fit mismatch'
    assert managed==pre-size//PAGE==number(log,'Final managed pages:')
    assert number(log,'PMM FREE PAGES:')==managed and number(log,'PMM ALLOCATED PAGES:')==0
    assert number(log,'PMM UNAVAILABLE BITS:')==size*8-managed
    assert number(log,'Metadata mapping pages verified:')==size//PAGE
    expected_ranges=[]; index=0
    for a,b in eligible:
        for c,d in subtract([(a,b)],[(base,base+size)]):
            expected_ranges.append((c,d,index)); index+=(d-c)//PAGE
    assert ranges==expected_ranges and index==managed
    single=number(log,'RC2 SINGLE:'); block=number(log,'RC2 BLOCK:')
    assert single==ranges[0][0] and single!=0 and number(log,'RC2 REUSED:')==single
    expected_block=None
    for a,b,_ in ranges:
        for pa in range((a+32767)//32768*32768,b-32768+1,32768):
            if not pa<=single<pa+32768:
                expected_block=pa; break
        if expected_block is not None: break
    assert block==expected_block and block%32768==0
    assert number(log,'RC2 FINAL FREE:')==managed and number(log,'RC2 FINAL ALLOCATED:')==0
    full,tail=divmod(managed,8)
    expected=b'\x00'*full+(bytes([(0xff<<tail)&0xff]) if tail else b'')
    expected+=b'\xff'*(size-len(expected))
    return base,size,managed,expected

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
            deadline=time.monotonic()+120
            while time.monotonic()<deadline:
                if proc.poll() is not None: raise RuntimeError(f'QEMU exited: {proc.returncode}')
                if sock.exists() and log.exists() and 'M10.10 COMPLETE\n' in log.read_text(errors='replace'): break
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
            print(f'PASS: {managed:,} managed, {managed:,} free, 0 allocated (normal boot smoke test verified); {size:,} bitmap bytes verified physically; RIP=0x{rip:016X}; IF=0 HLT=1 CPL=0')
            resets=(run/'qemu.log').read_text(errors='replace').count('CPU Reset')
            assert resets==2, f'Unexpected reset count: {resets}'
            print('CPU reset records:',resets)
            print('Evidence:',run)
            return {'ram_mib':ram,'accel':accel,'managed':managed,'free':managed,'allocated':0,'bitmap_bytes':size,'evidence':str(run.relative_to(ROOT))}
        finally:
            if proc.poll() is None:
                proc.terminate()
                try: proc.wait(timeout=5)
                except subprocess.TimeoutExpired: proc.kill(); proc.wait()

results=[]
for ram,accel in [(256,'kvm'),(512,'kvm'),(1024,'kvm'),(256,'tcg')]:
    results.append(boot(ram,accel))
assert results[0]['managed']<results[1]['managed']<results[2]['managed']
print('=== RC2 RUNTIME SUMMARY ===')
for r in results: print(f"{r['ram_mib']:4} MiB {r['accel']}: {r['managed']:,} managed, {r['free']:,} free, 0 allocated")
(BUILD/'runtime-summary.json').write_text(json.dumps(results,indent=2)+'\n')
print('RC2 QEMU: 4/4 PASS')

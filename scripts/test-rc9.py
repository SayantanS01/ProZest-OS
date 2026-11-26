#!/usr/bin/env python3
import json, os, re, shutil, socket, subprocess, tempfile, time, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
STAGE=int(sys.argv[1]); assert STAGE==17
RC=STAGE-8
BUILD=Path(os.environ.get('BUILD_DIR',ROOT/f'build/rc{RC}')).resolve()
MODE=0
ISO=Path(os.environ.get('ISO_PATH',ROOT/f'dist/ProZestOS-v0.0.7-rc{RC}-x86_64.iso')).resolve()
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
    # Strip the entire label, not just the first word. PCI DEVICE and
    # SCHED TASK both contain spaces. COUNT: is a separate scalar record.
    prefix=label+' '
    result=[]
    for line in log.splitlines():
        if not line.startswith(prefix): continue
        fields=line[len(prefix):].split()
        if len(fields)==columns:
            result.append(tuple(int(v,16) for v in fields))
    return result

def verify(log):
    markers=('DESCRIPTOR ACTIVATION: PASS','MEMORY INVENTORY: PASS','ALLOCATOR-SAFE INVENTORY: PASS',
        'METADATA MAPPING VALIDATION: PASS','PMM METADATA PLAN: PASS','PMM PREINIT GUARD: PASS',
        'PMM INITIALIZATION: PASS','PMM BITMAP READBACK: PASS','PMM COUNTERS: PASS','PMM METADATA EXCLUDED: PASS',
        'PHYSICAL MEMORY ALLOCATOR: INITIALIZED','RC2 SINGLE ALLOCATION: PASS','RC2 CONTIGUOUS ALLOCATION: PASS',
        'RC2 LIVE COUNTERS: PASS','RC2 REINIT GUARD: PASS','RC2 CONTIGUOUS RELEASE: PASS','RC2 SINGLE RELEASE: PASS',
        'RC2 SINGLE REUSE: PASS','RC2 FINAL PMM STATE: PASS')
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
    return base,size,managed,expected,ranges

def verify_vmm(log):
    prefix=log.split('VMM CPUID WIDTH: PASS\n',1)[0]
    assert prefix!=log
    base,size,managed,expected,ranges=verify(prefix)
    markers=['M11.1 PAGING DIAGNOSTICS: PASS','M11.2 PTE FORMAT: PASS','M11.3 TRANSLATION: PASS',
        'M11.4 TABLE ALLOCATION: PASS','M11.5 PAGE MAP: PASS','M11.6 UNMAP AND TLB: PASS',
        'M11.7 PERMISSIONS: PASS','VMM HARDWARE EXECUTE: PASS','M11.8 PRIVATE ADDRESS SPACE: PASS','M11.9 STRESS CLEANUP: PASS',
        'VMM ROOT RELEASE: PASS','VMM ZERO LEAKS: PASS','VMM FINAL ROOT ONLY: PASS']
    lines=log.splitlines()
    assert all(lines.count(m)==1 for m in markers)
    assert [lines.index(m) for m in markers]==sorted(lines.index(m) for m in markers)
    assert lines.count('VMM STRESS MAP')+lines.count('VMM STRESS FREE')==512
    assert lines.count('VMM STRESS MAP')==lines.count('VMM STRESS FREE')+lines.count('VMM STRESS CLEANUP')
    assert ': FAIL' not in log and 'NESTED CPU EXCEPTION' not in log
    old=number(log,'VMM ORIGINAL ROOT:'); root=number(log,'VMM ACTIVE ROOT:'); window=number(log,'VMM PRIVATE WINDOW:')
    assert old==number(log,'AFTER CR3')&0x000ffffffffff000 and old!=root
    assert window>>48==0xffff and 256<=((window>>39)&511)<511 and window% (1<<39)==0
    assert number(log,'VMM FINAL ALLOCATED:')==1
    def index(pa):
        assert pa%4096==0
        return next(i+(pa-a)//4096 for a,b,i in ranges if a<=pa<b)
    bitmap=bytearray(expected); i=index(root); bitmap[i//8]|=1<<(i%8)
    if MODE:
        assert lines.count('VMM FAULT PRECONDITION: PASS')==1
        assert lines.count('VMM INTENTIONAL FAULT')==1 and lines.count('FATAL CPU EXCEPTION')==1
        assert lines.count('EMERGENCY REPORT COMPLETE')==1 and 'M11 COMPLETE' not in lines
        assert number(log,'VMM FAULT MODE:')==MODE and number(log,'VMM EXPECTED FAULT VA:')==window
        assert number(log,'Vector:')==14 and number(log,'CR2:')==window
        assert number(log,'CPU error code:')=={1:0,2:3,3:17}[MODE]
        assert number(log,'Saved CS:')==8
    else:
        assert all(lines.count(m)==1 for m in ['M11.10 VMM INTEGRATION: PASS','M12 COMPLETE'])
        assert 'FATAL CPU EXCEPTION' not in log
    irq_markers=['M12.1 CPU INTERRUPT SUPPORT: PASS','M12.2 IRQ IDT: PASS','M12.3 IRQ DISPATCH: PASS',
        'IRQ REGISTER STRESS: PASS','M12.4 PIC MASKS: PASS','IRQ ACPI MADT: PASS','IRQ MMIO TRANSLATION: PASS',
        'M12.6 LOCAL APIC: PASS','M12.5 PIC PIT DELIVERY: PASS','M12.7 IOAPIC PIT DELIVERY: PASS',
        'IRQ LAPIC CALIBRATION: PASS','IRQ LAPIC TIMER DELIVERY: PASS','M12.8 TIMER ACCOUNTING: PASS',
        'M12.9 IRQ SAFETY: PASS','IRQ FINAL ROOT ONLY: PASS','M12.10 IRQ INTEGRATION: PASS','M12 COMPLETE']
    assert all(lines.count(m)==1 for m in irq_markers),[m for m in irq_markers if lines.count(m)!=1]
    assert [lines.index(m) for m in irq_markers]==sorted(lines.index(m) for m in irq_markers)
    assert lines.count('IRQ SOFTWARE ROUNDTRIP')==1023
    assert lines.count('IRQ EXCEPTION GATE PRESERVED')==512
    assert lines.count('IRQ TABLE RELEASE')==3
    pic=number(log,'IRQ PIC TICKS:'); io=number(log,'IRQ IOAPIC TICKS:'); apic=number(log,'IRQ LAPIC TICKS:')
    assert pic>=16 and io>=80 and apic>=64 and number(log,'IRQ SOFTWARE TICKS:')==1024 and number(log,'IRQ BAD:')==0
    divisor=number(log,'IRQ PIT DIVISOR:'); assert divisor==11932
    assert number(log,'IRQ PIT ELAPSED MS:')==(pic+io)*divisor*1000//1193182
    assert 16<=number(log,'IRQ LAPIC PERIOD:')<0x7fffffff
    assert number(log,'IRQ LAPIC PA:')==0xfee00000 and number(log,'IRQ IOAPIC PA:')==0xfec00000
    assert number(log,'IRQ PIT GSI:') in range(24)
    stage_markers={13:['HEAP PREINIT','HEAP BACKING','HEAP ALIGNMENT','HEAP FREE','HEAP REUSE','HEAP COALESCE','HEAP CALLOC REALLOC','HEAP OOM','HEAP STRESS','HEAP INTEGRATION'],
        14:['DRIVER CONTEXT','UART DETECTION','UART LOOPBACK','UART TX','PCI ENUMERATION','PCI CLASSIFICATION','PS2 KEYBOARD INIT','KEYBOARD MAKE BREAK','DRIVER SAFETY','DRIVER INTEGRATION'],
        15:['SCHEDULER CONTEXT','TASK STACKS','TASK CONTEXT','ROUND ROBIN','SCHEDULER GATES','TIMER PREEMPTION','COOPERATIVE YIELD','TASK EXIT','SCHEDULER CLEANUP','SCHEDULER INTEGRATION'],
        16:['PRIVILEGE CONTEXT','USER GDT','USER PAGE TABLES','ADDRESS SPACE ISOLATION','USER STACK','SYSCALL GATE','SYSCALL DISPATCH','USER PROGRAMS','PRIVILEGE FAULTS','USERSPACE CLEANUP'],
        17:['BLOCK CONTEXT','ATA IDENTIFY','SECTOR READ','FILESYSTEM MOUNT','FILE READ','FILE CREATE','FILE UPDATE','FILE DELETE','REMOUNT VERIFY','STORAGE CLEANUP']}
    order=[]
    for stage in range(13,STAGE+1):
        order.extend([f'M{stage}.{i} {m}: PASS' for i,m in enumerate(stage_markers[stage],1)])
        order.append(f'M{stage} COMPLETE')
    order.extend([f'KERNEL READY - M{STAGE}','SERVICES COMPLETE'])
    assert all(lines.count(m)==1 for m in order),[m for m in order if lines.count(m)!=1]
    assert [lines.index(m) for m in order]==sorted(lines.index(m) for m in order)
    assert lines.index('M12 COMPLETE')<lines.index(order[0])
    assert not re.search(r': FAIL\b|FATAL CPU EXCEPTION|NESTED CPU EXCEPTION',log)
    assert number(log,'HEAP ARENA BYTES:')==262144
    pa=number(log,'HEAP ARENA PA:');assert any(a<=pa and pa+262144<=b for a,b,_ in ranges)
    assert not pa<=root<pa+262144
    assert number(log,'HEAP STRESS STEPS:')==4096
    # Independent replay of the deterministic heap workload using a unit bitmap.
    rng=0x6a09e667
    def rnd():
        nonlocal rng
        rng^=(rng<<13)&0xffffffff; rng^=rng>>17; rng^=(rng<<5)&0xffffffff
        return rng
    slots={}; units=bytearray(262144//16); allocs=frees=peak=used=0
    for _ in range(4096):
        i=rnd()%64
        if i in slots:
            a,n=slots.pop(i);assert all(units[a:a+n]);units[a:a+n]=bytes(n);used-=n;frees+=1
        else:
            n=(1+rnd()%2048+15)//16
            a=units.find(bytes(n));assert a>=0;units[a:a+n]=bytes([1])*n;slots[i]=(a,n);used+=n;allocs+=1
        peak=max(peak,used*16)
    frees+=len(slots)
    assert number(log,'HEAP STRESS ALLOCS:')==allocs==frees==number(log,'HEAP STRESS FREES:')
    assert number(log,'HEAP PEAK BYTES:')==peak
    assert lines.count('HEAP ARENA RELEASE')==(2 if STAGE>=15 else 1)
    assert lines.count('HEAP PAGE MAPPING')==(128 if STAGE>=15 else 64)
    if STAGE>=14:
        pci=rows(log,'PCI DEVICE',4)
        assert len(pci)==number(log,'PCI DEVICE COUNT:') and len({(d,f) for d,f,_,_ in pci})==len(pci)
        assert [int(x,16) for x in re.findall(r'^KEY BYTE:\s+(?:0x)?([0-9a-f]+)$',log,re.M|re.I)]==[0x1e,0x9e]
        assert lines.count('UART LOOPBACK BYTE')==64
    if STAGE>=15:
        task=rows(log,'SCHED TASK',4);assert [i for i,*_ in task]==[1,2,3]
        assert all(t>=4 and y==9 and w>0 for _,t,y,w in task)
        ticks=number(log,'SCHED TIMER EVENTS:');yields=number(log,'SCHED YIELD EVENTS:');switches=number(log,'SCHED SWITCHES:')
        assert ticks==sum(t for _,t,_,_ in task) and yields>=28 and switches==ticks+yields
        assert lines.count('SCHED STACK RELEASE')==3 and lines.count('SCHED TABLE RELEASE')==3
    assert number(log,'USER SYSCALLS:')==10 and number(log,'USER FAULTS:')==5
    assert lines.count('M16 USER WRITE: PROZEST')==2
    assert lines.count('USER FRAME ALLOC')==14 and lines.count('USER FRAME RELEASE')==14
    assert lines.count('USER ENTRY FRAME')==15 and lines.count('USER KERNEL RETURN')==7
    faults=rows(log,'USER FAULT',4)
    assert len(faults)==5 and faults[:4]==[(1,14,5,number(log,'USER KERNEL PROBE:')),(2,14,7,0x400000),(3,14,21,0x401040),(4,14,4,0x402000)]
    assert faults[4][:3]==(5,13,0)
    spaces=rows(log,'USER FRAMESET',8)
    assert [s[0] for s in spaces]==[1,2]
    frames=[pa for s in spaces for pa in s[1:]]
    assert len(set(frames))==14 and root not in frames
    for pa in frames:
        i=index(pa);assert not (bitmap[i//8]>>(i%8))&1
    return base,size,managed,bitmap,root,old,window,index

def send_a(sock):
    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as conn:
        conn.settimeout(8);conn.connect(str(sock))
        with conn.makefile('rwb') as f:
            assert 'QMP' in json.loads(f.readline())
            for req in ({'execute':'qmp_capabilities'},{'execute':'send-key','arguments':{'keys':[{'type':'qcode','data':'a'}],'hold-time':100}}):
                f.write(json.dumps(req).encode()+b'\n');f.flush()
                while True:
                    line=f.readline();assert line,'QMP disconnected'
                    r=json.loads(line)
                    if 'error' in r:raise RuntimeError(r['error'])
                    if 'return' in r:break

def boot(ram,accel,disk,prior):
    run=Path(tempfile.mkdtemp(prefix='run-',dir=BUILD))
    sock=run/'qmp.sock'
    assert len(str(sock).encode())<108,'QMP socket path too long'
    shutil.copyfile(OVMF/'OVMF_VARS_4M.fd',run/'vars.fd')
    log=run/'kernel.log'
    args=['qemu-system-x86_64','-machine','q35','-accel',accel,'-cpu','host' if accel=='kvm' else 'max',
        '-smp','1','-m',str(ram),'-display','none','-vga','std','-serial',f'file:{run}/serial.log','-monitor','none',
        '-drive',f'if=pflash,format=raw,unit=0,readonly=on,file={OVMF}/OVMF_CODE_4M.fd',
        '-drive',f'if=pflash,format=raw,unit=1,file={run}/vars.fd','-cdrom',str(ISO),'-boot','order=d',
        '-chardev',f'file,id=dbg,path={log}','-device','isa-debugcon,iobase=0xe9,chardev=dbg',
        '-qmp',f'unix:{sock},server=on,wait=off','-no-reboot','-d','cpu_reset,guest_errors','-D',str(run/'qemu.log')]
    args += ['-device','isa-ide,id=pzide,iobase=0x1e8,iobase2=0x3ee,irq=11',
             '-drive',f'if=none,id=pzdisk,format=raw,cache=writeback,file={disk}',
             '-device','ide-hd,bus=pzide.0,drive=pzdisk,serial=PROZEST-M17-LAB']
    print(f'=== QEMU {ram} MiB {accel} | {run} ===',flush=True)
    with (run/'stderr.log').open('w') as err:
        proc=subprocess.Popen(args,stdout=err,stderr=err)
        try:
            deadline=time.monotonic()+120
            injected=False
            while time.monotonic()<deadline:
                if proc.poll() is not None: raise RuntimeError(f'QEMU exited: {proc.returncode}')
                if sock.exists() and log.exists():
                    current=log.read_text(errors='replace')
                    if STAGE>=14 and not injected and 'M14 KEYBOARD INPUT READY\n' in current:
                        send_a(sock);injected=True
                    if 'SERVICES COMPLETE\n' in current:break
                if log.exists() and re.search(r': FAIL\b|FATAL CPU EXCEPTION|NESTED CPU EXCEPTION',log.read_text(errors='replace')): raise RuntimeError('Kernel failed; inspect '+str(log))
                time.sleep(.2)
            else: raise RuntimeError('Boot timeout; inspect '+str(log))
            text=log.read_text(errors='replace')
            assert number(text,'STORAGE SECTORS:')==2048 and number(text,'STORAGE PRIOR FILE:')==prior
            assert text.splitlines().count('M17 PERSISTENCE STATE: PASS')==1
            base,size,managed,expected,root,old,window,index=verify_vmm(text)
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
                    assert (MODE or rip in halts) and not flags&0x200,regs
                    for name,val in [('CS',8),('TR',24)]:
                        assert int(re.search(r'\b'+name+r'\s*=\s*([0-9a-f]+)',regs,re.I)[1],16)==val,regs
                    assert int(re.search(r'\bCR3=([0-9a-f]+)',regs,re.I)[1],16)==(root|(number(text,'AFTER CR3')&0x18)),regs
                    assert int(re.search(r'\bCR4=([0-9a-f]+)',regs,re.I)[1],16)==number(text,'AFTER CR4'),regs
                    if MODE: assert int(re.search(r'\bCR2=([0-9a-f]+)',regs,re.I)[1],16)==window,regs
                    def table(pa,name):
                        p=run/name
                        cmd('pmemsave',{'val':pa,'size':4096,'filename':str(p)})
                        data=p.read_bytes(); assert len(data)==4096
                        return [int.from_bytes(data[i:i+8],'little') for i in range(0,4096,8)]
                    orig=table(old,'old-root.bin'); new=table(root,'active-root.bin'); slot=(window>>39)&511
                    assert orig[slot]==0
                    assert all((a&~32)==(b&~32) for j,(a,b) in enumerate(zip(orig,new)) if j!=slot), 'Inherited PML4 mismatch'
                    if MODE in (2,3):
                        e=new[slot]; seen={root}
                        for level in (3,2,1):
                            assert e&~(0x000ffffffffff000|32)==3,hex(e)
                            pa=e&0x000ffffffffff000; assert pa not in seen; seen.add(pa)
                            i=index(pa); expected[i//8]|=1<<(i%8)
                            entries=table(pa,f'private-l{level}.bin'); j=(window>>(12+9*(level-1)))&511
                            assert all(x==0 for k,x in enumerate(entries) if k!=j)
                            e=entries[j]
                        assert e&~(0x000ffffffffff000|0x60)==((1<<63)| (1 if MODE==2 else 3)),hex(e)
                        pa=e&0x000ffffffffff000; assert pa not in seen
                        i=index(pa); expected[i//8]|=1<<(i%8)
                    else: assert new[slot]==0
                    # The test frees these frames without scrubbing and does not allocate again.
                    # Read the former user page tables physically; ignore hardware A/D bits.
                    for uid,r,pdpt,pd,pt,code,data,stack in rows(text,'USER FRAMESET',8):
                        a=table(r,f'user{uid}-pml4.bin'); b=table(pdpt,f'user{uid}-pdpt.bin')
                        c=table(pd,f'user{uid}-pd.bin'); d=table(pt,f'user{uid}-pt.bin')
                        assert a[0]&~0x60==pdpt|7 and not any(a[1:256])
                        assert all((x&~0x60)==(y&~0x64) for x,y in zip(a[256:],new[256:]))
                        assert b[0]&~0x60==pd|7 and not any(b[1:])
                        assert c[2]&~0x60==pt|7 and not any(c[:2]+c[3:])
                        assert d[0]&~0x60==code|5
                        assert d[1]&~0x60==data|7|(1<<63)
                        assert d[3]&~0x60==stack|7|(1<<63)
                        assert d[2]==0 and not any(d[4:])
                    # Verify the active IDT independently from physical RAM and ELF symbols.
                    idt_match=re.search(r'\bIDT=\s*([0-9a-f]+)\s+([0-9a-f]+)',regs,re.I)
                    assert idt_match and int(idt_match[1],16)==number(text,'IRQ IDT BASE:') and int(idt_match[2],16)==4095,regs
                    def dump(pa,n,name):
                        p=run/name;cmd('pmemsave',{'val':pa,'size':n,'filename':str(p)})
                        data=p.read_bytes();assert len(data)==n;return data
                    oldidt=dump(number(text,'IRQ OLD IDT PA:'),512,'old-idt.bin')
                    newidt=dump(number(text,'IRQ NEW IDT PA:'),4096,'active-idt.bin')
                    assert newidt[:512]==oldidt,'Exception gate bytes changed'
                    assert newidt[128*16:129*16]==b'\x00'*16,'User syscall gate not restored'
                    gdtr=re.search(r'\bGDT=\s*([0-9a-f]+)\s+([0-9a-f]+)',regs,re.I)
                    assert gdtr and int(gdtr[1],16)==number(text,'AFTER GDTR_BASE') and int(gdtr[2],16)==39,regs
                    syms={p[0]:int(p[2],16) for line in subprocess.check_output(['nm','-P','--defined-only',str(BUILD/'kernel.elf')],text=True).splitlines() if len(p:=line.split())>=3}
                    for v,name in [(32,'irq_pic0'),(39,'irq_pic7'),(47,'irq_pic15'),(64,'irq_io0'),(65,'irq_lapic_timer'),(66,'irq_soft'),(255,'irq_spurious')]:
                        g=newidt[v*16:v*16+16];addr=int.from_bytes(g[:2],'little')|(int.from_bytes(g[6:8],'little')<<16)|(int.from_bytes(g[8:12],'little')<<32)
                        assert addr==syms[name] and g[2:6]==b'\x08\x00\x00\x8e' and g[12:]==b'\x00'*4,(v,g.hex())
                    if STAGE>=14:
                        assert injected and b'PROZEST M14 UART TX PASS' in (run/'serial.log').read_bytes()
                        buses=cmd('query-pci');actual={}
                        for bus in buses:
                            if bus['bus']==0:
                                for d in bus['devices']:
                                    actual[(d['slot'],d['function'])]=(d['id']['vendor']|(d['id']['device']<<16),d['class_info']['class'])
                        assert {(d,f):(ident,cls>>8) for d,f,ident,cls in rows(text,'PCI DEVICE',4)}==actual,actual
                    if STAGE>=15:
                        assert newidt[67*16:68*16]==b'\x00'*16 or newidt[67*16:68*16]==dump(number(text,'IRQ OLD IDT PA:')+67*16,16,'old-yield-gate.bin')
                    assert cmd('query-status')['running']
                    cmd('pmemsave',{'val':base,'size':size,'filename':str(run/'bitmap.bin')})
                    assert (run/'bitmap.bin').read_bytes()==expected,'Physical bitmap mismatch'
                    cmd('screendump',{'filename':str(run/'screen.ppm')})
            print(f'PASS: mode={MODE}; {managed:,} managed; {size:,} bitmap bytes verified physically; CR3={root:#x}; IF=0 HLT=1 CPL=0')
            resets=(run/'qemu.log').read_text(errors='replace').count('CPU Reset')
            assert resets==2, f'Unexpected reset count: {resets}'
            print('CPU reset records:',resets)
            print('Evidence:',run)
            return {'ram_mib':ram,'accel':accel,'managed':managed,'free':managed-(5 if MODE in (2,3) else 1),'allocated':(5 if MODE in (2,3) else 1),'mode':MODE,'pic_ticks':number(text,'IRQ PIC TICKS:'),'ioapic_ticks':number(text,'IRQ IOAPIC TICKS:'),'lapic_ticks':number(text,'IRQ LAPIC TICKS:'),'pit_ms':number(text,'IRQ PIT ELAPSED MS:'),'bitmap_bytes':size,'evidence':str(run.relative_to(ROOT))}
        finally:
            if proc.poll() is None:
                proc.terminate()
                try: proc.wait(timeout=5)
                except subprocess.TimeoutExpired: proc.kill(); proc.wait()

from storage_fixture import create_disk,verify_disk
results=[]
work=Path(tempfile.mkdtemp(prefix='disks-',dir=BUILD))
for i,(ram,accel) in enumerate([(256,'kvm'),(512,'kvm'),(1024,'kvm'),(256,'tcg')]):
    disk=work/f'disk-{0 if i==1 else i}.img'
    if i!=1: create_disk(disk)
    else: verify_disk(disk)
    results.append(boot(ram,accel,disk,int(i==1)))
    verify_disk(disk)
    print('INDEPENDENT DISK READBACK: PASS',disk,flush=True)
print('CROSS-BOOT PERSISTENCE: PASS (second VM mounted first VM disk)',flush=True)
if not MODE: assert results[0]['managed']<results[1]['managed']<results[2]['managed']
(BUILD/'runtime-summary.json').write_text(json.dumps(results,indent=2)+'\n')
print(f'M{STAGE} QEMU: {len(results)}/{len(results)} PASS')

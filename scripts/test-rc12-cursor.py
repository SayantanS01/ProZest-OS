#!/usr/bin/env python3
import json, os, re, shutil, socket, subprocess, tempfile, time, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
STAGE=int(sys.argv[1]); assert STAGE==20
RC=STAGE-8
BUILD=Path(os.environ.get('BUILD_DIR',ROOT/'build/rc12-cursor')).resolve()
MODE=0
ISO=Path(os.environ.get('ISO_PATH',ROOT/'dist/ProZestOS-v0.0.7-rc12-cursor-x86_64.iso')).resolve()
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
    for stage in range(13,18):
        order.extend([f'M{stage}.{i} {m}: PASS' for i,m in enumerate(stage_markers[stage],1)])
        order.append(f'M{stage} COMPLETE')
    order.extend(['M18 STORAGE CONTEXT: PASS','M18 PS2 DRAIN: PASS','M19 FRAMEBUFFER PITCH: PASS','M19 SURFACE: PASS','M20 MOUSE BAT: PASS','M20 RESET ID: PASS','M20 STANDARD MOUSE: PASS','M20 MOUSE ENABLED: PASS','M20 AUX PORT DISABLED: PASS','M19 TERMINAL SELECTED','M19 COMPLETE','M20 COMPLETE','M18 SHELL READY','M18 SHELL CLEANUP: PASS','M18 COMPLETE',f'KERNEL READY - M{STAGE}','SERVICES COMPLETE'])
    assert all(lines.count(m)==1 for m in order),[m for m in order if lines.count(m)!=1]
    assert [lines.index(m) for m in order]==sorted(lines.index(m) for m in order)
    assert [l for l in lines if l.startswith('M19 VIEW ')]==['M19 VIEW HOME','M19 VIEW SYSTEM','M19 VIEW FILES','M19 VIEW HOME']
    assert lines.count('M19 RENDER: PASS')==4
    assert lines.count('M20 LEFT DOWN')==lines.count('M20 LEFT UP')==4
    assert rows(log,'M20 CLICK:',1)==[(1,),(2,),(3,)]
    points=rows(log,'M20 POINTER',2)
    assert points[0]==(140,112) and points[-1]==(140,256)
    assert all(p in points for p in [(180,112),(140,160),(140,208),(140,256)])
    assert [l[9:] for l in lines if l.startswith('M19 FILE ')]==(['seed.txt','hello.txt','note.txt'] if number(log,'STORAGE PRIOR FILE:') else ['seed.txt','note.txt'])
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

CURSOR_RGBA=bytes.fromhex('0000000000000001000000000000002c02020256030a0a4b000b0b16020404000102020000000000000000000000002a000000690000006300000064000000630000000d00000001000000010000000000000000000000000000000000000000000000000000000000000001000000010000000000000000000000000000000000000002000000010000002e000000640000001f0000002c00000060000000420000001138555509152a3f0c152a2a0c38555509557f7f0655aaaa03000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000003000000000202025d0015150cd7ffff0d91ffffa092ffffc868bbcac124404a7f0000004e0000005000000051000000520000004d0000003100000021162121177fbfbf0400000001000000000000000000000000000000000000000000000001000000010000001a0000002b000000000000000200000000000000000000000400000001050b0b590000000a89fbff8d83edf8ff5cacb8fa77e0eaff8bffffeb8effff8f92ffff3d8dffff2d7fe7ff425faeba6539667161040909710000004e000000000000000300000003000000010000000000000000000000000000000200000000000000280000005a000000000000000200000000000000000000000000000000080c0c3e0000002d84f0ffb1306061ac4426945d5557a7f44e799bff6ac4cdff7eedf7ff88fbffff8bfcffff8dffffff73d1e3ed2b4e56da63b5be9275d5e9828cffff140102030001020200000000020000000000000001000000010000000f0000006e0000001300000000000000060000000000000000000000160000001d2244440f000000497ce1f983336964998673ff928b82ffff7363effa5b52bbfb4c5c99fd51869ffe63bac2fd78e2eafd82f1ffff76d7e5ff5ca8b7fa2f545f8b0000004f0000004b0000001b010202005555550300000001000000000000006000000022000000100000006f0000002e0000000400000004000000330000004a010202000000005475d1eb433d7b78af6f59e980837dfcff7b75fffb8079ffff7d71ffff6e5fe4ff5a54bafe4e5d9cfe4f7f9efc64b7c1fc76dfe5e583fdff8687fcff6650909e5f101f257900000036000000005555550300000001000000620000000200000018000000140000000000000000000000000000003100000044000000010000005647779720438584a25b4dbf70837cffff7972fdfc7972fdff7973feff7d78ffff8079ffff7e72ffff7263edfe6055c8fc5358a8fe5578a9ff5996b0ff7be5f2f98cffffe547808cdb5599a42d00000000003f3f040000000d00000007000000000000001f0000004f000000480000004b0000003b0000003e00000001000000591212240e4c91969a3e3e816e7f78fff87a73feff7a73ffff7a73ffff7a73feff7972fdff7973fdff7c77ffff807affff7f75fffe7f70fffa5b4ac0f94e58a3ff8eefffff6ecddbff5296a6f19bffff290103030002030300000000000000001500000067000000260000002b000000200000004a0000002f00000000020707650000000174d7e7955e8dbc517c70ffe57c76ffff7a73fffe7a73ffff7a73ffff7a73ffff7a73ffff7a73ffff7972fdff7973faff7c78fdff494399ff7072e6ff81e2fffe81edfcfb65b7c5ff488290bb000000460000002000000001002a2a06000000010000000000000000000000000000005b00000013000000150205055f000000007fe9ffbc8effff427869fdcb7f79ffff7972fffd7a73ffff7a73ffff7a73ffff7a73ffff7a73ffff7972fcff807affff4b469eff625acbff88ddffff7deaf8ff81e8fdfd7ae0ecfb5296a5f1254447b2121f209b0000000100000000000000000000000100000001000000010000006e00000000000303470000001fffffff017be0f5f8356b6b6d7e6fffac817bffff7a73fffc7a73ffff7a73ffff7a73ffff7a73ffff7972fcff837cfffe534db0fc574ab6ff89cdffff63bdc5ff4b8796ff559ba5ff559cabff71ceddff8dffffff81ecfde081edffa37fe7ff2000ffff01ffffff0100000000000000000000004f0000000006090d4e0000005d70cce0648cfcffff224b43847d6eff98837dffff7972fffb7a73ffff7a73ffff7972feff7a73feff817afffe4b479dff484296ff8dccffff7deef6ff457c8bfe82edffff84f0ffe28dffffc38fffffcd82eeff7f7fe8ff227ff4ff1889ebff0d000000000000000000000000000000000000000000000001152a2a180000007c569cacd495ffffff2b5e57927967ff8a847effff7972fffb7a73ffff7871fcff7d76ffff7c75fafc4e4aa2ff5c56c0fc3d3e81a24e989afc8effffff67bccbfb4c8c99fc27454c75000000470000002a00000000000000000000000000000000000000000000000000000000000000000000000000000003010203000000002a4a86946e8efdff8d366f6ca7745df67d857fffff7871fcfb7972feff827cffff726ce5fe4b479eff635dd0f0a7a0ff4600000027000000fe335d65fd7ce2f0ff88f8ffff76d8e870172e32420308089d00000010000000003f3f3f040000000300000002000000010000000000000000000000000000000300000001050a0a60000000008dffff5c448787c56950e16f847efeff7e77fffb7e79ffff554eaffd433c8ffc736cefc5948eff2b000000000404067b010202ff000000fb1120239f52969edc81ebf6ff5398a6f276d6ebeb86f3ffcf81edff39000000000102020000000000000000000000000000000000000000000000000100000000050a0a61ffffff0189f8ffa14a8f91d96942e1558984ffff645ecdfc473994fd585cacff43558bd7ffffff0100000000ffffff01000000bd000000ff020303fc0000001c0000000d192d3564325d65f670cbdfff95ffff7b5aa5b422070b0b440004043e0000003e0000001200000000000000010000000000000001000000000205055e0000000288f6ffbd64bac5f72b275b945146a2fe524fa7fe79a1f2ff8ff1fffc6bcad5fd0d1e1b5dffffff0200070722000000f6000000ff000000dd1236360e030506000000004264b8c8f85faeb9fd203d457d0000003d00030343050a0a32040404380000000e0000000000000001000000000000000300000000030303550000001986f4ffbc88f8ffff70cddeff75b8eeff89e5ffff71d5e2ff74d8e9fc8efffffc468090e60000000004060874000000ff000000ff0103039c010303000000003e5399a6f689faffff88f6fffd7be0f0e950959e6f070f126100000017000000000001010000000000000000000000000000000001000000000f161e22000000558dffff9b8cfffffc83eefff987faffff66becaff4e8d9cff4e8e9cff7bdfecff7fe7f5ff15272975000000cc010101ff020303ff000000200000003c5194a1f289faffff7ee7fcfd7ee5fcff87f5ffff8efffff981ebfadf3e717cc35fafbf2000000000ffffff0100000000000000000000000000000002020303000000003536646dac7adfe8ff81eafffa89f7fffd5ca8b9fd6fcaddff76d6ebe7407684aa92ffffe03d6e79fe010202fb040808ff000000a7122127535aa4b1f989faffff7ee7fcfd7fe8feff83efffff71cee2fe5ca7b7ff89f9ffff5facbdff6dc7daddafffff1000ffff01ffffff0100000000000000000000000000000001010202000000001e3f727ea5579eabff7de4f1ff76d8ecfb5ba6b6ff84f2ff900000003196ffff4c6dc6d9ff000000fc000000fd223d42ca6cc4d1ff89f8ffff7ee5fbfe80e9ffff81eafeff7de3f6ff5092a0ff559ba9ff68bdceff60afc0fc7de3f9ff89f9ff5d000000007fffff040000000000000000000000000000000000007f0201020200070c0d005aa4b55a457e8ccd80eafffa58a0b0ff89f9ffbd060909553059602583efffe937656dff447b87fe81ebfbff85f3ffff7ee5fcfe80e9ffff80e9ffff80eafeff7fe8f9ff4e8e9bfe79dcf0f73b6e78ab315760ab6ec8dbfc8dffff65000000007fffff0400000000000000000000000000000000000000000000000100000003000000000000004a5094a359569dacf689faffec294b52440000004e8fffffb28efffffd87f7fffc82ebfffd7fe6fdff80eaffff80e8feff80e9ffff7fe7fdff83effffe74d3e7ff4c8c99f04888911c000000556dc7d6ff89faff7a000000007fffff0400000000000000000000000000000000000000000000000000003f0455aaaa030a11114a000000325ca9b8e584f0fffcffffff0a000000404c8a98a789f9ffff7ce3f9fb7fe7fdff7fe8fdff80e8ffff82edffff81ebffff84f0ffff7fe7fdfe82ecfffd569dacff80e9ffb1060b0c008cfcff597ee7fd83aaffff03ffffff0100000000000000000000000000000000000000000000000000000001000000011020201f010303855daabae28bffffce00030340152a3f0c060d0d4a79dcf2ca8afbffff84f0fff984f0fffd81ecfdff75d5e8ff488390ff5da8b8ff88f7ffff7bdff5fe5194a1ff88f7ffff77d3e92f0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000010203040000000036569dad9f92ffffa90000003b0e131834000101007ff2ff1482ecfdc562b4c4ff6bc4d6ff8cfffffe5faebefd64b7c8ff59a3b2fd61b0c1eb8afcffff4e8e9cff7ae0f4f78bffff217fbfbf047fffff04000000000000000000000000000000000000000000000000000000000000000000000000000000000000000100000000daffff078bffff4b0408083e00000435555555030000000091ffff0774d2e67f498793d762b3c0ff7ce2f7ff5da9b9ff87f8ffa70000005784f1ffbe70ccdbff4f909ffbb8ffff2401030300336666050000000100000000000000000000000000000000000000000000000000000000000000000000000000000000000000020000000200000000000000580000001e000000007f7f7f020000000000000000ffffff02182d33716bc3d5d65ba5b5fe89faffbd0009091a1e373e868effffed6ac1ccfd294e56990000000d0204040001020200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000100ffff010913131a0000006800000001000000020000000000ffff0133669905000000000000001500000071559aa9e983eefff8ffffff090000002d3f717ece84f0f9ff77d8e3ff437983b6000000530000000e0000000000000000000000000000000000000000000000000000000000000000000000000000000000000002000000000000004b0000001f0000000000000001000000000000000000000000000000017fffff040000004163b4c56e83eefff75bb6c80e0101010000000017345f6b985ca8b2ff5ca8b2ff3e7079e158a3b15699ffff0a0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000ffff0100010100a1ffff137fe6fc740000000000555503020303000204040076d9ee5f79deeccd85f2fffa87f5fff37ee7fc77')
CURSOR_SAMPLES=[(13, 7, (126, 114, 255)), (13, 19, (127, 231, 245)), (17, 16, (0, 0, 0))]

def exercise_desktop(sock,log,run):
    def wait(marker,count):
        deadline=time.monotonic()+12
        while time.monotonic()<deadline:
            t=log.read_text(errors='replace')
            if re.search(r': FAIL\b|FATAL CPU EXCEPTION|NESTED CPU EXCEPTION',t):raise RuntimeError('Kernel failed: '+str(log))
            if t.splitlines().count(marker)==count:return
            time.sleep(.05)
        raise RuntimeError('Desktop timeout: '+marker+' '+str(log))
    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as conn:
        conn.settimeout(8);conn.connect(str(sock))
        with conn.makefile('rwb') as f:
            assert 'QMP' in json.loads(f.readline())
            def qmp(req):
                f.write(json.dumps(req).encode()+b'\n');f.flush()
                while True:
                    line=f.readline();assert line,'QMP disconnected'
                    r=json.loads(line)
                    if 'error' in r:raise RuntimeError(r['error'])
                    if 'return' in r:return r['return']
            qmp({'execute':'qmp_capabilities'})
            def key(k):
                qmp({'execute':'send-key','arguments':{'keys':[{'type':'qcode','data':k}],'hold-time':80}})
                time.sleep(.15)
            def pointer(x,y):
                deadline=time.monotonic()+12
                while time.monotonic()<deadline:
                    t=log.read_text(errors='replace')
                    if re.search(r': FAIL\b|FATAL CPU EXCEPTION|NESTED CPU EXCEPTION',t):raise RuntimeError('Kernel failed: '+str(log))
                    p=rows(t[:t.rfind('\n')+1],'M20 POINTER',2)
                    if p and p[-1]==(x,y):return
                    time.sleep(.05)
                raise RuntimeError(f'Pointer timeout: expected {(x,y)}; got {p[-10:]} in {log}')
            def move(dx,dy,x,y):
                events=[{'type':'rel','data':{'axis':a,'value':d}} for a,d in [('x',dx),('y',dy)] if d]
                qmp({'execute':'input-send-event','arguments':{'events':events}})
                pointer(x,y)
            def click(n):
                for down,marker in [(True,'M20 LEFT DOWN'),(False,'M20 LEFT UP')]:
                    qmp({'execute':'input-send-event','arguments':{'events':[{'type':'btn','data':{'down':down,'button':'left'}}]}})
                    wait(marker,n)
            def screen(name,color,x,y):
                path=run/(name+'.ppm')
                for attempt in range(15):
                    qmp({'execute':'screendump','arguments':{'filename':str(path)}})
                    raw=path.read_bytes();m=re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s',raw)
                    assert m,'Expected P6 screenshot'
                    w,h=map(int,m.groups());data=raw[m.end():]
                    assert w>=640 and h>=480 and len(data)==w*h*3,(w,h,len(data))
                    pixel=lambda x,y:tuple(data[3*(y*w+x):3*(y*w+x)+3])
                    if pixel(0,0)==(22,32,50) and pixel(200,82)==color and all(pixel(x+sx-6,y+sy-4)==rgb for sx,sy,rgb in CURSOR_SAMPLES):return pixel
                    time.sleep(.15)
                raise AssertionError((name,pixel(0,0),pixel(200,82),pixel(x,y),pixel(x+1,y+4),color))
            wait('M19 VIEW HOME',1);pointer(140,112);before=screen('mouse-home',(45,212,191),140,112)
            move(40,0,180,112);pixel=screen('mouse-moved',(45,212,191),180,112)
            # Full sprite alpha composition against the independently restored background.
            for sy in range(32):
                for sx in range(32):
                    x,y=134+sx,108+sy
                    bg=((30,43,63) if y<126 else (22,32,50))
                    assert pixel(x,y)==bg,(x,y,pixel(x,y),bg)
                    rr,gg,bb,alpha=CURSOR_RGBA[4*(sy*32+sx):4*(sy*32+sx)+4]
                    expected=tuple((v*alpha+b*(255-alpha)+127)//255 for v,b in zip((rr,gg,bb),bg))
                    assert before(x,y)==expected,(x,y,before(x,y),expected)
            click(1);assert not rows(log.read_text(errors='replace'),'M20 CLICK:',1),'Click outside panel selected a view'
            move(-40,48,140,160);click(2);wait('M19 VIEW SYSTEM',1);screen('mouse-system',(96,165,250),140,160)
            move(0,48,140,208);click(3);wait('M19 VIEW FILES',1);screen('mouse-files',(251,191,36),140,208)
            key('1');wait('M19 VIEW HOME',2);screen('mouse-home-return',(45,212,191),140,208)
            move(0,48,140,256);click(4);wait('M19 TERMINAL SELECTED',1)
    print('CUSTOM CURSOR RGBA, RESTORATION AND NAVIGATION: PASS (5 screenshots)',flush=True)


def exercise_shell(sock,log,prior):
    def wait(predicate):
        deadline=time.monotonic()+12
        while time.monotonic()<deadline:
            t=log.read_text(errors='replace')
            if re.search(r': FAIL\b|FATAL CPU EXCEPTION|NESTED CPU EXCEPTION',t):raise RuntimeError('Kernel failed: '+str(log))
            if predicate(t):return t
            time.sleep(.05)
        raise RuntimeError('Shell timeout: '+str(log))
    wait(lambda t:'SHELL PROMPT\n' in t)
    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as conn:
        conn.settimeout(8);conn.connect(str(sock))
        with conn.makefile('rwb') as f:
            assert 'QMP' in json.loads(f.readline())
            def qmp(req):
                f.write(json.dumps(req).encode()+b'\n');f.flush()
                while True:
                    line=f.readline();assert line,'QMP disconnected'
                    r=json.loads(line)
                    if 'error' in r:raise RuntimeError(r['error'])
                    if 'return' in r:return r['return']
            qmp({'execute':'qmp_capabilities'})
            def keys(s):
                for c in s:
                    q={' ':'spc','.':'dot','-':'minus','/':'slash','\b':'backspace','\n':'ret'}.get(c,c)
                    combo=[{'type':'qcode','data':q.lower()}]
                    if c.isupper():combo.insert(0,{'type':'qcode','data':'shift'})
                    qmp({'execute':'send-key','arguments':{'keys':combo,'hold-time':30}})
                    time.sleep(.065)
            def command(typed,actual,expected=None,exit=False):
                old=log.read_text(errors='replace');count=old.count('SHELL PROMPT\n')
                keys(typed+'\n')
                t=wait(lambda t:('M18 COMPLETE\n' in t) if exit else t.count('SHELL PROMPT\n')==count+1)
                delta=t[len(old):];prefix='SHELL CMD '+actual+'\n'
                assert delta.startswith(prefix),(actual,delta)
                response=delta[len(prefix):].split('SHELL PROMPT\n',1)[0]
                if exit:assert response.startswith(expected),(actual,response)
                elif expected is not None:assert response==expected,(actual,response)
                else:assert re.fullmatch(r'managed pages: [0-9]+ free pages: [0-9]+\n',response),response
            command('help','help','help version mem clear echo ls cat touch write rm exit\n')
            command('versiox\bn','version','ProZest OS v0.0.7-rc12-cursor\n')
            command('echo Hello world','echo Hello world','Hello world\n')
            command('ls','ls','seed.txt\nhello.txt\nnote.txt\n' if prior else 'seed.txt\nnote.txt\n')
            command('cat seed.txt','cat seed.txt','ProZest storage seed\n')
            command('cat hello.txt','cat hello.txt','persistent\n' if prior else 'error: file not found\n')
            command('touch scratch.txt','touch scratch.txt','ok\n')
            command('write scratch.txt sample','write scratch.txt sample','ok\n')
            command('cat scratch.txt','cat scratch.txt','sample\n')
            command('rm scratch.txt','rm scratch.txt','ok\n')
            command('cat scratch.txt','cat scratch.txt','error: file not found\n')
            command('write hello.txt persistent','write hello.txt persistent','ok\n')
            command('touch hello.txt','touch hello.txt','ok\n')
            command('cat hello.txt','cat hello.txt','persistent\n')
            command('rm seed.txt','rm seed.txt','error: reserved file\n')
            command('write ../bad x','write ../bad x','error: invalid argument\n')
            command('badcmd','badcmd','error: unknown command\n')
            managed=number(log.read_text(errors='replace'),'PMM MANAGED PAGES:')
            command('mem','mem',f'managed pages: {managed} free pages: {managed-1}\n')
            command('clear','clear','screen cleared\n')
            command('exit','exit','shell halted\n',exit=True)
    print('M18 KEYBOARD SHELL TRANSCRIPT: PASS (20 commands including backspace and file operations)',flush=True)

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
            injected=False; shell_tested=False; desktop_tested=False
            while time.monotonic()<deadline:
                if proc.poll() is not None: raise RuntimeError(f'QEMU exited: {proc.returncode}')
                if sock.exists() and log.exists():
                    current=log.read_text(errors='replace')
                    if STAGE>=14 and not injected and 'M14 KEYBOARD INPUT READY\n' in current:
                        send_a(sock);injected=True
                    if not desktop_tested and 'M19 VIEW HOME\n' in current:
                        exercise_desktop(sock,log,run);desktop_tested=True
                    if not shell_tested and 'M18 SHELL READY\n' in current:
                        exercise_shell(sock,log,prior);shell_tested=True
                    if 'SERVICES COMPLETE\n' in current:break
                if log.exists() and re.search(r': FAIL\b|FATAL CPU EXCEPTION|NESTED CPU EXCEPTION',log.read_text(errors='replace')): raise RuntimeError('Kernel failed; inspect '+str(log))
                time.sleep(.2)
            else: raise RuntimeError('Boot timeout; inspect '+str(log))
            assert shell_tested and desktop_tested
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

from storage_fixture import create_disk
from shell_fixture import verify_shell_disk
results=[]
work=Path(tempfile.mkdtemp(prefix='disks-',dir=BUILD))
for i,(ram,accel) in enumerate([(256,'kvm'),(512,'kvm'),(1024,'kvm'),(256,'tcg')]):
    disk=work/f'disk-{0 if i==1 else i}.img'
    if i!=1: create_disk(disk)
    else: verify_shell_disk(disk)
    results.append(boot(ram,accel,disk,int(i==1)))
    verify_shell_disk(disk)
    print('INDEPENDENT DISK READBACK: PASS',disk,flush=True)
print('CROSS-BOOT SHELL PERSISTENCE: PASS (second VM read first VM user file)',flush=True)
if not MODE: assert results[0]['managed']<results[1]['managed']<results[2]['managed']
(BUILD/'runtime-summary.json').write_text(json.dumps(results,indent=2)+'\n')
print(f'M{STAGE} QEMU: {len(results)}/{len(results)} PASS')

import re, struct, subprocess, sys
from pathlib import Path
stage=int(sys.argv[1]); assert stage==18
elf=sys.argv[2]
def run(*args): return subprocess.check_output(args,text=True)
header=run('readelf','-hW',elf)
# The linker may place kernel_main after other .text functions. Validate
# the ELF entry against the actual symbol and an executable PT_LOAD.
# Do not hardcode an address from a previous kernel build.
raw=Path(elf).read_bytes()
assert len(raw)>=64 and raw[:7]==b'\x7fELF\x02\x01\x01', 'Expected little-endian ELF64'
kind,machine,version,entry,phoff,_,_,ehsize,phentsize,phnum,_,_,_=struct.unpack_from('<HHIQQQIHHHHHH',raw,16)
assert (kind,machine,version,ehsize,phentsize)==(2,62,1,64,56), header
assert phnum>0 and phoff>=ehsize and phoff+phnum*phentsize<=len(raw), 'Invalid program header table'
assert entry>=0xffffffff80000000, f'Entry outside kernel higher half: {entry:#x}'
main=[]
for line in run('nm','-P','--defined-only',elf).splitlines():
    parts=line.split()
    if parts and parts[0]=='kernel_main':
        assert len(parts)>=3 and parts[1]=='T', f'kernel_main is not a global text symbol: {line}'
        main.append(int(parts[2],16))
assert main==[entry], f'ELF entry {entry:#x} does not match kernel_main {main}'
executable=False
for i in range(phnum):
    typ,flags,offset,va,_,filesz,memsz,_=struct.unpack_from('<IIQQQQQQ',raw,phoff+i*phentsize)
    if typ==1:
        assert filesz<=memsz and offset+filesz<=len(raw), 'Invalid PT_LOAD bounds'
        if flags&1 and va<=entry<va+filesz:
            executable=True
assert executable, f'Entry {entry:#x} not in a file-backed executable PT_LOAD'
print(f'SERVICES ENTRY AUDIT: PASS (kernel_main={entry:#018x}, executable PT_LOAD)')
assert not run('nm','-u',elf).strip(), 'Undefined symbols'
sections=run('readelf','-SW',elf)
assert '.limine_requests' in sections and '.bss' in sections
symbols=run('nm','--defined-only',elf)
for name in ('pmm_init','pmm_bitmap_state_init','pmm_get_stats','pmm_initial_state_check','pmm_alloc_page','pmm_bitmap_alloc_page','pmm_live_state_check','pmm_free_page','pmm_bitmap_free_page','pmm_bitmap_get_stats'):
    assert re.search(r'\b'+name+r'$',symbols,re.M), name
for name in ('pmm_alloc_contiguous','pmm_free_contiguous','pmm_bitmap_alloc_contiguous','pmm_bitmap_free_contiguous'):
    assert re.search(r'\b'+name+r'$',symbols,re.M), name
assert not re.search(r'\bstress_(?:run|slots|random|event)\b',symbols), 'Stress code in normal kernel'
for name in ('vmm_clone','vmm_translate','vmm_map_page','vmm_unmap_page','vmm_protect_page','vmm_destroy_empty'):
    assert re.search(r'\b'+name+r'$',symbols,re.M),name
asm=run('objdump','-d',elf)
assert re.search(r'\binvlpg\b',asm), 'Missing TLB invalidation'
assert len(re.findall(r'\bsti\s*$',asm,re.M))==1, 'Expected one controlled STI instruction'
sti_pos=re.search(r'\bsti\s*$',asm,re.M).start()
funcs=re.findall(r'^\s*[0-9a-f]+ <([^>]+)>:$',asm[:sti_pos],re.M)
assert funcs and funcs[-1].split('.')[0]=='irq_until', 'STI outside IRQ wait path'
assert re.search(r'\biretq\b',asm), 'Missing IRQ return'
for name in ('irq_dispatch','irq_register_probe','irq_pic0','irq_io0','irq_lapic_timer','irq_acpi_discover','irq_madt_parse'):
    assert re.search(r'\b'+name+r'$',symbols,re.M),name
for name in ('kmalloc','kfree','kcalloc','krealloc','kheap_init','kheap_alloc','kheap_free','kheap_check'):
    assert re.search(r'\b'+name+r'$',symbols,re.M),name
if stage>=14:
    for name in ('pci_config_address','key_decode'):
        assert re.search(r'\b'+name+r'$',symbols,re.M),name
if stage>=15:
    for name in ('sched_dispatch','sched_timer_entry','sched_yield_entry','sched_frame_init','sched_pick'):
        assert re.search(r'\b'+name+r'$',symbols,re.M),name
    assert re.search(r'\bint\s+\$0x43',asm), 'Missing scheduler software IRQ'
for name in ('user_enter','user_dispatch','user_syscall_entry','user_pf_entry','user_gp_entry','user_image_start','user_image_end','user_write_range','user_fault_expected'):
    assert re.search(r'\b'+name+r'$',symbols,re.M),name
for name in ('pzfs_mount','pzfs_read','pzfs_put','pzfs_remove','pzfs_crc'):
    assert re.search(r'\b'+name+r'$',symbols,re.M),name
for name in ('shell_exec','shell_feed','shell_key','console_shell_clear','console_shell_backspace'):
    assert re.search(r'\b'+name+r'$',symbols,re.M),name
print(f'M{stage} ELF AUDIT: PASS (entry, PMM, VMM, IRQ and stage APIs)')

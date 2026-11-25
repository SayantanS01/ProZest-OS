import re, subprocess, sys
elf=sys.argv[1]
def run(*args): return subprocess.check_output(args,text=True)
header=run('readelf','-hW',elf)
assert re.search(r'Entry point address:\s+0xffffffff80001000\b',header), header
assert not run('nm','-u',elf).strip(), 'Undefined symbols'
sections=run('readelf','-SW',elf)
assert '.limine_requests' in sections and '.bss' in sections
symbols=run('nm','--defined-only',elf)
for name in ('pmm_init','pmm_bitmap_state_init','pmm_get_stats','pmm_initial_state_check','pmm_alloc_page','pmm_bitmap_alloc_page','pmm_live_state_check','pmm_free_page','pmm_bitmap_free_page','pmm_bitmap_get_stats'):
    assert re.search(r'\b'+name+r'$',symbols,re.M), name
for name in ('pmm_alloc_contiguous','pmm_free_contiguous','pmm_bitmap_alloc_contiguous','pmm_bitmap_free_contiguous'):
    assert re.search(r'\b'+name+r'$',symbols,re.M), name
assert not re.search(r'\bstress_(?:run|slots|random|event)\b',symbols), 'Stress code in normal kernel'
asm=run('objdump','-d',elf)
assert not re.search(r'\bsti\s*$',asm,re.M), 'Unexpected STI'
print('RC2 ELF AUDIT: PASS (entry, sections, symbols, no STI, single and contiguous allocation/free present)')

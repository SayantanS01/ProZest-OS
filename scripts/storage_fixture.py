"""Independent PZFS fixture encoder/decoder; never open a host block device."""
import struct,zlib
from pathlib import Path
SEED=b'ProZest storage seed\n'
NOTE=b'ProZest persistent file\n'
SIZE=2048*512

def record(name,data):
    return struct.pack('<24sII',name,len(data),zlib.crc32(data))

def create_disk(path):
    path=Path(path)
    assert not path.exists() and not path.is_symlink()
    raw=bytearray(SIZE)
    raw[:16]=struct.pack('<8sII',b'PZFS001!',1,2048)
    raw[16:48]=record(b'seed.txt',SEED)
    raw[508:512]=struct.pack('<I',zlib.crc32(raw[:508]))
    raw[512:512+len(SEED)]=SEED
    with path.open('xb') as f:f.write(raw)

def verify_disk(path):
    path=Path(path);assert path.is_file() and not path.is_symlink()
    raw=path.read_bytes();assert len(raw)==SIZE
    assert raw[:16]==struct.pack('<8sII',b'PZFS001!',1,2048)
    assert struct.unpack_from('<I',raw,508)[0]==zlib.crc32(raw[:508])
    assert not any(raw[272:508]) and not any(raw[9*512:])
    found={}
    for i in range(8):
        e=raw[16+32*i:48+32*i]
        if not any(e):continue
        name,n,crc=struct.unpack('<24sII',e);name=name.rstrip(b'\0')
        assert name not in found and n<=512
        data=raw[(i+1)*512:(i+2)*512]
        assert zlib.crc32(data[:n])==crc and not any(data[n:])
        found[name]=data[:n]
    assert found=={b'seed.txt':SEED,b'note.txt':NOTE},found

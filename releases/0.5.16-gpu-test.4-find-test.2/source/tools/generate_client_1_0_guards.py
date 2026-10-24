"""Generate bounded live-code guards for the statically reviewed client 25966634."""
import hashlib
import sys
from pathlib import Path
import pefile

EXPECTED='4d6d43ea4a91e71c82d60b045dde932179c99e32e40657efe374688b2933589a'
# Each group covers reviewed instruction boundaries, calls and field accesses.
# Full executable identity is checked separately before selecting this profile.
GROUPS={
 'Camera':[(0x2b26a0,64),(0x9ba5a0,32),(0x9b5490,64),(0x9b2880,32),(0x3b4b20,32)],
 'Renderer':[(0xfd13d0,15),(0xfd2023,44),(0xfdd960,32),(0xfe0973,128),(0xfe1714,16)],
 'Markers':[(0x3179c0,64),(0x318015,32),(0x3b17d0,64),(0x9b2a20,5),(0x30e650,128),(0x30e6d0,128)],
 'Clock':[(0x2b2e80,48),(0xe88a60,21),(0xe7110e,10),(0xea77d2,4)],
 'Weather':[(0x2fd4e0,64),(0x2fd82e,47),(0xa9b420,32),(0xa9b95b,40)],
 'Fog':[(0x306d20,32),(0x306d82,14),(0x306dc0,41),(0x39b540,24),(0x39bfb4,13),(0x39c110,8),(0x13b6edc,4)],
 'World':[(0x2feb20,64),(0x302ee7,64),(0x2fd020,96),(0x3107f0,32)],
 'Death':[(0x3107f0,512)],
 'Visibility':[(0x414800,64),(0x414854,96),(0x414a62,22)],
}
def main():
    source=Path(sys.argv[1]).read_bytes()
    assert hashlib.sha256(source).hexdigest()==EXPECTED,'unsupported executable'
    pe=pefile.PE(data=source);image=pe.get_memory_mapped_image()
    lines=['// Generated instruction guards for Steam build 25966634; see docs/CLIENT-1.0.md.']
    for name,spans in GROUPS.items():
        lines.append('bool Match'+name+'10() {')
        for i,(rva,size) in enumerate(spans):
            values=','.join('0x%02x'%x for x in image[rva:rva+size])
            lines.append('    const std::uint8_t b%d[] = {%s};'%(i,values))
        lines.append('    return '+' &&\n        '.join('BytesMatchRva(0x%x,b%d,sizeof(b%d))'%(rva,i,i) for i,(rva,_) in enumerate(spans))+';\n}')
    Path(__file__).resolve().parents[1].joinpath('src/client_1_0_guards.h').write_text('\n'.join(lines)+'\n')
if __name__=='__main__':main()

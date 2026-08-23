"""Tiny read-only NBT parser (network style: unnamed root) + capture decoders."""
import io, json, struct, sys

TAG_END=0; TAG_BYTE=1; TAG_SHORT=2; TAG_INT=3; TAG_LONG=4
TAG_FLOAT=5; TAG_DOUBLE=6; TAG_BYTE_ARRAY=7; TAG_STRING=8
TAG_LIST=9; TAG_COMPOUND=10; TAG_INT_ARRAY=11; TAG_LONG_ARRAY=12

class R:
    def __init__(self, b): self.b=b; self.p=0
    def take(self,n):
        if self.p+n>len(self.b): raise EOFError("nbt truncated")
        v=self.b[self.p:self.p+n]; self.p+=n; return v
    def u8(self): return self.take(1)[0]
    def i8(self): return struct.unpack(">b",self.take(1))[0]
    def i16(self): return struct.unpack(">h",self.take(2))[0]
    def i32(self): return struct.unpack(">i",self.take(4))[0]
    def i64(self): return struct.unpack(">q",self.take(8))[0]
    def f32(self): return struct.unpack(">f",self.take(4))[0]
    def f64(self): return struct.unpack(">d",self.take(8))[0]
    def s(self):
        n=self.i16(); return self.take(n).decode("utf-8","replace")

def read_payload(r, t):
    if t==TAG_BYTE: return r.i8()
    if t==TAG_SHORT: return r.i16()
    if t==TAG_INT: return r.i32()
    if t==TAG_LONG: return r.i64()
    if t==TAG_FLOAT: return round(r.f32(),4)
    if t==TAG_DOUBLE: return round(r.f64(),6)
    if t==TAG_BYTE_ARRAY: return r.take(r.i32())
    if t==TAG_STRING: return r.s()
    if t==TAG_LIST:
        et=r.u8(); n=r.i32(); items=[]
        for _ in range(min(n,4096)): items.append(read_payload(r,et))
        return {"__list__":items,"elem":et}
    if t==TAG_COMPOUND:
        d={}
        while True:
            et=r.u8()
            if et==TAG_END: return d
            name=r.s(); d[name]=read_payload(r,et)
    if t==TAG_INT_ARRAY: return r.take(4*r.i32())
    if t==TAG_LONG_ARRAY: return r.take(8*r.i32())
    raise ValueError(f"bad tag {t}")

def read_named(r):
    t=r.u8()
    if t==TAG_END: return None,None
    name=r.s()
    return name, read_payload(r,t)

def skip_payload(r, t):
    if t==TAG_BYTE: r.take(1)
    elif t==TAG_SHORT: r.take(2)
    elif t==TAG_INT: r.take(4)
    elif t==TAG_LONG: r.take(8)
    elif t in (TAG_FLOAT,TAG_DOUBLE): r.take(4 if t==TAG_FLOAT else 8)
    elif t==TAG_BYTE_ARRAY: r.take(r.i32())
    elif t==TAG_STRING: r.take(max(0,r.i16()))
    elif t==TAG_LIST:
        et=r.u8(); n=r.i32()
        for _ in range(n): skip_payload(r,et)
    elif t==TAG_COMPOUND:
        while True:
            et=r.u8()
            if et==TAG_END: break
            r.take(max(0,r.i16())); skip_payload(r,et)
    elif t==TAG_INT_ARRAY: r.take(4*r.i32())
    elif t==TAG_LONG_ARRAY: r.take(8*r.i32())
    else: raise ValueError("bad tag")

def parse_anon_nbt(data, offset=0):
    """network NBT: root tag has NO name; returns (value, new_offset)"""
    r=R(data); r.p=offset
    t=r.u8()
    assert t==TAG_COMPOUND, f"root not compound: {t}"
    val=read_payload(r,t)
    return val, r.p

# ---------------- decoders ----------------
def varint(b, p):
    res=0; sh=0
    while True:
        x=b[p]; p+=1
        res |= (x&0x7f)<<sh
        if not x&0x80:
            if res & (1<<31): res -= 1<<32
            return res,p
        sh+=7

def string(b,p):
    n,p=varint(b,p)
    return b[p:p+n].decode(), p+n

def dec_join_game(fn="/tmp/opencode/captures/play_join_game.bin"):
    b=open(fn,'rb').read(); p=0
    out={}
    out['entityId']=struct.unpack_from(">i",b,p)[0]; p+=4
    out['hardcore']=b[p]; p+=1
    n,p=varint(b,p)
    worlds=[]
    for _ in range(n):
        w,p=string(b,p); worlds.append(w)
    out['worlds']=worlds
    out['maxPlayers'],p=varint(b,p)
    out['viewDist'],p=varint(b,p)
    out['simDist'],p=varint(b,p)
    out['reducedDebug']=b[p]; p+=1
    out['respawnScreen']=b[p]; p+=1
    out['limitedCrafting']=b[p]; p+=1
    out['spawn.dimensionTypeVarint'],p=varint(b,p)
    out['spawn.name'],p=string(b,p)
    out['spawn.hashedSeed']=struct.unpack_from(">q",b,p)[0]; p+=8
    out['spawn.gamemode']=struct.unpack_from(">b",b,p)[0]; p+=1
    out['spawn.prevGamemode']=b[p]; p+=1
    out['spawn.isDebug']=b[p]; p+=1
    out['spawn.isFlat']=b[p]; p+=1
    has=b[p]; p+=1
    if has:
        dn,p=string(b,p)
        pos=struct.unpack_from(">ddd",b,p); p+=24
        out['death']=(dn,pos)
    out['portalCooldown'],p=varint(b,p)
    out['seaLevel'],p=varint(b,p)
    out['enforcesSecureChat']=b[p]; p+=1
    out['_consumed']=p; out['_total']=len(b)
    print(json.dumps(out,indent=1))
    return out

def dec_chunk(fn):
    b=open(fn,'rb').read(); p=0
    x=struct.unpack_from(">i",b,p)[0]; p+=4
    z=struct.unpack_from(">i",b,p)[0]; p+=4
    hm,p = parse_anon_nbt(b,p)
    size,p = varint(b,p)
    data = b[p:p+size]; p+=size
    nbe,p = varint(b,p)
    bes=[]
    for _ in range(nbe):
        bb=b[p]; p+=1   # packed x|z
        y=struct.unpack_from(">h",b,p)[0]; p+=2
        typ,p=varint(b,p)
        # anonOptionalNbt: bool present? prismarine says optional nbt w/ presence implied?
        has=b[p]; p+=1
        nb=None
        if has: nb,p = parse_anon_nbt(b,p)
        bes.append((bb,y,typ,nb))
    def rd_longs():
        nonlocal p
        n,p = varint(b,p)
        L=[]
        for _ in range(n):
            L.append(struct.unpack_from(">q",b,p)[0]); p+=8
        return L
    sky_m = rd_longs(); block_m = rd_longs(); esk_m = rd_longs(); eblock_m = rd_longs()
    def light_arrays():
        nonlocal p
        n,p=varint(b,p); outs=[]
        for _ in range(n):
            m,p=varint(b,p)
            arr=list(b[p:p+m]); p+=m
            outs.append(m)
        return outs,p
    skylens,p=light_arrays(); blocklens,p=light_arrays()
    consumed=p
    print(f"chunk ({x},{z}) heightmaps_keys={list(hm.keys())} dataSize={size} blockEntities={nbe}")
    print(f"  masks: sky={[bin(v) for v in sky_m]} emptySky={[bin(v) for v in esk_m]}")
    print(f"  skyArrays={skylens} blockArrays={blocklens}; consumed={consumed}/{len(b)}")
    analyze_sections(data)

def unpack_long_array(la, bits, count=None):
    vals=[]
    per=64//bits
    mask=(1<<bits)-1
    for L in la:
        for i in range(per):
            if count is not None and len(vals)>=count: return vals
            vals.append((L >> (i*bits)) & mask)
    return vals

def analyze_sections(data):
    p=0
    for si in range(24):
        if p>=len(data): print(f"  [sections exhausted at {si}]"); return
        bc=struct.unpack_from(">h",data,p)[0]; p+=2
        bits=data[p]; p+=1
        desc=f"sec{si}: blocks={bc} bits={bits}"
        if bits==0:
            bid,p=varint(data,p)
            desc+=f" single(block={bid})"
        elif bits<=8:
            n,p=varint(data,p); pal=[]
            for _ in range(n):
                v,p=varint(data,p); pal.append(v)
            nl,p=varint(data,p)
            la=[struct.unpack_from(">q",data,p+8*i)[0] for i in range(nl)]
            p+=8*nl
            entries=unpack_long_array(la,bits,4096)
            desc+=f" palette={pal[:8]}{'...' if n>8 else ''} longs={nl} uniq={sorted(set(entries))[:10]}"
        else:
            nl,p=varint(data,p)
            p+=8*nl
            desc+=f" DIRECT longs={nl}"
        bbits=data[p]; p+=1
        if bbits==0:
            bid,p=varint(data,p)
            desc+=f" | biome single={bid}"
        else:
            n,p=varint(data,p); pal=[]
            for _ in range(n):
                v,p=varint(data,p); pal.append(v)
            nl,p=varint(data,p); p+=8*nl
            desc+=f" | biomePal={pal} longs={nl}"
        print(" ",desc)

def dec_registries():
    import glob, os
    for fn in sorted(glob.glob("/tmp/opencode/captures/registry_*.bin")):
        b=open(fn,'rb').read(); p=0
        key,p=string(b,p)
        n,p=varint(b,p)
        keys=[]
        first_val=None
        for i in range(n):
            ek,p=string(b,p)
            has=b[p]; p+=1
            if has:
                if first_val is None:
                    first_val=(ek,p)
                _,p = parse_anon_nbt(b,p)
            keys.append(ek)
        print(f"{key}: {n} -> {keys if n<=12 else keys[:12]+['...']}  consumed={p}/{len(b)}")

if __name__=="__main__":
    what=sys.argv[1] if len(sys.argv)>1 else "all"
    if what in ("all","join"): print("=== JOIN GAME ==="); dec_join_game()
    if what in ("all","chunk"): print("=== CHUNK 0 ==="); dec_chunk("/tmp/opencode/captures/play_chunk_0.bin"); print("=== CHUNK 3 ==="); dec_chunk("/tmp/opencode/captures/play_chunk_3.bin")
    if what in ("all","regs"): print("=== REGISTRIES ==="); dec_registries()

import sys, time
import numpy as np
sys.path.insert(0, '.')
from util.ics2115_remote import ICS2115Remote, WIDTH_LOWER8, WIDTH_UPPER8, WIDTH_16, Voice

r = ICS2115Remote.open("pgm", reset="low")
d=time.time()+10
while True:
    try: r.ping(); break
    except Exception:
        if time.time()>d: raise
        time.sleep(0.5)
r.open_audio()
r.write_reg(0,0x4D,0x0D,width=WIDTH_LOWER8)   # chip run gate on

V=4
def voice(fmt, addr, fc=4096, loop=True):
    v=Voice()
    v.osc_conf=(0x08 if loop else 0x00)|(fmt&3)
    v.osc_fc=fc; v.vol_acc=0xFFFF; v.vol_start=0xFF; v.vol_end=0xFF; v.pan=0x80
    v.set_start_wave_addr(addr); v.set_end_wave_addr(addr+0x8000); v.set_acc_wave_addr(addr)
    v.osc_ctl=0x00
    return v

def cap(fmt, addr, fc=4096, n=16384):
    r.write_reg(V,0x10,0x0F,width=WIDTH_UPPER8)   # keyoff first
    r.write_voice(V, voice(fmt,addr,fc))
    time.sleep(0.05)
    blks=r.audio.capture_blocks((n+127)//128, timeout=30.0)
    L=np.array([s[0] for b in blks for s in b.samples], dtype=np.float64)
    r.write_reg(V,0x10,0x0F,width=WIDTH_UPPER8)
    return L[:n]

def stats(L):
    L=L-L.mean()
    rms=float(np.sqrt(np.mean(L*L)))
    # distinct raw values (8 vs 16 bit hint)
    distinct=len(np.unique(np.round(L+L.mean())))
    # autocorrelation peak (noise -> low off-zero autocorr)
    if rms>1:
        ac=np.correlate(L,L,'full')[len(L)-1:]; ac=ac/ac[0]
        peak=float(np.max(np.abs(ac[8:len(L)//2])))   # strongest non-trivial lag
        peaklag=int(np.argmax(np.abs(ac[8:len(L)//2]))+8)
    else:
        peak,peaklag=0.0,0
    zcr=float(np.mean(np.abs(np.diff(np.sign(L)))>0))
    return dict(rms=round(rms,1),distinct=distinct,ac_peak=round(peak,3),ac_lag=peaklag,zcr=round(zcr,4))

A=0x2D640; B=0x80000   # two different ROM regions
print("=== (1) GENERATOR vs ROM-DECODE ===", flush=True)
for fmt,name in ((0,'lin8'),(3,'fmt3')):
    sa=stats(cap(fmt,A)); sb=stats(cap(fmt,B))
    print(f"  {name}: addrA {sa}  addrB {sb}", flush=True)
print("  -> if fmt3 A==B but lin8 A!=B: generator ignores ROM", flush=True)

print("=== (2) OscFC DEPENDENCE (fmt3) ===", flush=True)
for fc in (1024,4096,16384,32768):
    print(f"  fc={fc}: {stats(cap(3,A,fc=fc))}", flush=True)

print("=== (3) DETERMINISM (fmt3, two key-ons) ===", flush=True)
c1=cap(3,A); c2=cap(3,A)
n=min(len(c1),len(c2))
cc=float(np.corrcoef(c1[:n],c2[:n])[0,1])
print(f"  cross-corr of two captures = {cc:.3f} (1=deterministic same, ~0=random/free-running)", flush=True)

print("=== (4) AMPLITUDE DISTRIBUTION (fmt3) ===", flush=True)
L=cap(3,A,n=32768); Lc=L-L.mean()
hist,edges=np.histogram(Lc,bins=16)
print(f"  range [{L.min():.0f},{L.max():.0f}] mean={L.mean():.0f} std={L.std():.0f}", flush=True)
print(f"  16-bin hist: {hist.tolist()}", flush=True)
# kurtosis: uniform ~1.8, gaussian 3.0
k=float(np.mean((Lc/Lc.std())**4))
print(f"  kurtosis={k:.2f} (uniform~1.8, gaussian~3.0)", flush=True)

print("=== (5) PERIOD / LFSR LENGTH (long fmt3 capture, autocorr) ===", flush=True)
L=cap(3,A,n=65536); Lc=L-L.mean()
ac=np.correlate(Lc,Lc,'full')[len(Lc)-1:]; ac=ac/ac[0]
# find strongest peak beyond lag 64
seg=np.abs(ac[64:len(Lc)//2]); pl=int(np.argmax(seg)+64); pv=float(seg.max())
print(f"  strongest autocorr lag={pl} val={pv:.3f} (a clear peak => periodic; lag*resample = raw period ~ 2^N-1)", flush=True)
r.close()
print("DONE", flush=True)

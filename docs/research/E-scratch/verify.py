import math
# dbx attack times: step to D dB above threshold, time for GR to reach 63.2% of final, mean-square one-pole tau
for tau in (0.027, 0.0347):
    ts=[]
    for D in (10,20,30):
        t = -tau*math.log(1-10**(-0.0368*D)); ts.append(round(t*1000,1))
    print('tau',tau*1000,'ms -> attack(63%) ms for 10/20/30 dB:',ts,' release rate dB/s:', round(10*math.log10(math.e)/tau,1))
# simulate the mean-square detector directly to double-check (fs=48k), threshold -20 dBFS, ratio 4 (S=0.75)
fs=48000.0
def sim(D,tau=0.030):
    a=math.exp(-1/(tau*fs)); ms=1e-12; T=-20.0; S=0.75
    amp_rms = 10**((T+D)/20)  # rms of sine
    A = amp_rms*math.sqrt(2)
    final = S*D; n=0
    while True:
        x = A*math.sin(2*math.pi*1000*n/fs)
        ms = a*ms+(1-a)*x*x
        L = 10*math.log10(ms)
        gr = S*max(0,L-T)
        if gr >= 0.632*final and n>10: return n/fs*1000
        n+=1
print('sim tau=30ms sine 1k: attack ms', [round(sim(D),1) for D in (10,20,30)])
# naive one-sample-delay feedback vs ZDF for 1176 fastest attack
def fbsim(tau, S, fs, zdf, n=400):
    a=math.exp(-1/(tau*fs)); r=0.0; T=-20.0; x=0.0  # x in dB (steady 20 dB over threshold)
    x=T+20.0; hist=[]; yprev = x
    for i in range(n):
        if zdf:
            rn=(a*r+(1-a)*S*(x-T))/(1+(1-a)*S)
            if x-rn<=T: rn=a*r
            r=rn
        else:
            target=S*max(0.0,yprev-T)
            al = a  # attack branch assumed (worst case)
            r = al*r+(1-al)*target
            yprev = x - r
        hist.append(r)
    return hist
for fsx in (48000.0, 192000.0):
    a=math.exp(-1/(20e-6*fsx))
    print('fs',fsx,'alpha',round(a,4),'stable S<',round((1+a)/(1-a),3),'monotone S<=',round(a/(1-a),3))
h=fbsim(20e-6,3.0,48000.0,False); print('naive S=3 (4:1) 48k last r values', [round(v,2) for v in h[-4:]])
h=fbsim(20e-6,3.0,48000.0,True); print('ZDF   S=3 (4:1) 48k last r values', [round(v,3) for v in h[-4:]], 'expected', 3*20/4)
h=fbsim(20e-6,19.0,192000.0,False); print('naive S=19 (20:1) 192k last', [round(v,2) for v in h[-4:]])
h=fbsim(20e-6,19.0,192000.0,True); print('ZDF S=19 192k last', [round(v,3) for v in h[-4:]], 'expected', 19*20/20)

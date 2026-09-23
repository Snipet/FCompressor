import math, random
def rhat(y,T,W,S):
    o=y-T
    if 2*o<=-W: return 0.0
    if 2*o>=W: return S*o
    return S*(o+W/2)**2/(2*W)
def zdf(x,r1,a,T,W,S):
    # linear region candidate
    r=(a*r1+(1-a)*S*(x-T))/(1+(1-a)*S)
    if x-r-T>=W/2: return r
    b=x-T+W/2; c=b-a*r1
    if c>0:
        k=(1-a)*S/(2*W); u=2*c/(1+math.sqrt(1+4*k*c))
        if u<=W: return b-u
    return a*r1
def brute(x,r1,a,T,W,S):
    lo,hi=-100.0,200.0
    for _ in range(200):
        m=(lo+hi)/2
        F=m-a*r1-(1-a)*rhat(x-m,T,W,S)
        if F>0: hi=m
        else: lo=m
    return (lo+hi)/2
random.seed(1); worst=0
for i in range(200000):
    x=random.uniform(-60,20); r1=random.uniform(0,40); a=random.uniform(0.01,0.9999); T=random.uniform(-40,0); W=random.uniform(0.01,24); S=random.uniform(0.01,50)
    worst=max(worst,abs(zdf(x,r1,a,T,W,S)-brute(x,r1,a,T,W,S)))
print('max |zdf - bisection| dB =',worst)

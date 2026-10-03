"""Reference implementation of the Wide Angle Fix horizontal remap (numpy).
Mirrors Shaders/WideAngle.metal so the maths can be tested off-Mac.

For each half of the frame, d = normalised distance from the centre line
(0 at centre, 1 at frame edge). Source distance is
    D(d) = d + amount * G(d),   G = integral of a smoothstep weight w(d)
w is 0 in the untouched centre, ramps up over `feather`, then stays 1.
Positive amount => sides are squeezed (people look slimmer).
"""
import numpy as np

def G(d, e0, r):
    r = max(r, 1e-6)
    t = np.clip((d - e0) / r, 0.0, 1.0)
    ramp = r * (t**3 - 0.5 * t**4)           # integral of smoothstep over the ramp
    return np.where(d <= e0 + r, ramp, r * 0.5 + (d - e0 - r))

def side_map(d, zone, feather, amount, half_w, fill):
    e0 = max(0.0, 1.0 - zone / half_w)
    r = feather / half_w
    D = d + amount * G(d, e0, r)
    if fill:
        D = D / (1.0 + amount * float(G(np.array(1.0), e0, r)))
    return D

def source_x(x, center=0.5, zl=0.33, fl=0.10, al=0.2, zr=0.33, fr=0.10, ar=0.2, fill=True):
    x = np.asarray(x, float)
    out = np.empty_like(x)
    L = x < center
    d = (center - x[L]) / center
    out[L] = center - center * side_map(d, zl, fl, al, center, fill)
    d = (x[~L] - center) / (1 - center)
    out[~L] = center + (1 - center) * side_map(d, zr, fr, ar, 1 - center, fill)
    return out

if __name__ == "__main__":
    x = np.linspace(0, 1, 2001)
    for amt in (-0.3, 0.0, 0.2, 0.5, 1.0):
        s = source_x(x, al=amt, ar=amt)
        slope = np.gradient(s, x)
        print(f"amount {amt:+.1f}: ends {s[0]:.4f}->{s[-1]:.4f}  monotonic={np.all(np.diff(s)>0)}  "
              f"slope centre={slope[1000]:.3f} edge={slope[5]:.3f}")
    # unfilled: centre stays exactly 1:1, edges sample outside the frame
    s = source_x(x, al=0.5, ar=0.5, fill=False)
    print("no-fill ends", s[0], s[-1], "centre slope", np.gradient(s, x)[1000])

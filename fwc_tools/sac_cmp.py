import pickle, sys
o = pickle.load(open('/tmp/sac_dual.pkl', 'rb')); inp = o['inp']
MODES = ['cube','ship','ball','ufo','wave','robot','spider','swing']
def first_diff(a, b):
    for t in sorted(set(a) & set(b)):
        x, y = a[t], b[t]
        if any(abs(u - v) > 1e-3 for u, v in zip(x, y)): return t
def row(d, t):
    v = d[t]
    return 'dual %d | P1 y %.1f v %.2f %s fl %.0f ce %.0f gnd %d hold %d latch %d | P2 y %.1f v %.2f %s fl %.0f ce %.0f gnd %d hold %d latch %d mode %d' % (v[0], v[1], v[2], 'up' if v[3] else 'dn', v[4], v[5], v[6], v[7], v[8], v[9], v[10], 'up' if v[11] else 'dn', v[12], v[13], v[14], v[15], v[16], v[17])
which = sys.argv[1]; pre = int(sys.argv[2]); post = int(sys.argv[3]); stp = int(sys.argv[4])
base, alt = o['base'][1], o[which][1]
k = first_diff(base, alt); print(which, 'first diff tick', k, '%.2f초' % (k / 240), 'x', o['base'][0][k][0], 'P1 mode', o['base'][0][k][2])
for t in range(k - pre, k + post, stp):
    if t in base: print(t, 'in', int(inp[t]), 'BASE', row(base, t))
    if t in alt: print(t, '     ', 'ALT ', row(alt, t))
ky = next(t for t in sorted(set(base) & set(alt)) if abs(base[t][1] - alt[t][1]) > 1e-3 or base[t][0] != alt[t][0] or (base[t][0] and abs(base[t][9] - alt[t][9]) > 1e-3))
print('--- first position diff tick', ky, '%.2f초' % (ky / 240), 'x', o['base'][0][ky][0], o['base'][0][ky][2])
rel = [t for t in range(k, ky + 4) if inp[t] != inp[t - 1]]; print('input changes between', rel)
for t in range(ky - 8, ky + 6):
    if t in base: print(t, 'in', int(inp[t]), 'BASE', row(base, t))
    if t in alt: print(t, '     ', 'ALT ', row(alt, t))

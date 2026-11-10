#!/usr/bin/env python3
"""Check the app against your Excel workbook.

Usage:  python compare_to_excel.py BALANCE_PivotTable.xlsx [--date YYYY-MM-DD] [--keep out.fbd]
Needs:  pip install openpyxl, and g++ (to build tools/fbd_breakdown.cpp, which runs the app's own core maths).

Steps: (1) xlsx_to_fbd.py makes a .fbd from 'Source Data'; (2) fbd_breakdown prints the app's own Breakdown and
totals for it; (3) this script compares them with the workbook's 'Breakdown' pivot and the balance block
(Debtor / Cash / Total / Entered Total / Difference) and lists every difference. Exit code 0 = all match.
Pivot values are CACHED in the file: refresh the pivot in Excel (Data > Refresh All) before saving, or stale pivot
rows will show up here as differences.
"""
import os, subprocess, sys, tempfile
try:
    import openpyxl
except ImportError:
    sys.exit("This needs openpyxl: pip install openpyxl")
HERE = os.path.dirname(os.path.abspath(__file__))
TOL = 0.005

def main(argv):
    args = [a for a in argv[1:]]
    date = None; keep = None
    for opt in ('--date', '--keep'):
        if opt in args:
            i = args.index(opt); val = args[i + 1]; del args[i:i + 2]
            if opt == '--date': date = val
            else: keep = val
    if not args: sys.exit(__doc__)
    xlsx = args[0]
    tmp = tempfile.mkdtemp()
    fbd = keep or os.path.join(tmp, 'check.fbd')
    exe = os.path.join(tmp, 'fbd_breakdown')
    cmd = [sys.executable, os.path.join(HERE, 'xlsx_to_fbd.py'), xlsx, fbd] + (['--date', date] if date else [])
    print(subprocess.run(cmd, check=True, capture_output=True, text=True).stdout.strip())
    subprocess.run(['g++', '-std=c++17', '-I', os.path.join(HERE, '..', 'src', 'core'),
                    os.path.join(HERE, 'fbd_breakdown.cpp'), '-o', exe], check=True)
    out = subprocess.run([exe, fbd], check=True, capture_output=True, text=True).stdout.splitlines()

    app = {}   # (supplier, product, price) lowercase key -> [kgs, amt]
    for ln in out:
        f = ln.split('\t')
        if f[0] == 'P':
            k = (f[1].strip().lower(), f[2].strip().lower(), round(float(f[3]), 4))
            a = app.setdefault(k, [0.0, 0.0]); a[0] += float(f[4]); a[1] += float(f[5])
        elif f[0] == 'T':
            app_debtor, app_cash, app_entered, n = float(f[1]), float(f[2]), float(f[3]), int(f[4])

    wbv = openpyxl.load_workbook(xlsx, data_only=True)
    # --- Excel pivot (Breakdown sheet): supplier/product carried down; skip subtotal / grand total rows
    xl = {}; sup = prod = None
    for row in wbv['Breakdown'].iter_rows(min_row=4, values_only=True):
        a, b, c, _, e, f = (list(row) + [None] * 6)[:6]
        if a is not None: sup = str(a).strip()
        if b is not None and str(b).strip().endswith(' Total'): continue
        if a is not None and str(a).strip().lower() in ('grand total', '(blank)'): continue
        if b is not None: prod = str(b).strip()
        if c is None or e is None: continue
        k = (sup.lower(), prod.lower(), round(float(c), 4))
        x = xl.setdefault(k, [0.0, 0.0]); x[0] += float(e); x[1] += float(f or 0)

    diffs = []
    for k in sorted(set(app) | set(xl)):
        a, x = app.get(k), xl.get(k)
        if a is None: diffs.append(f"only in Excel pivot : {k[0]} / {k[1]} @ {k[2]:g}  kgs {x[0]:.1f}  total {x[1]:.2f}")
        elif x is None: diffs.append(f"only in the app    : {k[0]} / {k[1]} @ {k[2]:g}  kgs {a[0]:.1f}  total {a[1]:.2f}")
        elif abs(a[0] - x[0]) > TOL or abs(a[1] - x[1]) > TOL:
            diffs.append(f"differs            : {k[0]} / {k[1]} @ {k[2]:g}  app kgs {a[0]:.1f} / ${a[1]:.2f}   Excel kgs {x[0]:.1f} / ${x[1]:.2f}")

    # --- balance block on Source Data (values as Excel last calculated them)
    xd = xc = xt = xe = None
    for row in wbv['Source Data'].iter_rows(min_row=1, values_only=True):
        lab = str(row[6] or '').strip().lower() if len(row) > 6 else ''
        vals = [v for v in row[7:] if isinstance(v, (int, float))]
        if lab == 'debtor' and vals: xd = vals[0]
        elif lab == 'cash' and vals: xc = vals[0]
        elif lab == 'total' and vals: xt = vals[0]
        elif lab == 'entered total' and vals: xe = vals[0]
    def chk(name, mine, theirs):
        if theirs is None: return f"{name:14} app {mine:12.2f}   (not found in workbook)"
        ok = abs(mine - theirs) <= TOL
        if not ok: diffs.append(f"{name} differs: app {mine:.2f}  Excel {theirs:.2f}")
        return f"{name:14} app {mine:12.2f}   Excel {theirs:12.2f}   {'OK' if ok else 'DIFFERENT'}"
    print(f"\nEntries loaded into the app: {n}")
    print(chk('Debtor', app_debtor, xd)); print(chk('Cash', app_cash, xc))
    print(chk('Total (D+C)', app_debtor + app_cash, xt)); print(chk('Entered Total', app_entered, xe))
    print(f"{'Difference':14} app {round(app_debtor + app_cash - app_entered, 2) + 0.0:12.2f}   Excel {(round(xt - xe, 2) + 0.0) if xt is not None and xe is not None else float('nan'):12.2f}")
    print(f"\nBreakdown lines: app {len(app)}, Excel pivot {len(xl)}")
    if diffs:
        print(f"\n{len(diffs)} DIFFERENCE(S):"); [print('  ' + d) for d in diffs]; return 1
    print("\nAll Breakdown lines and totals match."); return 0

if __name__ == '__main__': sys.exit(main(sys.argv))

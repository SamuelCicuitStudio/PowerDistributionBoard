import tkinter as tk
from tkinter import ttk, messagebox
import math

# ===== Planner logic (EXACT port of your C++ helpers) =====

def pop10(mask: int) -> int:
    # popcount for up to 10 bits
    return bin(mask & 0x3FF).count("1")

def req(mask: int, R):
    """Parallel equivalent resistance for set 'mask' using R[0..9]."""
    g = 0.0
    for i in range(10):
        if mask & (1 << i):
            Ri = R[i]
            if Ri > 0.01 and math.isfinite(Ri):
                g += 1.0 / Ri
    if g <= 0.0:
        return math.inf
    return 1.0 / g

def choose_best(allowed_mask: int,
                R,
                target: float,
                max_active: int,
                prefer_above_or_equal: bool,
                recent_mask: int) -> int:
    """Exact translation of _chooseBest from DeviceLoop.cpp."""
    best_score = math.inf
    best = 0
    found_above = False
    FULL = 1 << 10  # 10 bits

    for m in range(1, FULL):
        # must be subset of allowed
        if (m & ~allowed_mask) != 0:
            continue

        k = pop10(m)
        if k == 0 or k > max_active:
            continue

        Req = req(m, R)
        if not math.isfinite(Req):
            continue

        above = (Req >= target)
        err = abs(Req - target)

        if prefer_above_or_equal:
            if above and not found_above:
                # first "above" candidate → reset state
                found_above = True
                best_score = math.inf
                best = 0
            if (not above) and found_above:
                # once we have some "above" candidates, ignore undershoots
                continue

        score = err
        if m == recent_mask:
            score += 0.0001  # mild fairness

        # tie-breakers: fewer channels, then higher Req (safer)
        if (score < best_score or
            (score == best_score and k < pop10(best)) or
            (score == best_score and k == pop10(best) and Req > req(best, R))):
            best_score = score
            best = m

    # If we insisted on ≥ target but got nothing, retry allowing undershoot
    if prefer_above_or_equal and best == 0:
        return choose_best(allowed_mask, R, target,
                           max_active, False, recent_mask)
    return best

def build_plan(allowed_mask: int,
               R,
               target: float,
               max_active: int,
               prefer_above_or_equal: bool):
    """Exact translation of _buildPlan from DeviceLoop.cpp."""
    plan = []
    remaining = allowed_mask
    last = 0

    while remaining:
        pick = choose_best(remaining, R, target,
                           max_active, prefer_above_or_equal, last)
        if pick == 0:
            # no multi-group possible → pick best single wire
            best_err = math.inf
            solo = 0
            for i in range(10):
                if remaining & (1 << i):
                    Req = req(1 << i, R)
                    err = abs(Req - target)
                    if err < best_err:
                        best_err = err
                        solo = (1 << i)
            pick = solo
            if pick == 0:
                break

        plan.append(pick)
        remaining &= ~pick
        last = pick

    return plan

# ===== GUI =====

class PlannerGUI:
    def __init__(self, root):
        self.root = root
        root.title("Advanced Mode Planner Tester")

        main = ttk.Frame(root, padding=10)
        main.grid(row=0, column=0, sticky="nsew")
        root.rowconfigure(0, weight=1)
        root.columnconfigure(0, weight=1)

        # --- Top configuration row ---
        cfg = ttk.Frame(main)
        cfg.grid(row=0, column=0, sticky="ew", pady=(0, 10))
        for c in range(0, 8):
            cfg.columnconfigure(c, weight=0)
        cfg.columnconfigure(7, weight=1)

        ttk.Label(cfg, text="Target Req [Ω]:").grid(row=0, column=0, sticky="w")
        self.target_entry = ttk.Entry(cfg, width=8)
        self.target_entry.grid(row=0, column=1, padx=5)
        self.target_entry.insert(0, "16.0")

        ttk.Label(cfg, text="Max active wires:").grid(row=0, column=2, sticky="w", padx=(10, 0))
        self.max_active_spin = ttk.Spinbox(cfg, from_=1, to=10, width=4)
        self.max_active_spin.grid(row=0, column=3, padx=5)
        self.max_active_spin.set("4")

        self.prefer_above_var = tk.BooleanVar(value=True)
        prefer_chk = ttk.Checkbutton(cfg,
                                     text="Prefer Req ≥ target",
                                     variable=self.prefer_above_var)
        prefer_chk.grid(row=0, column=4, padx=(10, 0))

        # New: Input voltage field for current calculation
        ttk.Label(cfg, text="Input V [V]:").grid(row=0, column=5, sticky="w", padx=(10, 0))
        self.vin_entry = ttk.Entry(cfg, width=8)
        self.vin_entry.grid(row=0, column=6, padx=5, sticky="w")
        self.vin_entry.insert(0, "230.0")  # adjust if you use another bus voltage

        # --- Headers ---
        header = ttk.Frame(main)
        header.grid(row=1, column=0, sticky="ew")
        for c, text in enumerate(["Wire", "Temp [°C]", "Locked", "R [Ω]"]):
            ttk.Label(header, text=text).grid(row=0, column=c, padx=4)

        self.temp_scales = []
        self.lock_vars = []
        self.r_entries = []

        # --- 10 wire rows ---
        for i in range(10):
            row = ttk.Frame(main)
            row.grid(row=2 + i, column=0, sticky="ew", pady=2)

            ttk.Label(row, text=f"{i+1}").grid(row=0, column=0, padx=4)

            temp = tk.DoubleVar(value=25.0)
            scale = ttk.Scale(row, from_=20, to=200,
                              orient="horizontal", variable=temp)
            scale.grid(row=0, column=1, padx=4, sticky="ew")
            row.columnconfigure(1, weight=1)

            lock_var = tk.BooleanVar(value=False)
            lock_cb = ttk.Checkbutton(row, variable=lock_var)
            lock_cb.grid(row=0, column=2, padx=4)

            r_entry = ttk.Entry(row, width=8)
            r_entry.grid(row=0, column=3, padx=4)
            r_entry.insert(0, "41.0")

            self.temp_scales.append(temp)
            self.lock_vars.append(lock_var)
            self.r_entries.append(r_entry)

        # --- Build Plan button ---
        btn = ttk.Button(main, text="Build Plan",
                         command=self.on_build_plan)
        btn.grid(row=12, column=0, pady=(10, 4), sticky="ew")

        # --- Output box ---
        self.output = tk.Text(main, height=12, width=80)
        self.output.grid(row=13, column=0, sticky="nsew")
        main.rowconfigure(13, weight=1)

    def on_build_plan(self):
        # Read global params
        try:
            target = float(self.target_entry.get())
        except ValueError:
            messagebox.showerror("Error", "Invalid target resistance")
            return

        try:
            max_active = int(self.max_active_spin.get())
        except ValueError:
            messagebox.showerror("Error", "Invalid max active value")
            return

        try:
            vin = float(self.vin_entry.get())
        except ValueError:
            messagebox.showerror("Error", "Invalid input voltage")
            return

        if vin <= 0:
            messagebox.showerror("Error", "Input voltage must be > 0")
            return

        max_active = max(1, min(10, max_active))
        prefer_above = self.prefer_above_var.get()

        # Read per-wire resistances
        R = []
        for i in range(10):
            txt = self.r_entries[i].get().strip()
            try:
                val = float(txt)
            except ValueError:
                val = 0.0
            R.append(val if val > 0 else 0.0)

        # Build allowed mask:
        # - allowed if NOT locked AND R > 0
        # - plus auto-lock if Temp >= 150°C (matches your safety behavior)
        allowed_mask = 0
        for i in range(10):
            temp = float(self.temp_scales[i].get())
            locked = self.lock_vars[i].get() or (temp >= 150.0)
            if (not locked) and R[i] > 0.01 and math.isfinite(R[i]):
                allowed_mask |= (1 << i)

        self.output.delete("1.0", tk.END)

        if allowed_mask == 0:
            self.output.insert(tk.END, "No allowed wires (all locked or R=0).\n")
            messagebox.showinfo("Result", "No allowed wires (all locked or R=0).")
            return

        # Run planner with EXACT same logic
        plan = build_plan(allowed_mask, R, target, max_active, prefer_above)

        self.output.insert(
            tk.END,
            f"Allowed mask: 0b{allowed_mask:010b}\n"
            f"Target Req: {target:.4f} Ω, "
            f"max_active={max_active}, "
            f"prefer_above={prefer_above}, "
            f"Vin={vin:.2f} V\n\n"
        )

        if not plan:
            self.output.insert(tk.END, "Planner returned an empty plan.\n")
            return

        for step, mask in enumerate(plan, start=1):
            wires = [str(i+1) for i in range(10) if mask & (1 << i)]
            Req = req(mask, R)
            if math.isfinite(Req) and Req > 0:
                I = vin / Req
            else:
                I = 0.0

            self.output.insert(
                tk.END,
                f"Step {step}: mask=0b{mask:010b}  "
                f"wires=[{', '.join(wires)}]  "
                f"Req={Req:.4f} Ω  "
                f"I={I:.3f} A\n"
            )

def main():
    root = tk.Tk()
    PlannerGUI(root)
    root.mainloop()

if __name__ == "__main__":
    main()

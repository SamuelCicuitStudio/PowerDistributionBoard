#!/usr/bin/env python3
"""Generate calibration history samples for files in data/calib_history."""

import json
import math
import random
from pathlib import Path

DATA_DIR = Path(__file__).resolve().parents[1] / "data" / "calib_history"
DEFAULT_INTERVAL_MS = 500
DEFAULT_AMBIENT_C = 25.0
DEFAULT_TARGET_C = 60.0
DEFAULT_MODE = "model"
DEFAULT_WIRE_INDEX = 1


def clamp(value, lo, hi):
    return max(lo, min(hi, value))


def ntc_ohms(temp_c, r0=10000.0, beta=3950.0, t0_c=25.0):
    t_k = temp_c + 273.15
    t0_k = t0_c + 273.15
    return r0 * math.exp(beta * (1.0 / t_k - 1.0 / t0_k))


def generate_temperature_profile(count, t_min, t_peak, t_ambient):
    if count <= 1:
        return [t_ambient]

    rise_count = max(2, int(count * 0.55))
    fall_count = max(2, count - rise_count)

    temps = []
    for i in range(rise_count):
        frac = i / (rise_count - 1)
        temp = t_min + frac * (t_peak - t_min)
        temp += random.uniform(-1.5, 1.5)
        temps.append(clamp(temp, t_min, t_peak))

    for i in range(fall_count):
        frac = i / (fall_count - 1)
        temp = t_peak + frac * (t_ambient - t_peak)
        temp += random.uniform(-1.2, 1.2)
        temps.append(clamp(temp, min(t_min, t_ambient), t_peak))

    temps[-1] = t_ambient
    return temps[:count]


def generate_samples(count, interval_ms, t_min, t_peak, t_ambient):
    temps = generate_temperature_profile(count, t_min, t_peak, t_ambient)
    samples = []

    for idx, temp_c in enumerate(temps):
        t_ms = idx * interval_ms
        heat_frac = clamp(temp_c / t_peak, 0.0, 1.0)

        voltage_v = 48.0 + random.uniform(-0.6, 0.6) - 0.3 * heat_frac
        current_a = 2.0 + 8.0 * heat_frac + random.uniform(-0.5, 0.6)
        current_a = max(0.0, current_a)

        r_ntc = ntc_ohms(temp_c)
        r_fixed = 10000.0
        v_ref = 3.3
        ntc_v = v_ref * (r_ntc / (r_ntc + r_fixed))
        ntc_adc = int(round(clamp(ntc_v / v_ref, 0.0, 1.0) * 4095))

        samples.append(
            {
                "t_ms": t_ms,
                "v": round(voltage_v, 3),
                "i": round(current_a, 3),
                "temp_c": round(temp_c, 2),
                "ntc_v": round(ntc_v, 4),
                "ntc_ohm": int(round(r_ntc)),
                "ntc_adc": ntc_adc,
                "ntc_ok": True,
                "pressed": random.random() < 0.02,
            }
        )

    return samples


def parse_epoch_from_name(path):
    try:
        return int(path.stem)
    except ValueError:
        return None


def update_history_file(path):
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        payload = {}

    meta = payload.get("meta", {})
    interval_ms = int(meta.get("interval_ms") or DEFAULT_INTERVAL_MS)

    start_epoch = parse_epoch_from_name(path) or int(meta.get("start_epoch") or 0)
    start_ms = int(meta.get("start_ms") or 0)
    target_c = float(meta.get("target_c") or DEFAULT_TARGET_C)
    wire_index = int(meta.get("wire_index") or DEFAULT_WIRE_INDEX)

    sample_count = max(240, int(meta.get("count") or 0), 320)
    ambient_c = float(meta.get("ambient_c") or DEFAULT_AMBIENT_C)

    samples = generate_samples(sample_count, interval_ms, 0.0, 150.0, ambient_c)

    saved_ms = start_ms + (sample_count - 1) * interval_ms
    saved_epoch = start_epoch + max(0, saved_ms // 1000) if start_epoch else 0

    meta.update(
        {
            "mode": meta.get("mode", DEFAULT_MODE),
            "running": False,
            "count": sample_count,
            "capacity": max(int(meta.get("capacity") or 0), sample_count),
            "interval_ms": interval_ms,
            "start_ms": start_ms,
            "start_epoch": start_epoch,
            "target_c": target_c,
            "wire_index": wire_index,
            "saved": True,
            "saved_ms": saved_ms,
            "saved_epoch": saved_epoch,
        }
    )

    payload = {"meta": meta, "samples": samples}
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def main():
    if not DATA_DIR.exists():
        raise SystemExit(f"Missing folder: {DATA_DIR}")

    files = sorted(DATA_DIR.glob("*.json"))
    if not files:
        raise SystemExit(f"No .json files found in {DATA_DIR}")

    for path in files:
        update_history_file(path)

    print(f"Updated {len(files)} calibration history files in {DATA_DIR}")


if __name__ == "__main__":
    random.seed(42)
    main()

#!/usr/bin/env python3
"""Runs renode/tests/model_selftest.resc and checks the output against an independent
Python implementation of the Bosch BMP280 compensation and the MPU6050 scaling.

Usage (from the repository root):
    python3 renode/tests/check_models.py [--renode /path/to/renode]
"""
import argparse
import os
import re
import subprocess
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_RENODE = "/opt/renode-dl/renode_1.15.3-dotnet_portable/renode"


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def parse_calib(b):
    w = [b[i] | (b[i + 1] << 8) for i in range(0, 24, 2)]
    t1, p1 = w[0], w[3]
    t2, t3 = s16(w[1]), s16(w[2])
    p = [s16(x) for x in w[4:]]
    return dict(T1=t1, T2=t2, T3=t3, P1=p1, P2=p[0], P3=p[1], P4=p[2], P5=p[3],
                P6=p[4], P7=p[5], P8=p[6], P9=p[7])


def comp_t(adc_t, c):
    # datasheet bmp280_compensate_T_int32 (Python >> on negatives == arithmetic shift, like C)
    var1 = (((adc_t >> 3) - (c["T1"] << 1)) * c["T2"]) >> 11
    var2 = (((((adc_t >> 4) - c["T1"]) * ((adc_t >> 4) - c["T1"])) >> 12) * c["T3"]) >> 14
    t_fine = var1 + var2
    return (t_fine * 5 + 128) >> 8, t_fine


def cdiv(a, b):
    # C integer division truncates toward zero
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def comp_p64(adc_p, t_fine, c):
    var1 = t_fine - 128000
    var2 = var1 * var1 * c["P6"]
    var2 = var2 + ((var1 * c["P5"]) << 17)
    var2 = var2 + (c["P4"] << 35)
    var1 = ((var1 * var1 * c["P3"]) >> 8) + ((var1 * c["P2"]) << 12)
    var1 = (((1 << 47) + var1) * c["P1"]) >> 33
    if var1 == 0:
        return 0
    p = 1048576 - adc_p
    p = cdiv(((p << 31) - var2) * 3125, var1)
    var1 = (c["P9"] * (p >> 13) * (p >> 13)) >> 25
    var2 = (c["P8"] * p) >> 19
    p = ((p + var1 + var2) >> 8) + (c["P7"] << 4)
    return p / 256.0


def comp_p32(adc_p, t_fine, c):
    # datasheet bmp280_compensate_P_int32 (32-bit variant, for firmware that uses it)
    def i32(x):
        x &= 0xFFFFFFFF
        return x - (1 << 32) if x & 0x80000000 else x
    var1 = i32((t_fine >> 1) - 64000)
    var2 = i32((((var1 >> 2) * (var1 >> 2)) >> 11) * c["P6"])
    var2 = i32(var2 + ((var1 * c["P5"]) << 1))
    var2 = i32((var2 >> 2) + (c["P4"] << 16))
    var1 = i32((((c["P3"] * (((var1 >> 2) * (var1 >> 2)) >> 13)) >> 3) + ((c["P2"] * var1) >> 1)) >> 18)
    var1 = i32((((32768 + var1)) * c["P1"]) >> 15)
    if var1 == 0:
        return 0
    p = ((1048576 - adc_p) - (var2 >> 12)) * 3125 & 0xFFFFFFFF
    if p < 0x80000000:
        p = (p << 1) // (var1 & 0xFFFFFFFF)
    else:
        p = (p // (var1 & 0xFFFFFFFF)) * 2
    p &= 0xFFFFFFFF
    var1 = i32((c["P9"] * i32((((p >> 3) * (p >> 3)) >> 13) & 0xFFFFFFFF)) >> 12)
    var2 = i32(((p >> 2) * c["P8"]) >> 13)
    p = (p + ((var1 + var2 + c["P7"]) >> 4)) & 0xFFFFFFFF
    return float(p)


def raw20(b, i):
    return (b[i] << 12) | (b[i + 1] << 4) | (b[i + 2] >> 4)


def hexline(out, label):
    m = re.search(re.escape(label) + r".*\n\s*([0-9A-F ]+)\n", out)
    if not m:
        raise SystemExit("missing " + label)
    return [int(x, 16) for x in m.group(1).split()]


def valline(out, label):
    m = re.search(re.escape(label) + r".*\n\s*(0x[0-9A-Fa-f]+)", out)
    if not m:
        raise SystemExit("missing " + label)
    return int(m.group(1), 16)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--renode", default=DEFAULT_RENODE)
    args = ap.parse_args()
    out = subprocess.run([args.renode, "--disable-gui", "--console", "--plain",
                          "renode/tests/model_selftest.resc", "-e", "quit"],
                         cwd=REPO, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=300).stdout.replace("\r", "")
    failures = []

    def check(name, cond, detail=""):
        print(("PASS " if cond else "FAIL ") + name + (" " + detail if detail else ""))
        if not cond:
            failures.append(name)

    check("MPU WHO_AM_I", valline(out, "WHO_AM_I:") == 0x68)
    check("MPU PWR_MGMT_1 reset", valline(out, "PWR_MGMT_1 after reset:") == 0x40)
    b = hexline(out, "BURST14")
    v = [s16((b[i] << 8) | b[i + 1]) for i in range(0, 14, 2)]
    exp = [round(0.5 * 8192), round(-0.25 * 8192), 8192, 0, round(10 * 65.5), round(-20 * 65.5), round(100 * 65.5)]
    check("MPU burst 0x3B..0x48", v == exp, "got %s expected %s" % (v, exp))
    check("MPU DEVICE_RESET", valline(out, "PWR_MGMT_1 after DEVICE_RESET:") == 0x40 and
          valline(out, "ACCEL_CONFIG after DEVICE_RESET:") == 0)

    check("BMP CHIP_ID", valline(out, "CHIP_ID:") == 0x58)
    c = parse_calib(hexline(out, "CALIB24"))
    expc = dict(T1=27504, T2=26435, T3=-1000, P1=36477, P2=-10685, P3=3024, P4=2855, P5=140,
                P6=-7, P7=15500, P8=-14600, P9=6000)
    check("BMP calibration", c == expc, str(c))
    for label, p_set, t_set in (("RAW6_A", 100000.0, 25.00), ("RAW6_B", 98765.43, -12.34),
                                ("RAW6_C", 101325.0, 21.50)):
        r = hexline(out, label)
        adc_p, adc_t = raw20(r, 0), raw20(r, 3)
        t, t_fine = comp_t(adc_t, c)
        p64 = comp_p64(adc_p, t_fine, c)
        p32 = comp_p32(adc_p, t_fine, c)
        check("BMP %s temperature" % label, abs(t / 100.0 - t_set) <= 0.011,
              "adc_T=%d -> %.2f C (set %.2f)" % (adc_t, t / 100.0, t_set))
        check("BMP %s pressure int64" % label, abs(p64 - p_set) <= 0.1,
              "adc_P=%d -> %.3f Pa (set %.2f)" % (adc_p, p64, p_set))
        check("BMP %s pressure int32" % label, abs(p32 - p_set) <= 5.0,
              "-> %.0f Pa (set %.2f)" % (p32, p_set))
    check("BMP forced -> sleep", valline(out, "CTRL_MEAS after forced") & 3 == 0)

    check("I2C START -> SB", valline(out, "SR1 after START") & 0x1 == 0x1)
    check("I2C addr+W -> ADDR|TXE", valline(out, "SR1 after addr+W (expect") & 0x82 == 0x82)
    check("I2C addr+R -> ADDR|RXNE", valline(out, "SR1 after addr+R") & 0x42 == 0x42)
    check("I2C DR = WHO_AM_I", valline(out, "DR (expect WHO_AM_I") == 0x68)
    sr1 = valline(out, "SR1 after addr+W to disconnected device")
    check("Disconnected -> AF, no ADDR", (sr1 & 0x400) and not (sr1 & 0x2), hex(sr1))
    check("AF read-to-clear", valline(out, "SR1 read again") & 0x400 == 0)
    check("Reconnect -> ADDR", valline(out, "SR1 after reconnect") & 0x2 == 0x2)
    check("RCC_CSR power-on flags", valline(out, "=== RCC_CSR reset flags") == 0x0E000000)
    check("RCC_CSR RMVF", valline(out, "after RMVF") == 0)

    print("\n%d failure(s)" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

# -*- coding: utf-8 -*-
# 按 shared/jt808.c 的 jt808_build 规则逐字节生成平台下发测试帧
PHONE = bytes([0x09, 0x11, 0x11, 0x22, 0x22, 0x00])
TERM = bytes([0x09, 0x11, 0x11, 0x22, 0x22, 0x00])


def build(msg_id, body, serial):
    attr = len(body) & 0x0FFF
    raw = bytearray()
    raw.append(0x7E)
    raw += bytes([msg_id >> 8, msg_id & 0xFF])
    raw += bytes([(attr >> 8) & 0x03, attr & 0xFF])
    raw += PHONE + TERM
    raw += bytes([(serial >> 8) & 0xFF, serial & 0xFF])
    raw += bytes([0, 0, 0, 0])
    raw += bytes(body)
    x = 0
    for b in raw[1:]:
        x ^= b
    raw.append(x)
    raw.append(0x7E)
    out = bytearray()
    out.append(0x7E)
    for b in raw[1:-1]:
        if b in (0x7E, 0x7D):
            out += bytes([0x7D, 0x02 if b == 0x7E else 0x01])
        else:
            out.append(b)
    out.append(0x7E)
    return bytes(out)


cases = [
    (0x8210, [0x01], "疲劳驾驶 ON"),
    (0x8210, [0x00], "疲劳驾驶 OFF"),
    (0x8811, [0x01], "蜂鸣器 ON"),
    (0x8811, [0x00], "蜂鸣器 OFF"),
    (0x8110, [0x00], "身份验证 通过 PASS(舵机开门)"),
    (0x8110, [0x01], "身份验证 失败 FAIL"),
]

serial = 1
for mid, body, desc in cases:
    f = build(mid, body, serial)
    serial += 1
    print("| %-32s | %s |" % (desc, "".join("%02X" % b for b in f)))

f = build(0x8001, [0x00, 0x01, 0x02, 0x00, 0x00], serial)
print("| %-32s | %s |" % ("平台通用应答 0x8001(示意)", "".join("%02X" % b for b in f)))

# -*- coding: utf-8 -*-
"""生成"平台->终端"下行帧（严格复刻 shared/jt808.c 与 fake_platform.py 的组帧规则），
并逐帧回灌到同一套解码逻辑里做自校验。结果同时写入 out.txt。"""

PHONE = bytes([0x09, 0x11, 0x11, 0x22, 0x22, 0x00])   # "091111222200" BCD
TERM  = bytes([0x09, 0x11, 0x11, 0x22, 0x22, 0x00])
SERIAL = 0x0001                                        # 每帧都用同一条流水号，方便单帧直接粘贴


def build_raw(msg_id, serial, body):
    raw = bytearray([0x7E])
    raw += bytes([(msg_id >> 8) & 0xFF, msg_id & 0xFF])
    raw += bytes([(len(body) >> 8) & 0x03, len(body) & 0xFF])
    raw += PHONE + TERM
    raw += bytes([(serial >> 8) & 0xFF, serial & 0xFF])
    raw += bytes([0x00, 0x01])      # 总包数
    raw += bytes([0x00, 0x01])      # 包序号
    raw += body
    x = 0
    for b in raw[1:]:
        x ^= b
    raw.append(x)
    raw.append(0x7E)
    return bytes(raw)


def escape(raw):
    out = bytearray([0x7E])
    for b in raw[1:-1]:
        if b in (0x7E, 0x7D):
            out += bytes([0x7D, 0x02 if b == 0x7E else 0x01])
        else:
            out.append(b)
    out.append(0x7E)
    return bytes(out)


def decode(check):
    """复刻 jt808_process：返回 [(msg_id, body), ...]"""
    buf, in_frame, esc = bytearray(), False, False
    frames = []
    for c in check:
        if not in_frame:
            if c == 0x7E:
                in_frame, buf, esc = True, bytearray(), False
            continue
        if esc:
            esc = False
            buf.append(0x7E if c == 0x02 else (0x7D if c == 0x01 else c))
            continue
        if c == 0x7D:
            esc = True
            continue
        if c == 0x7E:
            if len(buf) >= 23:
                blen = len(buf) - 23
                if (((buf[2] & 0x03) << 8) | buf[3]) == blen:
                    x = 0
                    for b in buf[:-1]:
                        x ^= b
                    if x == buf[-1]:
                        frames.append(((buf[0] << 8) | buf[1], bytes(buf[22:22 + blen])))
            buf, in_frame, esc = bytearray(), False, False
            continue
        buf.append(c)
    return frames


CASES = [
    ("0x8210", "疲劳驾驶 ON  -> 蜂鸣器周期响 + 红灯报警序列", 0x8210, bytes([0x01])),
    ("0x8210", "疲劳驾驶 OFF -> 解除报警",                    0x8210, bytes([0x00])),
    ("0x8110", "身份验证 准许 -> PASS + 开门 + 短响一声",      0x8110, bytes([0x00])),
    ("0x8110", "身份验证 拒绝 -> FAIL",                       0x8110, bytes([0x01])),
    ("0x8811", "蜂鸣器 ON     -> 持续响",                     0x8811, bytes([0x01])),
    ("0x8811", "蜂鸣器 OFF    -> 停",                         0x8811, bytes([0x00])),
    ("0x8001", "平台通用应答（可选）",                          0x8001, bytes([0x00, 0x01, 0x02, 0x00, 0x00])),
]

lines = []
lines.append("%-8s | %-42s | %s" % ("消息号", "作用", "粘贴到发送框的十六进制(全部小写亦可)"))
lines.append("-" * 150)
for mid_s, desc, mid, body in CASES:
    raw = build_raw(mid, SERIAL, body)
    frame = escape(raw)
    got = decode(frame)
    assert got == [(mid, body)], (desc, got)
    hx = frame.hex().upper()
    # 每两字符插空格，便于人眼核对
    hxs = " ".join(hx[i:i + 2] for i in range(0, len(hx), 2))
    lines.append("%-8s | %-42s | %s" % (mid_s, desc, hx))
    lines.append("%-8s | %-42s | %s" % ("", "(带空格版)", hxs))
    lines.append("")
lines.append("全部 %d 帧自校验通过：长度域 / 异或校验 / 7E-7D 转义 / 消息体 全部一致。" % len(CASES))

with open("C:/Users/adnim/WorkBuddy/2026-08-31-09-34-50/out.txt", "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
print("written")

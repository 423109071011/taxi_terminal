#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
出租车终端 —— 简易模拟平台（用于联调 / 验收）

作用：在 PC 或虚拟机上起一个 TCP 服务端，接收终端的 JT808 帧，
      解析并打印；收到身份验证请求(0x0210)时回准许/拒绝(0x8110)；
      可以手动下发疲劳驾驶状态(0x8210)。

运行：  python3 fake_platform.py [端口]        默认端口 8888
命令：  help / list / fatigue 1 / fatigue 0 / strict 0 / strict 1 / quit

帧格式与终端 shared/jt808.c 完全一致：
  7E | 消息ID(2) | 消息体属性(2) | 终端手机号(6) | 终端ID(6) | 流水号(2) |
  总包数(2) | 包序号(2) | 消息体(N) | 异或校验(1) | 7E
  转义：0x7E -> 7D 02 ；0x7D -> 7D 01（头尾 7E 不转义）
"""

import socket
import sys
import threading
import time

try:
    sys.stdout.reconfigure(line_buffering=True)   # 保证重定向/管道时日志实时输出
except AttributeError:
    pass

# ---------------- 消息号 ----------------
MSG_LOCATION = 0x0200
MSG_AUTH_REQ = 0x0210
MSG_AUTH_RESP = 0x8110
MSG_FATIGUE = 0x8210
MSG_COMMON_ACK = 0x8001

# ---------------- 司机白名单（卡号 -> 身份码）----------------
DRIVERS = {
    "11223344": "123456",
    "AABBCCDD": "888888",
}
STRICT = False          # False = 全部放行（默认，方便第一次联调）

# ---------------- 运行状态 ----------------
clients = []            # 已连接的终端 socket
clients_lock = threading.Lock()
client_ids = {}         # socket -> (phone6, term6)，用于下发时填正确的终端号
tx_serial = 1
rx_count = 0


# ================= 编解码 =================
def build(msg_id, phone, term, serial, body):
    """按 jt808.c 的规则组一帧，返回转义后的完整字节"""
    raw = bytearray()
    raw.append(0x7E)
    raw += bytes([(msg_id >> 8) & 0xFF, msg_id & 0xFF])
    raw += bytes([(len(body) >> 8) & 0x03, len(body) & 0xFF])
    raw += phone
    raw += term
    raw += bytes([(serial >> 8) & 0xFF, serial & 0xFF])
    raw += bytes([0, 1])        # 总包数 = 1
    raw += bytes([0, 1])        # 包序号 = 1
    raw += body
    x = 0
    for b in raw[1:]:
        x ^= b
    raw.append(x)
    raw.append(0x7E)

    out = bytearray([0x7E])
    for b in raw[1:-1]:
        if b in (0x7E, 0x7D):
            out += bytes([0x7D, 0x02 if b == 0x7E else 0x01])
        else:
            out.append(b)
    out.append(0x7E)
    return bytes(out)


class Decoder(object):
    """流式解码：一个连接一个实例"""

    def __init__(self):
        self.reset()

    def reset(self):
        self.buf = bytearray()
        self.in_frame = False
        self.esc = False

    def feed(self, data):
        """喂入一段字节，返回本段里解出的所有帧 [(mid, phone, term, serial, body), ...]"""
        frames = []
        for c in data:
            if not self.in_frame:
                if c == 0x7E:
                    self.in_frame = True
                    self.buf = bytearray()
                    self.esc = False
                continue
            if self.esc:
                self.esc = False
                self.buf.append(0x7E if c == 0x02 else (0x7D if c == 0x01 else c))
                continue
            if c == 0x7D:
                self.esc = True
                continue
            if c == 0x7E:
                f = self._unpack()
                self.in_frame = False
                if f:
                    frames.append(f)
                continue
            self.buf.append(c)
        return frames

    def _unpack(self):
        if len(self.buf) < 23:
            return None
        blen = len(self.buf) - 23
        if (((self.buf[2] & 0x03) << 8) | self.buf[3]) != blen:
            return None
        x = 0
        for b in self.buf[:-1]:
            x ^= b
        if x != self.buf[-1]:
            return None
        mid = (self.buf[0] << 8) | self.buf[1]
        phone = bytes(self.buf[4:10])
        term = bytes(self.buf[10:16])
        serial = (self.buf[16] << 8) | self.buf[17]
        body = bytes(self.buf[22:22 + blen])
        return (mid, phone, term, serial, body)


# ================= 发送 =================
def send_to(sock, msg_id, phone, term, body):
    global tx_serial
    frame = build(msg_id, phone, term, tx_serial, body)
    tx_serial = (tx_serial + 1) & 0xFFFF
    try:
        sock.sendall(frame)
        print("  >> 已下发 0x%04X : %s" % (msg_id, frame.hex()))
        return True
    except OSError as e:
        print("  >> 下发失败: %s" % e)
        return False


def broadcast(msg_id, body):
    with clients_lock:
        if not clients:
            print("  (当前没有终端连接)")
            return
        for s in list(clients):
            phone, term = client_ids.get(s, (b"\x00" * 6, b"\x00" * 6))
            send_to(s, msg_id, phone, term, body)


# ================= 业务处理 =================
def on_auth_req(sock, phone, term, body):
    card = body[:4].hex().upper()
    code = body[4:].decode("ascii", "replace")
    print("  [身份验证请求] 卡号=%s  身份码=%s" % (card, code))
    if STRICT:
        ok = DRIVERS.get(card) == code
        print("  校验模式：白名单 -> %s" % ("准许" if ok else "拒绝"))
    else:
        ok = True
        print("  校验模式：全部放行")
    text = b"PASS" if ok else b"FAIL"
    send_to(sock, MSG_AUTH_RESP, phone, term, bytes([0 if ok else 1]) + text)


def on_location(body):
    if len(body) < 16:
        return
    lat = int.from_bytes(body[8:12], "big", signed=True) / 1000000.0
    lon = int.from_bytes(body[12:16], "big", signed=True) / 1000000.0
    print("  [位置汇报] 纬度=%.6f  经度=%.6f" % (lat, lon))


def handle_client(sock, addr):
    global rx_count
    print("\n[终端接入] %s:%d" % addr)
    with clients_lock:
        clients.append(sock)
    dec = Decoder()
    try:
        while True:
            data = sock.recv(1024)
            if not data:
                break
            rx_count += 1
            print("  << 收到 %d 字节: %s" % (len(data), data.hex()))
            for (mid, phone, term, serial, body) in dec.feed(data):
                with clients_lock:
                    client_ids[sock] = (phone, term)
                print("  [帧] 消息ID=0x%04X 终端=%s 流水号=%d 消息体=%s"
                      % (mid, term.hex().upper(), serial, body.hex()))
                if mid == MSG_AUTH_REQ:
                    on_auth_req(sock, phone, term, body)
                elif mid == MSG_LOCATION:
                    on_location(body)
                else:
                    print("  (未定义消息号，忽略)")
    except OSError as e:
        print("[终端断开] %s (%s)" % (addr, e))
    finally:
        with clients_lock:
            if sock in clients:
                clients.remove(sock)
            client_ids.pop(sock, None)
        try:
            sock.close()
        except OSError:
            pass
        print("[终端离线] %s:%d" % addr)


def stdin_loop():
    time.sleep(0.3)
    print("命令: help | list | fatigue 1 | fatigue 0 | strict 1 | strict 0 | quit")
    while True:
        try:
            line = input("platform> ").strip()
        except EOFError:
            return
        if not line:
            continue
        parts = line.split()
        cmd = parts[0].lower()

        if cmd in ("help", "?"):
            print("  help              显示本帮助")
            print("  list              列出已连接终端")
            print("  fatigue 1         下发疲劳驾驶=1（终端蜂鸣器报警）")
            print("  fatigue 0         下发疲劳驾驶=0（解除报警）")
            print("  strict 1          身份验证按白名单校验")
            print("  strict 0          身份验证全部放行（默认）")
            print("  quit              退出")
        elif cmd == "list":
            with clients_lock:
                print("  已连接终端数: %d" % len(clients))
        elif cmd == "fatigue":
            v = parts[1] if len(parts) > 1 else "1"
            broadcast(MSG_FATIGUE, bytes([1 if v == "1" else 0]))
        elif cmd == "strict":
            global STRICT
            STRICT = (len(parts) > 1 and parts[1] == "1")
            print("  白名单校验: %s" % ("开" if STRICT else "关"))
        elif cmd == "quit":
            print("  bye")
            import os
            os._exit(0)
        else:
            print("  未知命令，输入 help 查看")


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8888
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", port))
    srv.listen(4)
    print("模拟平台已启动，监听 0.0.0.0:%d" % port)
    print("请把终端 deploy/config 里的 server_ip 改成本机 IP，然后运行终端程序")

    threading.Thread(target=stdin_loop, daemon=True).start()

    while True:
        try:
            sock, addr = srv.accept()
        except KeyboardInterrupt:
            print("\nbye")
            break
        threading.Thread(target=handle_client, args=(sock, addr), daemon=True).start()


if __name__ == "__main__":
    main()

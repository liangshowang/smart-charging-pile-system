#!/usr/bin/env python3
"""
充电桩协议测试服务器 (Step 22 验证用)

模拟服务器端协议:
  - 接收客户端 LOGIN 并回复 Logon
  - 回复 PING → pong
  - 发送 on/off/time/jfpg/reboot 等命令
  - 接收 LOG over 并回复 over ACK

用法:
  python test_server.py [--port 9002] [--host 0.0.0.0]

协议格式 (纯文本, 每行 \r\n):
  客户端→服务器: LOGIN user pwd vision
  服务器→客户端: Logon
  客户端→服务器: PING 0 0 220
  服务器→客户端: pong
  服务器→客户端: on 1 30 TEST001
  客户端→服务器: LOG over TEST001 0 0 0 0 0 0 0 0
  服务器→客户端: over TEST001
"""

import socket
import sys
import time
import threading
import argparse
import re


class ChargingServer:
    """充电桩协议测试服务器"""

    def __init__(self, host='0.0.0.0', port=9002):
        self.host = host
        self.port = port
        self.sock = None
        self.client = None
        self.addr = None
        self.running = False
        self.logged_in = False

    def log(self, direction, msg):
        """带方向的日志输出"""
        arrow = '→' if direction == 'send' else '←'
        # 截断过长的消息
        display = msg.replace('\r', '\\r').replace('\n', '\\n')
        if len(display) > 100:
            display = display[:100] + '...'
        print(f"  [{arrow}] {display}")

    def send_line(self, line):
        """发送一行 (追加 \r\n)"""
        data = (line + '\r\n').encode('utf-8')
        if self.client:
            self.client.sendall(data)
            self.log('send', line)

    def recv_until_newline(self, timeout=60):
        """逐字节接收直到 \r\n"""
        buf = b''
        self.client.settimeout(timeout)
        try:
            while True:
                ch = self.client.recv(1)
                if not ch:
                    return None  # 连接断开
                buf += ch
                if buf.endswith(b'\r\n'):
                    break
        except socket.timeout:
            if buf:
                print(f"  [!] recv timeout, partial: {buf}")
            return None
        return buf.decode('utf-8').rstrip('\r\n')

    def handle_client(self):
        """客户端连接处理主循环"""
        print(f"\n=== 客户端已连接: {self.addr} ===\n")

        # 等待 LOGIN
        login_ok = False
        while not login_ok:
            line = self.recv_until_newline(timeout=30)
            if line is None:
                print("[!] 等待 LOGIN 超时, 断开连接")
                return

            self.log('recv', line)

            if line.startswith('LOGIN'):
                parts = line.split()
                if len(parts) >= 4:
                    print(f"  [+] 用户登录: {parts[1]}, 版本: {parts[3]}")
                    self.send_line('Logon')
                    login_ok = True
                else:
                    print(f"  [-] LOGIN 格式错误: {line}")
                    self.send_line('nologin')
                    return
            else:
                print(f"  [-] 首条消息不是 LOGIN: {line}")
                self.send_line('nologin')
                return

        print("\n=== 已登录, 进入协议交互 ===\n")

        # 发送初始数据
        time.sleep(1)
        self.send_line('time 20260101120000 50')
        time.sleep(0.5)
        self.send_line('jfpg 00:00-08:00 08:00-10:00 10:00-12:00 12:00-14:00 14:00-16:00 16:00-18:00 18:00-20:00 20:00-24:00 120')

        # 主循环: 收发协议
        self.running = True
        ping_count = 0
        last_recv = time.time()

        while self.running:
            try:
                line = self.recv_until_newline(timeout=5)
            except Exception as e:
                print(f"[!] recv error: {e}")
                break

            if line is None:
                # 检查是否长时间无数据
                if time.time() - last_recv > 60:
                    print("[!] 客户端 60s 无数据, 断开")
                    break
                continue

            last_recv = time.time()
            self.log('recv', line)

            parts = line.split()

            if not parts:
                continue

            cmd = parts[0].upper()

            # ---- 协议处理 ----
            if cmd == 'PING':
                ping_count += 1
                self.send_line('pong')
                if ping_count % 5 == 0:
                    print(f"  [·] PING #{ping_count}")

            elif cmd == 'GETTIME':
                now = time.strftime('%Y%m%d%H%M%S')
                checksum = sum(int(c) for c in now if c.isdigit())
                self.send_line(f'time {now} {checksum}')

            elif cmd == 'GETJFPG':
                # 简化: 8组时段
                self.send_line('jfpg 00:00-08:00 08:00-10:00 10:00-12:00 12:00-14:00 14:00-16:00 16:00-18:00 18:00-20:00 20:00-24:00 120')

            elif cmd == 'LOG':
                # LOG all: 电流数据, LOG over: 充电结束
                if len(parts) >= 3 and parts[1] == 'over':
                    ddh = parts[2]
                    print(f"  [+] 充电结束: DDH={ddh}")
                    time.sleep(0.3)
                    self.send_line(f'over {ddh}')

            else:
                print(f"  [?] 未知命令: {line}")

        print("\n=== 客户端断开 ===\n")

    def start(self):
        """启动服务器"""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((self.host, self.port))
        self.sock.listen(1)

        print(f"""
╔══════════════════════════════════════════╗
║   充电桩协议测试服务器                    ║
║   Step 22: Server Protocol Verification  ║
╠══════════════════════════════════════════╣
║   监听: {self.host}:{self.port}                       ║
║   协议: 纯文本, \\r\\n 分行                   ║
║                                        ║
║   支持命令:                              ║
║     客户端 → LOGIN / PING / GETTIME     ║
║     服务器 → Logon / pong / time / on   ║
║              off / over / reboot         ║
║                                        ║
║   交互命令 (服务器端输入):               ║
║     on <sock> <min> <ddh>  — 开始充电   ║
║     off <sock> <ddh>       — 停止充电   ║
║     time                   — 发送时间    ║
║     reboot                 — 远程复位    ║
║     over <ddh>             — 确认结束    ║
║     relogin                — 要求重登    ║
║     quit                   — 退出       ║
╚══════════════════════════════════════════╝
""")
        print("等待客户端连接...\n")

        while True:
            self.client, self.addr = self.sock.accept()
            self.logged_in = False

            # 在后台线程处理客户端
            client_thread = threading.Thread(target=self.handle_client, daemon=True)
            client_thread.start()

            # 主线程: 接收用户输入发送命令
            self.interactive_loop()
            break

    def interactive_loop(self):
        """交互式命令输入"""
        print("\n>>> 交互模式: 输入命令发送给客户端 <<<\n")

        while self.running:
            try:
                cmd = input("  server> ").strip()
            except (EOFError, KeyboardInterrupt):
                print("\n[!] 退出")
                break

            if not cmd:
                continue

            if cmd == 'quit':
                break
            elif cmd.startswith('on '):
                self.send_line(cmd)
            elif cmd.startswith('off '):
                self.send_line(cmd)
            elif cmd == 'time':
                now = time.strftime('%Y%m%d%H%M%S')
                checksum = sum(int(c) for c in now if c.isdigit())
                self.send_line(f'time {now} {checksum}')
            elif cmd == 'reboot':
                self.send_line('reboot')
            elif cmd.startswith('over '):
                self.send_line(cmd)
            elif cmd == 'relogin':
                self.send_line('relogin')
            elif cmd == 'help':
                print("  命令: on/off/time/reboot/over/relogin/quit")
            else:
                # 原样发送
                self.send_line(cmd)

        self.running = False
        if self.client:
            self.client.close()
        if self.sock:
            self.sock.close()
        print("服务器已关闭")


def main():
    parser = argparse.ArgumentParser(description='充电桩协议测试服务器')
    parser.add_argument('--port', type=int, default=9002, help='监听端口 (默认: 9002)')
    parser.add_argument('--host', default='0.0.0.0', help='监听地址 (默认: 0.0.0.0)')
    args = parser.parse_args()

    server = ChargingServer(host=args.host, port=args.port)
    server.start()


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""
test_server.py — 充电桩集成测试服务器

同时充当两个角色:
  1. TCP 充电协议服务器 (端口 9002) — Login/心跳/命令/事件
  2. HTTP OTA 固件服务器 (端口 80)   — Range 分块下载

用法:
  python test_server.py [--ota-dir <path>]

示例:
  python test_server.py --ota-dir ../tools/
  (把 crc16_embed.py 生成的 _ota.bin 放到 tools/ 目录)

协议格式 (文本协议, 换行分隔):
  上行 (充电桩→服务器):  @cmd:key=val|key=val|...\r\n
  下行 (服务器→充电桩):  #cmd:key=val|key=val|...\r\n

支持的命令:
  LOGIN    — 登录      @login:name=xxx|pwd=xxx|ver=xxx
  RELOGIN  — 重新登录
  PONG     — 心跳回复  @pong
  EVENT    — 事件上报  @event:type=START|...
  LOG      — 日志上报  @log:msg=xxx
"""

import socket
import threading
import sys
import os
import time
import re
from http.server import HTTPServer, BaseHTTPRequestHandler


# ================================================================
# 充电协议服务器 (端口 9002)
# ================================================================

class ChargeServer:
    """充电桩 TCP 协议服务器"""

    def __init__(self, host='0.0.0.0', port=9002):
        self.host = host
        self.port = port
        self.sock = None
        self.clients = []       # (conn, addr, state) 列表
        self.running = False

    def start(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((self.host, self.port))
        self.sock.listen(5)
        self.running = True
        print(f"[charge] 充电协议服务器启动: {self.host}:{self.port}")

        # 接收连接线程
        t = threading.Thread(target=self._accept_loop, daemon=True)
        t.start()

    def stop(self):
        self.running = False
        if self.sock:
            self.sock.close()

    def _accept_loop(self):
        while self.running:
            try:
                self.sock.settimeout(1.0)
                conn, addr = self.sock.accept()
                print(f"[charge] 新连接: {addr}")
                self.clients.append({'conn': conn, 'addr': addr, 'logged_in': False})
                t = threading.Thread(target=self._handle_client,
                                     args=(conn, addr), daemon=True)
                t.start()
            except socket.timeout:
                continue
            except Exception as e:
                if self.running:
                    print(f"[charge] accept 错误: {e}")

    def _handle_client(self, conn, addr):
        """处理单个客户端连接"""
        buf = b''
        conn.settimeout(60.0)  # 60s 无数据超时

        while self.running:
            try:
                data = conn.recv(1024)
                if not data:
                    print(f"[charge] {addr} 断开连接")
                    break

                buf += data

                # 解析以 \n 分隔的帧
                while b'\n' in buf:
                    line, buf = buf.split(b'\n', 1)
                    self._process_line(conn, addr, line.decode('utf-8', errors='replace').strip())

            except socket.timeout:
                print(f"[charge] {addr} 超时断开")
                break
            except Exception as e:
                print(f"[charge] {addr} 错误: {e}")
                break

        # 清理
        self.clients = [c for c in self.clients if c['addr'] != addr]
        try:
            conn.close()
        except:
            pass
        print(f"[charge] {addr} 已断开 (当前 {len(self.clients)} 个连接)")

    def _process_line(self, conn, addr, line):
        """处理一条协议帧"""
        if not line:
            return

        print(f"  ← {addr}: {line}")

        # 解析帧格式: @cmd:key=val|key=val|...
        match = re.match(r'@(\w+):?(.*)', line)
        if not match:
            return

        cmd = match.group(1).upper()
        params = match.group(2)

        # 解析 key=val 对
        args = {}
        if params:
            for pair in params.split('|'):
                if '=' in pair:
                    k, v = pair.split('=', 1)
                    args[k.strip()] = v.strip()

        # ---- 命令派发 ----
        if cmd == 'LOGIN':
            self._on_login(conn, addr, args)
        elif cmd == 'RELOGIN':
            self._on_relogin(conn, addr, args)
        elif cmd == 'PONG':
            pass  # 心跳响应, 不需要回复
        elif cmd == 'EVENT':
            self._on_event(conn, addr, args)
        elif cmd == 'LOG':
            pass  # 日志, 打印即可
        elif cmd == 'GETSTATU':
            self._on_getstatu(conn, addr, args)
        elif cmd == 'GETPARAM':
            self._on_getparam(conn, addr, args)
        else:
            print(f"  [未知命令] {cmd}")

    def _send(self, conn, cmd, **kwargs):
        """发送响应帧"""
        params = '|'.join(f"{k}={v}" for k, v in kwargs.items())
        if params:
            msg = f"#{cmd}:{params}\r\n"
        else:
            msg = f"#{cmd}\r\n"

        print(f"  → {msg.strip()}")
        try:
            conn.sendall(msg.encode('utf-8'))
        except Exception as e:
            print(f"  [发送失败] {e}")

    def _on_login(self, conn, addr, args):
        """登录 — 验证名称/密码/版本"""
        name = args.get('name', 'unknown')
        pwd = args.get('pwd', '')
        ver = args.get('ver', 'unknown')

        print(f"  [LOGIN] name={name} ver={ver}")

        # 简单验证 (实际项目按需修改)
        if name and pwd:
            self._send(conn, 'LOGON')
            # 更新登录状态
            for c in self.clients:
                if c['addr'] == addr:
                    c['logged_in'] = True
        else:
            self._send(conn, 'NOLOGIN', reason='invalid credentials')

    def _on_relogin(self, conn, addr, args):
        """重新登录"""
        self._send(conn, 'LOGON')

    def _on_event(self, conn, addr, args):
        """事件上报 — 记录并回复 OVER"""
        evt_type = args.get('type', 'unknown')
        print(f"  [EVENT] type={evt_type} params={args}")
        self._send(conn, 'OVER', type=evt_type)

    def _on_getstatu(self, conn, addr, args):
        """状态查询"""
        self._send(conn, 'STATU',
                   sock='0,0',      # 插座状态: 都空闲
                   net='1',         # 网络: 在线
                   ver='step27')    # 版本

    def _on_getparam(self, conn, addr, args):
        """参数查询"""
        self._send(conn, 'PARAM',
                   jf='0.5',        # 费率: 0.5元/度
                   time='60',       # 最大充电时间: 60分钟
                   maxpow='3500')   # 最大功率: 3500W


# ================================================================
# HTTP OTA 服务器 (端口 80)
# ================================================================

class OTAHttpHandler(BaseHTTPRequestHandler):
    """HTTP 请求处理 — 支持 Range 分块下载"""

    ota_dir = '.'  # 固件文件目录 (由启动参数设置)

    def log_message(self, format, *args):
        """重定向日志"""
        print(f"  [HTTP] {args[0]}")

    def do_GET(self):
        """处理 GET 请求 (支持 Range)"""
        path = self.path.split('?')[0]  # 去掉 query string
        filepath = os.path.join(self.ota_dir, path.lstrip('/'))

        if not os.path.exists(filepath) or not os.path.isfile(filepath):
            self.send_error(404, f"File not found: {path}")
            return

        file_size = os.path.getsize(filepath)
        range_header = self.headers.get('Range', '')

        if range_header.startswith('bytes='):
            # ---- Range 请求 ----
            range_spec = range_header[6:]  # 去掉 "bytes="
            ranges = []

            for part in range_spec.split(','):
                part = part.strip()
                if '-' in part:
                    start_str, end_str = part.split('-', 1)
                    start = int(start_str) if start_str else 0
                    end = int(end_str) if end_str else file_size - 1
                    ranges.append((start, min(end, file_size - 1)))

            if not ranges:
                self.send_error(416, "Range Not Satisfiable")
                return

            start, end = ranges[0]
            length = end - start + 1

            print(f"  [HTTP] Range: bytes={start}-{end} → {length} bytes")

            with open(filepath, 'rb') as f:
                f.seek(start)
                data = f.read(length)

            self.send_response(206)  # Partial Content
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Range', f'bytes {start}-{end}/{file_size}')
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Connection', 'keep-alive')
            self.end_headers()
            self.wfile.write(data)

        else:
            # ---- 完整文件请求 ----
            print(f"  [HTTP] GET {path} (full, {file_size} bytes)")

            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(file_size))
            self.end_headers()

            with open(filepath, 'rb') as f:
                self.wfile.write(f.read())


class OTAServer:
    """HTTP OTA 服务器包装"""

    def __init__(self, host='0.0.0.0', port=80, ota_dir='.'):
        self.host = host
        self.port = port
        self.ota_dir = ota_dir

    def start(self):
        OTAHttpHandler.ota_dir = self.ota_dir
        self.server = HTTPServer((self.host, self.port), OTAHttpHandler)
        print(f"[ota] HTTP OTA 服务器启动: {self.host}:{self.port}")
        print(f"[ota] 固件目录: {os.path.abspath(self.ota_dir)}")
        t = threading.Thread(target=self.server.serve_forever, daemon=True)
        t.start()

    def stop(self):
        if self.server:
            self.server.shutdown()


# ================================================================
# 主入口
# ================================================================

def main():
    import argparse

    parser = argparse.ArgumentParser(description='充电桩集成测试服务器')
    parser.add_argument('--ota-dir', default='.',
                        help='OTA 固件文件目录 (默认: 当前目录)')
    parser.add_argument('--charge-port', type=int, default=9002,
                        help='充电协议端口 (默认: 9002)')
    parser.add_argument('--ota-port', type=int, default=80,
                        help='HTTP OTA 端口 (默认: 80)')
    args = parser.parse_args()

    print("=" * 60)
    print("  智能充电桩 — 集成测试服务器")
    print("=" * 60)
    print()

    # 检查 OTA 目录
    ota_dir = os.path.abspath(args.ota_dir)
    if not os.path.isdir(ota_dir):
        print(f"警告: OTA 目录不存在: {ota_dir}")

    # 启动充电协议服务器
    charge_server = ChargeServer(port=args.charge_port)
    charge_server.start()

    # 启动 HTTP OTA 服务器
    ota_server = OTAServer(port=args.ota_port, ota_dir=ota_dir)
    try:
        ota_server.start()
    except PermissionError:
        print(f"[ota] 端口 {args.ota_port} 需要管理员权限, "
              f"请用 'python test_server.py --ota-port 8080'")
        print(f"[ota] 或修改 BL 代码 OTA_SERVER_PORT 为 8080")
        ota_server = None

    print()
    print("服务器运行中... 按 Ctrl+C 停止")
    print()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n停止服务器...")
        charge_server.stop()
        if ota_server:
            ota_server.stop()


if __name__ == '__main__':
    main()

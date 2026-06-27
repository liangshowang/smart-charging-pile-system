#!/usr/bin/env python3
"""
crc16_embed.py — 固件 CRC16 预处理工具

读取原始固件 .bin 文件，每 128 字节数据后插入 2 字节 CRC16-IBM，
输出与 BootLoader OTA 下载格式兼容的文件。

输出格式:
  [128B data][2B CRC16 LE][128B data][2B CRC16 LE]...

CRC16-IBM 参数:
  多项式: 0x8005 (reflected → 0xA001)
  初始值: 0x0000
  不反转输出

用法:
  python crc16_embed.py <input.bin> [output.bin]

示例:
  python crc16_embed.py step27_integration/app/app_firmware.bin
  → 生成 app_firmware_ota.bin (可直接放服务器 /zxx/ota/ 目录)
"""

import sys
import os


# ---- CRC16-IBM 查找表 (预计算, 加速) ----
def _make_crc16_table():
    """生成 CRC16-IBM 查找表 (多项式 0xA001)"""
    table = []
    for i in range(256):
        crc = i
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc = crc >> 1
        table.append(crc & 0xFFFF)
    return table


CRC16_TABLE = _make_crc16_table()


def crc16_ibm(data: bytes) -> int:
    """计算 CRC16-IBM (查表法, 快速)"""
    crc = 0x0000
    for byte in data:
        crc = (crc >> 8) ^ CRC16_TABLE[(crc ^ byte) & 0xFF]
    return crc & 0xFFFF


# ---- 核心逻辑 ----
CHUNK_DATA_SIZE = 128   # 每块数据大小
CHUNK_CRC_SIZE  = 2     # CRC 大小
CHUNK_TOTAL     = CHUNK_DATA_SIZE + CHUNK_CRC_SIZE  # 130


def embed_crc(input_path: str, output_path: str = None) -> str:
    """
    读取原始固件, 每 128 字节嵌入 2 字节 CRC16。

    参数:
        input_path  — 输入 .bin 文件路径
        output_path — 输出路径 (默认: input_ota.bin)

    返回:
        输出文件路径

    异常:
        FileNotFoundError — 输入文件不存在
        ValueError        — 输入文件为空
    """
    # ---- 读取原始固件 ----
    if not os.path.exists(input_path):
        raise FileNotFoundError(f"输入文件不存在: {input_path}")

    with open(input_path, 'rb') as f:
        raw_data = f.read()

    if len(raw_data) == 0:
        raise ValueError(f"输入文件为空: {input_path}")

    file_size = len(raw_data)
    print(f"[crc16_embed] 输入文件: {input_path}")
    print(f"[crc16_embed] 原始大小: {file_size} bytes ({file_size / 1024:.1f} KB)")

    # ---- 逐块嵌入 CRC ----
    chunks = []
    total_chunks = (file_size + CHUNK_DATA_SIZE - 1) // CHUNK_DATA_SIZE

    for i in range(total_chunks):
        start = i * CHUNK_DATA_SIZE
        end = min(start + CHUNK_DATA_SIZE, file_size)
        chunk = raw_data[start:end]

        # 最后一块不足 128 字节时补 0xFF (Flash 擦除后的默认值)
        if len(chunk) < CHUNK_DATA_SIZE:
            chunk = chunk + b'\xFF' * (CHUNK_DATA_SIZE - len(chunk))

        crc = crc16_ibm(chunk)
        crc_bytes = bytes([crc & 0xFF, (crc >> 8) & 0xFF])  # little-endian

        chunks.append(chunk + crc_bytes)

        if (i + 1) % 100 == 0 or i == total_chunks - 1:
            pct = (i + 1) * 100 // total_chunks
            print(f"  处理中... {i + 1}/{total_chunks} 块 ({pct}%)")

    # ---- 写入输出 ----
    if output_path is None:
        base, ext = os.path.splitext(input_path)
        output_path = f"{base}_ota{ext}"

    with open(output_path, 'wb') as f:
        for chunk in chunks:
            f.write(chunk)

    output_size = len(chunks) * CHUNK_TOTAL
    overhead = output_size - file_size
    print(f"[crc16_embed] 输出文件: {output_path}")
    print(f"[crc16_embed] 输出大小: {output_size} bytes ({output_size / 1024:.1f} KB)")
    print(f"[crc16_embed] CRC 开销: {overhead} bytes ({overhead / file_size * 100:.1f}%)")
    print(f"[crc16_embed] 总块数:   {total_chunks}")
    print(f"[crc16_embed] 完成!")

    return output_path


# ---- 验证 (用于调试) ----
def verify_ota_file(ota_path: str) -> bool:
    """
    验证 OTA 文件的 CRC 正确性。

    读取嵌入 CRC 后的文件, 逐块验证 CRC16, 报告错误。
    """
    if not os.path.exists(ota_path):
        print(f"[verify] 文件不存在: {ota_path}")
        return False

    with open(ota_path, 'rb') as f:
        data = f.read()

    total_chunks = len(data) // CHUNK_TOTAL
    errors = 0

    for i in range(total_chunks):
        offset = i * CHUNK_TOTAL
        chunk_data = data[offset:offset + CHUNK_DATA_SIZE]
        crc_stored = data[offset + CHUNK_DATA_SIZE]
        crc_stored |= data[offset + CHUNK_DATA_SIZE + 1] << 8

        crc_calc = crc16_ibm(chunk_data)

        if crc_calc != crc_stored:
            print(f"  [FAIL] 块 {i}: calc=0x{crc_calc:04X} "
                  f"stored=0x{crc_stored:04X}")
            errors += 1

    if errors == 0:
        print(f"[verify] 全部 {total_chunks} 块 CRC 校验通过 [OK]")
        return True
    else:
        print(f"[verify] {errors}/{total_chunks} 块 CRC 校验失败 [FAIL]")
        return False


# ---- 命令行入口 ----
if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("用法: python crc16_embed.py <input.bin> [output.bin]")
        print("示例: python crc16_embed.py firmware.bin firmware_ota.bin")
        print()
        print("选项:")
        print("  --verify <ota.bin>  验证已有 OTA 文件的 CRC")
        sys.exit(1)

    if sys.argv[1] == '--verify':
        if len(sys.argv) < 3:
            print("用法: python crc16_embed.py --verify <ota.bin>")
            sys.exit(1)
        ok = verify_ota_file(sys.argv[2])
        sys.exit(0 if ok else 1)

    input_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else None

    try:
        result = embed_crc(input_file, output_file)
        # 自动验证
        verify_ota_file(result)
    except (FileNotFoundError, ValueError) as e:
        print(f"错误: {e}")
        sys.exit(1)

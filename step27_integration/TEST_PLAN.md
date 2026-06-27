# Step 27: 全系统集成测试

## 目录结构

```
step27_integration/
├── TEST_PLAN.md          ← 本文件
├── tools/
│   ├── crc16_embed.py    ← 固件 CRC 预处理工具
│   └── test_server.py    ← 集成测试服务器 (充电协议 + HTTP OTA)
├── bootloader/           ← BL Keil 工程 (复制自 step26)
└── app/                  ← APP Keil 工程 (复制自 step26)
```

---

## 一、编译验证

### 1.1 BootLoader 工程

```
打开: bootloader/step26_bl.uvprojx
操作: F7 编译
期望:
  [ ] 0 Error, 0 Warning
  [ ] BL hex 大小 < 64KB (0x10000)
  [ ] .\out\step26_bl.hex 生成
```

### 1.2 APP 工程

```
打开: app/step15_hc595.uvprojx
操作: F7 编译
期望:
  [ ] 0 Error, 0 Warning
  [ ] APP hex 起始地址 = 0x11800
  [ ] .\out\step15_hc595.hex 生成
```

---

## 二、固件预处理

### 2.1 CRC 嵌入

```bash
# 生成 APP 的 .bin 文件 (从 Keil 工程的 .hex 用 fromelf 转换)
# 或直接用 Keil 的 fromelf: fromelf --bin --output=app.bin step15_hc595.axf

# 嵌入 CRC16
python tools/crc16_embed.py app.bin app_ota.bin

# 验证
python tools/crc16_embed.py --verify app_ota.bin
```

期望:
  [ ] 输出总块数正确 (文件大小 / 128 向上取整)
  [ ] CRC 开销约 1.5%
  [ ] `--verify` 全部通过

---

## 三、烧录与首次启动

### 3.1 烧录 BL

```
工具: J-Link / J-Flash
操作:
  1. 擦除整片 Flash
  2. 烧录 step26_bl.hex 到 0x00000000
```

### 3.2 烧录 APP

```
工具: J-Link / J-Flash
操作:
  1. 烧录 APP hex 到 0x00011800
```

### 3.3 首次上电

```
操作: 上电, SecureCRT (115200-8-N-1) 观察 UART0 输出
期望:
  [ ] BL 打印 banner "BootLoader v2.0"
  [ ] BL 打印 "boot_app @0x00010004 = ..."
  [ ] BL 打印 "CONF not initialized, jumping to APP..."
  [ ] APP 启动, 打印 "Step 25: BootLoader + Flash Partition"
  [ ] APP 打印 "boot_config_t written (boot_app=1 @0x00010000)"
  [ ] 网络状态机启动
```

### 3.4 再次上电 (验证 CONF 持久化)

```
操作: 断电 5 秒 → 重新上电
期望:
  [ ] BL 打印 "boot_app = _APP_FAC, jumping to APP..."
  [ ] APP 打印 "boot_config_t already valid (boot_app=1)"
  [ ] 系统正常启动
```

---

## 四、硬件初始化测试

### 4.1 GPIO 初始化

```
操作: 上电后观察
期望:
  [ ] LED SLED 闪烁 (充电动画)
  [ ] 继电器 ELC0/ELC1 初始断开 (常开)
  [ ] NetRst 为高电平
```

### 4.2 串口命令行

```
操作: SecureCRT 输入 help
期望:
  [ ] 列出所有命令: test, clear, help, reboot, led, relay, io, info, ota

操作: led on → led off
  [ ] SLED 亮 → 灭

操作: relay 0 on → relay 0 off
  [ ] ELC0 继电器吸合 → 断开

操作: info
  [ ] 打印 CPU 频率、Tick 计数、UART 配置
```

---

## 五、4G 网络测试

### 5.1 模块启动

```
操作: 上电, 插入 SIM 卡
期望:
  [ ] sysprt 日志: "AIR724 device created"
  [ ] net_man 进入 RESET 阶段
  [ ] NetRst 脉冲后等待 45s
  [ ] AT 握手成功 (AT → OK)
```

### 5.2 网络初始化

```
期望:
  [ ] CGMI → OK
  [ ] CGMR → OK
  [ ] CPIN? → READY
  [ ] CSQ → 信号值 (0~31)
  [ ] CREG? → 已注册
  [ ] CGATT? → 已附着
  [ ] CIPMODE → OK
  [ ] CIPMUX → OK
  [ ] CSTT → OK
  [ ] CIICR → 获取 IP 成功
  [ ] CIFSR → 显示 IP 地址
```

### 5.3 TCP 连接服务器

```
前提: PC 运行 python tools/test_server.py
期望:
  [ ] net_man Phase 2: CIPSTART → CONNECT OK
  [ ] net_man Phase 3: LOGIN → 收到 #LOGON
  [ ] net_man Phase 4: READY, 网络灯亮
```

---

## 六、服务器协议测试

### 6.1 心跳保活

```
操作: 系统就绪后观察
期望:
  [ ] 每 10s 发送 @PING
  [ ] 服务器回复 #PONG
  [ ] 30s 无数据触发断线重连
```

### 6.2 时间同步

```
操作: 服务器发送 #TIME:time=20260627120000
期望:
  [ ] APP 打印 "RTC synced: 2026-06-27 12:00:00"
```

### 6.3 费率下发

```
操作: 服务器发送 #JFPG:jf=0.5|time=60
期望:
  [ ] APP 更新费率配置 (0.5元/度, 最长60分钟)
```

### 6.4 状态查询

```
操作: 服务器发送 #GETSTATU
期望:
  [ ] APP 回复 @STATU:sock=0,0|net=1|...
```

### 6.5 事件上报

```
操作: 触发任意事件 (如保险丝故障)
期望:
  [ ] APP 发送 @EVENT:type=FAULT_FUSE|...
  [ ] 服务器回复 #OVER:type=FAULT_FUSE
  [ ] 事件池重试机制: 失败自动重试
```

---

## 七、充电业务测试

### 7.1 开/关继电器

```
操作: 服务器发送 #ON:sock=0
期望:
  [ ] ELC0 继电器吸合
  [ ] HLW8012 开始计量
  [ ] APP 回复 @EVENT:type=START|sock=0

操作: 服务器发送 #OFF:sock=0
期望:
  [ ] ELC0 继电器断开
  [ ] HLW8012 停止计量
  [ ] APP 回复 @EVENT:type=STOP|sock=0|energy=xxx
```

### 7.2 安全保护

```
操作: 充电过程中断开保险丝
期望:
  [ ] 200ms 内检测到保险丝异常
  [ ] 自动断开继电器
  [ ] APP 上报 @EVENT:type=FAULT_FUSE

操作: 充电超过设定时间
期望:
  [ ] APP 自动断开继电器
  [ ] APP 上报 @EVENT:type=FINISH|reason=timeout
```

---

## 八、FlashDB 持久化测试

### 8.1 配置读写

```
操作: 写入校准系数, 断电 10s, 重新上电
期望:
  [ ] 重新上电后 HLW8012 读数使用上次校准系数
  [ ] 误差 < 5%
```

### 8.2 CONF 分区

```
操作: BL 跳转 APP 后, 在 APP 中读 CONF
期望:
  [ ] boot_code == 0xA5
  [ ] boot_app == 1 (_APP_FAC)
```

---

## 九、OTA 升级测试

### 9.1 触发 OTA

```
操作: SecureCRT 输入 ota
期望:
  [ ] APP 打印 "Setting OTA mode and rebooting..."
  [ ] APP 写入 boot_app = 0 (_APP_UPDATE)
  [ ] 500ms 后复位

操作: 上电后观察
  [ ] BL 打印 "boot_app = _APP_UPDATE"
  [ ] BL 打印 "OTA mode requested, initializing FreeRTOS..."
  [ ] BL banner: "BootLoader v2.0 (OTA mode)"
```

### 9.2 OTA 网络初始化

```
期望:
  [ ] PHASE_INIT: 构建 URL
  [ ] PHASE_RESET: 等 45s → AT 握手 OK
  [ ] PHASE_INIT_DEV: 12 条 AT 命令全部 OK
  [ ] PHASE_CONNECT: TCP 连接 OTA 服务器 :80
```

### 9.3 OTA 下载

```
前提: 
  1. PC 运行 python tools/test_server.py --ota-dir tools/
  2. app_ota.bin 在 tools/ 目录

期望:
  [ ] PHASE_GET_SIZE: 获取文件大小, 解析正确
  [ ] APP 分区擦除完成 (47 个 4KB 扇区)
  [ ] PHASE_DOWNLOAD: 开始分块下载
  [ ] 每 10% 打印进度
  [ ] 每块 CRC16 验证通过
  [ ] PHASE_FINISH: 关 TCP, 写 boot_app = _APP_FAC
  [ ] 1s 后复位
```

### 9.4 OTA 后启动

```
期望:
  [ ] BL 读 boot_app = _APP_FAC
  [ ] BL 跳转 APP
  [ ] APP 启动 (新版固件)
  [ ] 功能正常
```

### 9.5 OTA 异常恢复

```
测试场景 1: OTA 下载到 50% 时断电
  [ ] 重新上电 → BL 检测到 app 区已部分擦除
  [ ] BL 重新进入 OTA 模式
  [ ] 重新下载 → 成功

测试场景 2: 网络断开 1 分钟
  [ ] 超时后 BL 重新 AT 初始化
  [ ] 重连服务器 → 继续下载

测试场景 3: 单块 CRC 校验连续失败 3 次
  [ ] 关 TCP 重连
  [ ] 重新请求该块
```

---

## 十、压力/稳定性测试

### 10.1 长时间运行

```
操作: 系统通电运行 24 小时
期望:
  [ ] 无死机
  [ ] 无内存泄漏 (FreeRTOS xPortGetFreeHeapSize 稳定)
  [ ] 心跳不中断
  [ ] 事件不丢失
```

### 10.2 电源循环

```
操作: 每隔 30 秒断电/上电, 循环 50 次
期望:
  [ ] 每次都能正常启动
  [ ] BL → APP 跳转 50 次全部成功
  [ ] FlashDB 配置不丢失
```

### 10.3 并发插座

```
操作: 两个插座同时充电
期望:
  [ ] 两个 HLW8012 读数独立
  [ ] 两个继电器独立控制
  [ ] 事件上报不混淆
```

---

## 十一、验证清单汇总

| 类别 | 测试项 | 状态 |
|------|--------|------|
| 编译 | BL 0 Error 0 Warning, hex < 64KB | [ ] |
| 编译 | APP 0 Error 0 Warning | [ ] |
| 烧录 | BL + APP 烧录成功 | [ ] |
| 启动 | 首次启动 APP 写 CONF | [ ] |
| 启动 | 再次启动 BL 跳转 APP | [ ] |
| 硬件 | LED/继电器/IO 正常 | [ ] |
| 命令行 | help/led/relay/info/io/test | [ ] |
| 4G | 模块启动 + AT 初始化 | [ ] |
| 4G | TCP 连接服务器 | [ ] |
| 协议 | 登录/心跳/时间同步/费率 | [ ] |
| 充电 | 开/关继电器 + 计量 | [ ] |
| 充电 | 保险丝保护 | [ ] |
| 充电 | 超时保护 | [ ] |
| 持久化 | FlashDB 断电不丢 | [ ] |
| OTA | 触发 OTA → 下载 → 复位 | [ ] |
| OTA | 下载中恢复 (断网/断电) | [ ] |
| OTA | CRC 校验失败重试 | [ ] |
| 稳定性 | 24h 无死机 | [ ] |
| 稳定性 | 50 次电源循环 | [ ] |

---

## 附录: 快速启动命令

```bash
# 1. 启动测试服务器
cd step27_integration/tools
python test_server.py --ota-dir . --ota-port 8080

# 2. 预处理固件
python crc16_embed.py ../app/app_firmware.bin ../app/app_firmware_ota.bin

# 3. 验证 OTA 文件
python crc16_embed.py --verify ../app/app_firmware_ota.bin

# 4. SecureCRT 连接
#    端口: UART0 (debug console)
#    波特率: 115200-8-N-1
```

---

> **提示**: 每个 [ ] 在测试通过后勾选。测试前确保 SIM 卡已插入、天线已连接、服务器已启动。

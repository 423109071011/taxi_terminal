# 出租车智能定位终端 —— 验收 / 验证步骤

- 日期：2026-09-16
- 对应设计文档 §13「验收标准」
- 环境：FS6818（NFS 启动）＋ Ubuntu 交叉编译 ＋ PC/Ubuntu 模拟平台

---

## 一、编译（Ubuntu）

```bash
# 1. 拷到 Linux 本地目录（不要在共享目录里编译）
mkdir -p ~/workdir
cp -r /mnt/hgfs/share/taxi_terminal ~/workdir/

# 2. 编译
cd ~/workdir/taxi_terminal/terminal
make

# 3. 确认产物是 ARM 静态可执行文件
file taxi_terminal
```

**通过标准**：`file` 输出含 `ELF 32-bit LSB executable, ARM, ... statically linked`。

---

## 二、部署到开发板（NFS 根文件系统）

Ubuntu 上：

```bash
sudo mkdir -p /source/rootfs/app_taxi/deploy

# 可执行文件 + 配置 + 脚本 + 历史路径文件
sudo cp ~/workdir/taxi_terminal/terminal/taxi_terminal          /source/rootfs/app_taxi/
sudo cp ~/workdir/taxi_terminal/terminal/deploy/config           /source/rootfs/app_taxi/deploy/
sudo cp ~/workdir/taxi_terminal/terminal/deploy/history_path.txt /source/rootfs/app_taxi/deploy/
sudo cp ~/workdir/taxi_terminal/terminal/deploy/bt.sh            /source/rootfs/app_taxi/
```

再放 5 个驱动模块（放到 `/app_taxi/` 同目录，并把 `bt.sh` 里的 `/xxx.ko` 改成 `./xxx.ko`）：

| 模块 | 来源 |
|---|---|
| `key-zlg72128.ko` | 课时 19（或 `PPT配套资源/demo.key1/`）|
| `rfid_driver.ko` | `PPT配套资源/demo.key1/` |
| `servo.ko` | `PPT配套资源/demo.key1/` |
| `fs6818_led.ko` | 课时 14 编译产物 |
| `beep_driver.ko` | 课时 15 编译产物 |

```bash
cd /source/rootfs/app_taxi
sed -i 's|insmod /|insmod ./|' bt.sh    # 改成同目录，避免污染板子根目录
cat bt.sh
```

---

## 三、板子上启动

```bash
cd /app_taxi
sh bt.sh                                  # 加载驱动
ls /dev/input/event4 /dev/rfid_module0 /dev/servo /dev/beep /dev/led
./taxi_terminal deploy/config
```

**通过标准**：打印 `init done. 输入 ? 或 help 查看命令`，并出现 `taxi_terminal>` 提示符。

> `bt.sh` 报 `File exists` 说明模块之前已加载，不影响，继续。

---

## 四、逐项验收

### A. 命令行（对应 §13 第 5 条）

| 操作 | 期望 |
|---|---|
| `help` | 打印 `? / help`、`info`、`uppath` 三行 |
| `info` | 打印 net/door/verified/fatigue + gps + card |

### B. 键盘与显示（对应 §13 第 6 条）

| 操作 | 期望 |
|---|---|
| 按数字键 | 数码管显示输入的数字，控制台打印 `[身份码输入] 1` `12` `123`… |
| 按翻页键 | 数码管切到下一项，控制台同时打印该项**名称 + 数值** |
| 10 秒不按键 | 数码管每 3 秒自动切换一项，**控制台无任何输出** |

### C. 车门（对应 §13 第 2 条）

| 操作 | 期望 |
|---|---|
| 刷 RFID 卡 | 控制台 `[刷卡] 卡号 xxxxxxxx，车门打开`，舵机转到 90° |
| 按关门键 | 控制台 `[车门] 关闭`，舵机回到 0° |
| 刷卡后 `info` | `door_open=1` |

### D. 身份验证（对应 §13 第 3 条）

前置：模拟平台已启动，`deploy/config` 的 `server_ip` 指向平台所在机器。

| 操作 | 期望 |
|---|---|
| 键盘输入 6 位身份码 → 按确认键 | 终端发 `0x0210`；平台打印卡号 + 身份码 |
| 平台回 `0x8110`（结果 0） | 数码管显示 `PASS`，蜂鸣器短响一声，`info` 里 `verified=1` |
| 平台回 `0x8110`（结果 1） | 数码管显示 `FAIL`，`info` 里 `verified=-1` |

### E. 疲劳报警（对应 §13 第 4 条）

| 操作 | 期望 |
|---|---|
| 平台执行 `fatigue 1` | 蜂鸣器开始周期报警（响 300ms / 停 300ms），`info` 里 `fatigue=1` |
| 平台执行 `fatigue 0` | 蜂鸣器停止，`fatigue=0` |

### F. 历史路径上传（对应 §13 第 5 条 uppath）

```bash
uppath deploy/history_path.txt
```

**通过标准**：终端打印 `uppath deploy/history_path.txt done`；平台连续打印多帧 `[位置汇报]`，经纬度递增。

### G. 状态指示灯（对应 §13 第 7 条）

| 阶段 | 时序 |
|---|---|
| 启动 | 绿 200ms 亮 / 300ms 灭 |
| 正常运行 | 绿 200ms 亮 / 1s 灭 |
| 报警（疲劳）| 红 100/灭 50 ×2 → 绿 100/灭 50 ×2 → 蓝 100/灭 50 ×2 → 蓝 100/灭 350 |
| 报警且无网络 | 红 300/灭 200 → 绿 300/灭 200 → 蓝 300/灭 200 |

### H. 平台端（对应 §13 第 8 条）

| 操作 | 期望 |
|---|---|
| 平台 `list` | 显示已连接终端数 |
| 平台 `strict 1` + 正确卡号/身份码 | 终端显示 `PASS` |
| 平台 `strict 1` + 错误身份码 | 终端显示 `FAIL` |

---

## 五、暂时做不到 / 已知差异

1. **设计文档 §13 第 8 条的“多终端管理 + 数据库/HTTP 转发”未实现**。当前 `platform/src/fake_platform.py` 只做基础收发与指令下发，`hook.c` 预留接口为空。
2. **开门逻辑与课程 docx 措辞不同**。docx 写“平台验证正确后下发准许指令”再开门；本实现按设计文档 §3.1 的约定为**刷卡即开门，平台准许仅作提示**。答辩时按设计文档解释即可。
3. **键盘物理键 ↔ key.code 需真机标定**。如翻页/关门/确认键不对，用 `tools/key_dump` 标定后改 `src/common/keymap.c`。

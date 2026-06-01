# OpenMV_Control — 基于人脸识别的物联网控制系统

## 项目简介

本项目通过摄像头实时检测和识别人脸，根据识别结果通过 MQTT 协议发送控制指令，人脸特征数据持久化存储在阿里云 MySQL 数据库中。

核心场景：识别特定人员是否在场，并通过 MQTT 消息通知其他设备做出响应。

## 功能特性

- **实时人脸检测**：基于 YuNet 模型，检测画面中的所有人脸
- **人脸识别**：基于 FaceRecognizerSF，提取128维特征向量进行身份比对
- **人脸注册**：按 R 键随时注册新人脸，数据同步写入 MySQL 数据库
- **MQTT 通信**：识别到目标用户上线/离线时，通过 MQTT 发送通知
- **数据持久化**：人脸特征向量存储在远程 MySQL 数据库，程序重启后自动加载

## 技术栈

| 技术 | 版本 | 用途 |
|------|------|------|
| OpenCV | 4.5+ | 人脸检测（YuNet）、人脸识别（FaceRecognizerSF） |
| paho-mqtt-c | - | MQTT 协议通信 |
| MySQL | 8.0+ | 人脸特征数据持久化存储 |
| CMake | 3.16+ | 构建管理 |

## 项目结构

```
opencv_demo1/
├── CMakeLists.txt                  # CMake 构建配置
├── main.cpp                        # 主程序源码
├── init_db.sql                     # MySQL 数据库初始化脚本
├── models/
│   ├── yunet.onnx                  # 人脸检测模型
│   └── face_recognizer_fast.onnx   # 人脸识别模型
├── photos/                         # 拍照保存目录
└── build/                          # 构建输出目录
```

## 环境要求

- 操作系统：Linux
- 编译器：支持 C++11 及以上
- 摄像头：任意 USB 摄像头或内置摄像头

## 安装依赖

### Ubuntu/Debian

```bash
# OpenCV（需含 DNN 模块）
# 如果从源码编译 OpenCV，请确保启用 DNN 模块

# paho-mqtt-c
sudo apt install libpaho-mqtt-dev

# MySQL 客户端开发库
sudo apt install libmysqlclient-dev

# CMake
sudo apt install cmake
```

## 数据库配置

### 1. 在阿里云服务器上配置 MySQL

确保 MySQL 允许远程连接：

```bash
# 编辑 MySQL 配置
sudo vim /etc/mysql/mysql.conf.d/mysqld.cnf
# 将 bind-address = 127.0.0.1 改为 bind-address = 0.0.0.0

# 重启 MySQL
sudo systemctl restart mysql
```

### 2. 创建数据库用户

```sql
-- 创建允许远程连接的用户
CREATE USER 'Ww'@'%' IDENTIFIED BY '123456';

-- 授予权限
GRANT ALL PRIVILEGES ON OpenMVdb.* TO 'Ww'@'%';
FLUSH PRIVILEGES;
```

### 3. 执行初始化脚本

```bash
mysql -h 39.104.71.92 -u Ww -p123456 < init_db.sql
```

该脚本会创建 `OpenMVdb` 数据库和 `face_users` 表：

| 字段 | 类型 | 说明 |
|------|------|------|
| id | INT AUTO_INCREMENT | 主键 |
| user_id | VARCHAR(50) UNIQUE | 用户ID |
| feature_vector | BLOB | 128维人脸特征向量（512字节） |
| created_at | TIMESTAMP | 注册时间 |

### 4. 阿里云安全组

确保安全组放行以下端口：

- **3306**：MySQL
- **1883**：MQTT

## 编译与运行

```bash
# 进入构建目录
cd opencv_demo1/build

# 生成构建文件
cmake ..

# 编译
make

# 运行
./demo1
```

## 操作说明

| 按键 | 功能 |
|------|------|
| R | 注册新人脸 |
| 空格 | 拍照保存到 photos/ 目录 |
| Q | 退出程序 |

### 人脸注册流程

1. 按 R 键进入注册模式
2. 在终端输入用户 ID
3. 正对摄像头，确保画面中只有一个人脸
4. 按空格拍照完成注册
5. 按 ESC 取消注册

### MQTT 消息协议

| 消息 | 含义 | 触发条件 |
|------|------|----------|
| a | 目标用户出现 | 识别到用户 "Ww" 且之前不在场 |
| b | 目标用户离开 | 用户 "Ww" 消失超过10秒 |

- Broker 地址：`tcp://39.104.71.92:1883`
- 主题：`wengyuanhang/cmd`
- QoS：1

## 系统架构

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│   摄像头      │────▶│  人脸检测     │────▶│  人脸识别     │
│  (VideoCapture)│    │  (YuNet)     │     │(FaceRecognizer)│
└──────────────┘     └──────────────┘     └──────┬───────┘
                                                  │
                                    ┌─────────────┼─────────────┐
                                    ▼             ▼             ▼
                              ┌──────────┐  ┌──────────┐  ┌──────────┐
                              │ 内存数据库 │  │ MySQL    │  │  MQTT    │
                              │(faceDatabase)│ │(持久化)  │  │ (通知)   │
                              └──────────┘  └──────────┘  └──────────┘
```

## 常见问题

### 摄像头打开失败

确认摄像头设备是否被其他程序占用，或尝试修改 `VideoCapture camera(0)` 中的设备编号。

### MySQL 连接失败

1. 检查阿里云安全组是否放行 3306 端口
2. 检查 MySQL 配置中 `bind-address` 是否为 `0.0.0.0`
3. 检查用户是否具有远程连接权限（`'Ww'@'%'`）

### 编译错误：找不到 mysql.h

```bash
sudo apt install libmysqlclient-dev
```

### 编译错误：找不到 MQTTClient.h

```bash
sudo apt install libpaho-mqtt-dev
```

# PicoW-NIC v1.0 仕様書・取扱説明書

## 概要

Raspberry Pi Pico W を USB-WiFi ブリッジ NIC として機能させるファームウェア。  
USB 側の PC と WiFi AP に接続したデバイス間で、双方向の IP 通信（ping / SSH / curl など）を実現する。

```
[Linux/Windows PC]──USB(CDC-NCM)──[Pico W]──WiFi(AP)──[Mac/スマホ etc.]
     10.0.0.2                      10.0.0.1              192.168.4.10
                                  192.168.4.1
```

---

## ネットワーク仕様

| 項目 | 値 |
|------|-----|
| USB サブネット | `10.0.0.0/24` |
| Pico USB 側 IP | `10.0.0.1` |
| USB クライアント IP | `10.0.0.2`（DHCP 固定） |
| WiFi SSID | `PicoBridge` |
| WiFi パスワード | `picobridge123` |
| WiFi チャンネル | 6 |
| WiFi サブネット | `192.168.4.0/24` |
| Pico WiFi 側 IP | `192.168.4.1` |
| WiFi クライアント IP | `192.168.4.10`（DHCP 固定） |

### ルーティング自動設定

USB クライアントには DHCP オプション 121（Classless Static Route, RFC 3442）で  
`192.168.4.0/24 via 10.0.0.1` が自動配布される。  
Linux では再起動後も含め、手動 `ip route add` 不要。

---

## 対応 OS / 動作確認環境

| 接続方式 | 動作確認 OS |
|---------|------------|
| USB (CDC-NCM) | Linux (Ubuntu 22.04 / 24.04) |
| WiFi | macOS 14 (Sonoma) |
| USB (CDC-NCM) | Windows 10/11 ※ RNDIS ドライバが必要な場合あり |

---

## ビルド環境

| 項目 | バージョン |
|------|-----------|
| Pico SDK | 2.x |
| TinyUSB | 0.15+ (Pico SDK 同梱) |
| lwIP | Pico SDK 同梱 |
| コンパイラ | arm-none-eabi-gcc |
| CMake | 3.13 以上 |

### ビルド手順

```bash
git clone <this-repo>
cd PicoW-NIC

# ビルドディレクトリを作成
mkdir build && cd build

# PICO_SDK_PATH を環境変数にセット済みであること
cmake .. -DPICO_BOARD=pico_w
make -j4
```

`build/picow_nic.uf2` が生成される。

### フラッシュ手順

```bash
# BOOTSEL ボタンを押しながら Pico を USB に接続
# RPI-RP2 ドライブが現れたらコピー
cp build/picow_nic.uf2 /Volumes/RPI-RP2/
```

---

## 接続手順

### USB 側（Linux）

1. Pico を USB に接続
2. `ip addr` で `picow` または `usb0` 系インターフェースが `10.0.0.2` を取得していることを確認
3. `ip route` で `192.168.4.0/24 via 10.0.0.1` が自動設定されていることを確認

```bash
ip addr show
# → inet 10.0.0.2/24

ip route | grep 192.168.4
# → 192.168.4.0/24 via 10.0.0.1 dev picow proto dhcp
```

### WiFi 側（Mac / スマホ）

1. WiFi AP `PicoBridge` に接続（パスワード: `picobridge123`）
2. `192.168.4.10` が DHCP で割り当てられることを確認
3. USB 側 PC への疎通確認：`ping 10.0.0.2`

### 手動ルート設定（WiFi 側が USB 側に接続する場合）

WiFi 側デバイスから USB 側（`10.0.0.x`）へ到達するにはルートが必要：

```bash
# macOS
sudo route add -net 10.0.0.0/24 192.168.4.1

# Linux
sudo ip route add 10.0.0.0/24 via 192.168.4.1

# Windows
route add 10.0.0.0 mask 255.255.255.0 192.168.4.1
```

---

## ソースファイル構成

```
PicoW-NIC/
├── CMakeLists.txt          ビルド設定
├── lwipopts.h              lwIP オプション（チェックサム、NAPT hook など）
├── tusb_config.h           TinyUSB オプション（CDC-NCM + CDC-ACM）
└── src/
    ├── main.c              エントリーポイント、メインループ
    ├── usb_net.c/h         USB CDC-NCM netif、ソフトウェアチェックサム
    ├── wifi_ap.c/h         CYW43 WiFi AP モード
    ├── network.c/h         WiFi TX チェックサムラッパー、初期化
    ├── nat.c/h             NAPT テーブル（TCP/UDP/ICMP 対応）
    ├── dhcpserver.c/h      固定 IP DHCP サーバー（オプション 121 対応）
    ├── usb_descriptors.c/h USB 複合デバイス記述子（NCM + CDC-ACM）
    └── stdio_cdc.c         printf → USB CDC-ACM（デバッグコンソール）
```

---

## デバッグ出力

### デバッグコンソール接続

USB 接続後、CDC-ACM (`/dev/ttyACM0` など) をターミナルで開く：

```bash
screen /dev/ttyACM0 115200
# または
picocom -b 115200 /dev/ttyACM0
```

### 起動ログ例

```
================================================
 PicoW-NIC  USB-WiFi Bridge
 USB subnet : 10.0.0.0/24
 WiFi subnet: 192.168.4.0/24  SSID=PicoBridge
================================================

WiFi AP: SSID=PicoBridge  Pico=192.168.4.1  Client=192.168.4.10 (DHCP fixed)
DHCP ready on w1: fixed IP = 192.168.4.10
USB net: Pico=10.0.0.1  Client=10.0.0.2 (DHCP fixed)
DHCP ready on us: fixed IP = 10.0.0.2
Network: IP forwarding enabled (IP_FORWARD=1)
Network: NAPT active (USB→WiFi masquerade via nat.c)
Network: WiFi TX checksum wrapper installed

Ready — connect PC to USB and/or WiFi AP "PicoBridge"
```

15 秒ごとにステータスが出力される：

```
--- Status ---
USB  (us1): IP=10.0.0.1         link=UP
WiFi (w10): IP=192.168.4.1      link=UP  RSSI=-45 dBm
```

### 詳細ログの有効化

`lwipopts.h` の `VERBOSE_LOG` を `1` にしてリビルド：

```c
// lwipopts.h
#define VERBOSE_LOG  1   // USB RX/TX、NAT ICMP、DHCP recv の全ログ
```

---

## WiFi 設定の変更

`CMakeLists.txt` の定義を変更してリビルド：

```cmake
target_compile_definitions(picow_nic PRIVATE
    WIFI_SSID="MyNetwork"       ← 変更
    WIFI_PASSWORD="MyPass123"   ← 変更
    WIFI_CHANNEL=6
    ...
)
```

---

## 既知の制限事項

- WiFi クライアントは 1 台のみ（DHCP 固定 IP のため）  
  複数クライアント対応は dhcpserver.c の拡張が必要
- IPv6 は非対応（`LWIP_IPV6=0`）
- NAT テーブルサイズは `nat.h` の `NAT_TABLE_SIZE` で定義（デフォルト: 64 エントリ）
- WiFi→USB 方向の自動ルート配布は未対応（WiFi 側は手動ルート設定が必要）

# PicoW-Rescue-NIC v1.0 仕様書

英語版: [spec-EN.md](spec-EN.md)

---

## 概要

Raspberry Pi Pico W を USB-WiFi ブリッジ NIC（ネットワークインターフェースカード）として機能させるファームウェアです。

USB に接続すると Pico W は:
- サーバーに **標準 USB イーサネットアダプター（CDC-NCM）** として認識される
- 同時に **WiFi アクセスポイント** を動作させる
- 2 つのネットワーク間で IP トラフィックをルーティング・NAPT する

これにより、サーバーのプライマリネットワークスタックとは独立した  
**アウトオブバンドネットワークアクセス** を提供します。  
サーバーの通常ネットワークが設定ミスやロックアウトで使えない状態でも、  
Pico W は WiFi 経由でアクセス可能なままです。

```
[Linux/Windows サーバー]──USB(CDC-NCM)──[Pico W]──WiFi(AP)──[スマホ/ラップトップ]
         10.0.0.2                        10.0.0.1               192.168.4.10
                                        192.168.4.1
```

---

## ネットワーク仕様

| 項目 | 値 |
|------|-----|
| USB サブネット | `10.0.0.0/24` |
| Pico USB 側 IP | `10.0.0.1` |
| USB クライアント IP | `10.0.0.2`（DHCP 固定） |
| WiFi SSID | `PicoBridge`（変更可能） |
| WiFi パスワード | `picobridge123`（変更可能） |
| WiFi チャンネル | 6（変更可能） |
| WiFi セキュリティ | WPA2-AES |
| WiFi サブネット | `192.168.4.0/24` |
| Pico WiFi 側 IP | `192.168.4.1` |
| WiFi クライアント IP | `192.168.4.10`（DHCP 固定） |

### ルーティング自動設定

USB 側 DHCP サーバーは DHCP オプション 121（Classless Static Route、RFC 3442）で以下を配布します:

```
192.168.4.0/24  via  10.0.0.1
```

Linux ホストはこのオプションを自動的に処理します。ルートは以下のように表示されます:
```bash
ip route | grep 192.168.4
# 192.168.4.0/24 via 10.0.0.1 dev picow proto dhcp src 10.0.0.2 metric 101
```

手動での `ip route add` は不要です。ルートは DHCP クライアントが管理し、再起動後も維持されます。

---

## ハードウェア要件

| コンポーネント | 仕様 |
|--------------|------|
| MCU | Raspberry Pi Pico W（RP2040 + CYW43439） |
| USB ケーブル | USB-A to Micro-USB、データ通信対応 |
| ホスト | USB ポートのある Linux / macOS / Windows 10+ マシン |
| 電源 | USB バス給電（約 100 mA 典型値） |

Pico W は約 1,000 円です。追加コンポーネントは不要です。

---

## ソフトウェア依存関係

| コンポーネント | バージョン | 入手元 |
|--------------|----------|-------|
| Pico SDK | 2.x | [raspberrypi/pico-sdk](https://github.com/raspberrypi/pico-sdk) |
| TinyUSB | 0.15+ | Pico SDK 同梱 |
| lwIP | 2.1.x | Pico SDK 同梱 |
| コンパイラ | arm-none-eabi-gcc | パッケージマネージャ |
| CMake | 3.13+ | パッケージマネージャ |

---

## 対応プロトコル

| プロトコル | USB→WiFi | WiFi→USB | 備考 |
|----------|----------|----------|------|
| ICMP（ping） | ✅ | ✅ | ID 変換を含む完全な NAT |
| TCP（SSH、curl、HTTP） | ✅ | ✅ | ポート変換を含む完全な NAPT |
| UDP | ✅ | ✅ | ポート変換を含む完全な NAPT |
| IPv6 | ❌ | ❌ | 未実装（`LWIP_IPV6=0`） |

---

## USB デバイス設定

Pico W は **複合 USB デバイス** として提示されます（2 つのインターフェース）:

| インターフェース | USB クラス | OS 上の名前 | 用途 |
|---------------|----------|------------|------|
| CDC-NCM + IAD | 0x02/0x0D | `picow`、`usb0`、`enp*` | USB イーサネット（IP トラフィック） |
| CDC-ACM | 0x02/0x02 | `/dev/ttyACM*`、`COM*` | デバッグシリアルコンソール |

**MAC アドレス:** `02:02:84:69:60:00`（ローカル管理、固定）

**CDC-NCM** を RNDIS より選択した理由は、Linux・macOS でドライバなしのプラグアンドプレイ互換性のためです。Windows 10 バージョン 1903 以降は CDC-NCM をネイティブサポートします。

---

## NAPT（ネットワークアドレス・ポート変換）

`src/nat.c` に `LWIP_HOOK_IP4_INPUT` を使った独自双方向 NAPT を実装。

lwIP 組み込みの NAPT（`ip4_napt.c`）は Pico SDK の lwIP スナップショットに含まれていません（上流 2.1.x 以降で追加）。

### NAT テーブル

- **サイズ:** 64 エントリ（`nat.h` の `NAT_TABLE_SIZE` で変更可能）
- **ポート範囲:** 49152〜65535（IANA 動的/プライベートレンジ）
- **TTL:** TCP 120 秒、UDP 30 秒、ICMP 10 秒

### 変換フロー

| 方向 | 送信元マスカレード | 宛先復元 |
|------|-----------------|---------|
| USB → WiFi | `nat_outbound`: クライアント IP → `192.168.4.1` | `nat_inbound`: → 元のクライアント IP |
| WiFi → USB | `nat_outbound2`: クライアント IP → `10.0.0.1` | `nat_inbound2`: → 元のクライアント IP |

### チェックサム処理

lwIP の `ip4_forward()` は `CHECKSUM_GEN_*=1` のとき転送パケットのすべてのトランスポートチェックサムをゼロ化します（ハードウェアオフロード前提）。USB CDC-NCM ドライバも CYW43 ドライバもチェックサムオフロードを実装していません。

送信直前にソフトウェアチェックサムエンジンがすべてのチェックサムを再計算します:
- **USB TX:** `usb_net.c` の `usb_fill_checksums()`
- **WiFi TX:** `network.c` の linkoutput ラッパー経由 `wifi_fill_checksums()`

---

## DHCP サーバー

インターフェースごとに独立した 2 つの DHCP サーバー（`src/dhcpserver.c`）:

| インターフェース | 割当 IP | リース時間 | オプション 121 |
|---------------|---------|----------|-------------|
| USB | `10.0.0.2`（固定） | 86400 秒（24 時間） | `192.168.4.0/24 via 10.0.0.1` |
| WiFi | `192.168.4.10`（固定） | 86400 秒（24 時間） | （なし） |

---

## デバッグコンソール

CDC-ACM インターフェースがリアルタイムのデバッグコンソールを提供します。

**接続:**
```bash
screen /dev/ttyACM0 115200         # Linux
screen /dev/cu.usbmodem* 115200    # macOS
# Windows: デバイスマネージャーで COM ポート確認、PuTTY で 115200 8N1
```

**起動ログ:**
```
================================================
 PicoW-NIC  USB-WiFi Bridge
 USB subnet : 10.0.0.0/24
 WiFi subnet: 192.168.4.0/24  SSID=PicoBridge
================================================

WiFi AP: SSID=PicoBridge  Pico=192.168.4.1  Client=192.168.4.10
Network: IP forwarding enabled (IP_FORWARD=1)
Network: NAPT active
Network: WiFi TX checksum wrapper installed

Ready — connect PC to USB and/or WiFi AP "PicoBridge"
```

**定期ステータス**（15 秒ごと）:
```
--- Status ---
USB  (us1): IP=10.0.0.1         link=UP
WiFi (w10): IP=192.168.4.1      link=UP  RSSI=-45 dBm
```

**詳細モード**（`lwipopts.h` の `VERBOSE_LOG=1`、リビルド必要）:
```
USB RX: 98 bytes  IPv4 proto=1 dport=12345
NAT ICMP: inp=w1 src=192.168.4.10 dst=10.0.0.2 type=8
USB TX: 98 bytes  ready=1 can_xmit=1
```

---

## 設定

ユーザーが変更可能なすべての設定は `CMakeLists.txt` にあります:

```cmake
target_compile_definitions(picow_nic PRIVATE
    WIFI_SSID="PicoBridge"        # WiFi AP 名
    WIFI_PASSWORD="picobridge123"  # WPA2 パスフレーズ
    WIFI_CHANNEL=6                 # 2.4 GHz チャンネル（1〜13）
    PICO_STDIO_USB=0               # USB stdio 無効（CDC-ACM は stdio_cdc.c が処理）
)
```

IP アドレスとサブネットはソースで定義:

| シンボル | ファイル | デフォルト |
|---------|---------|----------|
| Pico USB IP | `usb_net.c` | `10.0.0.1` |
| USB クライアント IP | `usb_net.c` | `10.0.0.2` |
| Pico WiFi IP | `wifi_ap.c` | `192.168.4.1` |
| WiFi クライアント IP | `dhcpserver.c` | `192.168.4.10` |
| NAT テーブルサイズ | `nat.h` | 64 |

---

## ビルド

```bash
# 前提条件（Ubuntu/Debian）
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential

# クローン & ビルド
git clone https://github.com/mar1mo-41414/PicoW-Rescue-NIC
cd PicoW-Rescue-NIC
mkdir build && cd build
cmake .. -DPICO_BOARD=pico_w -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
# 出力: build/picow_nic.uf2
```

---

## フラッシュ

```bash
# BOOTSEL を押しながら USB 接続、ボタンを離す
# RPI-RP2 ドライブが現れる
cp build/picow_nic.uf2 /media/$USER/RPI-RP2/
# Pico が自動的に再起動してファームウェアを開始
```

---

## 既知の制限事項

| 制限事項 | 詳細 |
|---------|------|
| WiFi クライアント 1 台のみ | DHCP 固定 IP — 2 台目は静的 IP 設定が必要 |
| USB Full Speed | 物理 12 Mbps、NAT スループット 2〜4 Mbps 程度 |
| IPv4 のみ | `LWIP_IPV6=0` |
| WiFi クライアントのルート | WiFi→USB 方向は WiFi 側で手動 `ip route add` が必要（USB→WiFi は DHCP opt 121 で自動） |
| 追加認証なし | WiFi は WPA2 だがブリッジ自体に追加認証層なし |
| Pico W のみ | RP2040 + CYW43439 でのみテスト済み |

---

## 動作確認環境

| 役割 | OS / デバイス | インターフェース | 状態 |
|------|-------------|---------------|------|
| USB ホスト | Ubuntu 22.04 LTS | CDC-NCM (`picow`) | ✅ 確認済み |
| USB ホスト | Ubuntu 24.04 LTS | CDC-NCM | ✅ 確認済み |
| USB ホスト | Windows 10/11 | CDC-NCM | ✅ 確認済み |
| WiFi クライアント | macOS 14 Sonoma | WiFi（WPA2） | ✅ 確認済み |
| WiFi クライアント | Android（複数機種） | WiFi（WPA2） | ✅ 確認済み |

---

## バージョン履歴

| バージョン | 日付 | 変更内容 |
|----------|------|---------|
| v1.0 | 2026-05-18 | 初回安定版リリース。双方向 TCP/UDP/ICMP 完全動作。DHCP オプション 121 対応。 |

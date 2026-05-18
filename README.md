# 🆘 PicoW-Rescue-NIC

> **サーバーの緊急脱出口 — OS のネットワークスタックとは独立した、常時動作する非常口。**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Raspberry%20Pi%20Pico%20W-red)](https://www.raspberrypi.com/products/raspberry-pi-pico/)
[![SDK](https://img.shields.io/badge/Pico%20SDK-2.x-blue)](https://github.com/raspberrypi/pico-sdk)
[![CI Build](https://github.com/mar1mo-41414/PicoW-Rescue-NIC/actions/workflows/build.yml/badge.svg)](https://github.com/mar1mo-41414/PicoW-Rescue-NIC/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/mar1mo-41414/PicoW-Rescue-NIC)](https://github.com/mar1mo-41414/PicoW-Rescue-NIC/releases/latest)

[English version → README-EN.md](README-EN.md)

---

## 問題

あなたにも経験があるはず。

```
[自宅の PC] ──── インターネット ──✗── [遠くにあるサーバー]
```

`iptables -F` を実行して SSH のポートを開け直すのを忘れた。  
ネットワークインターフェースを誤って設定してしまった。  
テスト中に NetworkManager を終了してしまった。  
ルーティングテーブルのエントリを誤って消した。

**マシンは生きている。ディスクも正常。でもアクセスできない。**

物理的にアクセスするか、誰かに頼むしかない。

---

## 解決策

**6ドルの Raspberry Pi Pico W** をサーバーの空き USB ポートに挿すだけ。

```
[自宅 / スマホ]
      │
      ▼ WiFi (192.168.4.0/24)
 ┌────────────┐
 │  Pico W    │  ← 4cm × 2cm、USB 給電、常時動作
 │  Rescue NIC│
 └────────────┘
      │ USB CDC-NCM (10.0.0.0/24)
      ▼
[ロックアウトされたサーバー]
  - SSH が通る
  - curl が通る
  - ping が通る
  - iptables/ルーティング変更も効く
```

Pico W はサーバーに **標準 USB イーサネットアダプター (CDC-NCM)** として認識されます。  
同時に **WiFi アクセスポイント** を動作させます。  
スマホやラップトップから WiFi 経由で接続し、ブリッジ越しに SSH して問題を修正できます。

**ドライバ不要。クラウド不要。ベンダーロックインなし。オフラインでも動作。**

---

> [!WARNING]
> **注意！ 使用は自己責任です！**
>
> これを使用してサーバーを復活させられる保証はありません。  
> 正しく動かないことがあるかもしれません。  
> 逆にこれの使用がトドメを刺す可能性もあります。  
> 何が起きても、私は責任を取れません。  
> **事前に実験機などでテストしておくことをお勧めします。**

---

## 機能

| 機能 | 詳細 |
|------|------|
| 🔌 **USB イーサネット (CDC-NCM)** | Linux・macOS・Windows 10+ でプラグアンドプレイ |
| 📡 **WiFi アクセスポイント** | WPA2-AES、SSID・パスワード設定可能 |
| 🔄 **双方向 IP ルーティング** | フル NAT/NAPT — SSH・curl・ping・SCP |
| 🏷️ **自動ルート配布** | DHCP オプション 121 で WiFi ルートを USB ホストに自動設定 |
| 🖥️ **デバッグコンソール** | CDC-ACM シリアル — 同じ USB ケーブルでファームウェアログ確認 |
| ⚡ **依存ゼロ** | OS 不要・RTOS 不要・カーネルモジュール不要 |
| 🔋 **USB バス給電** | 約 100mA — サーバー再起動後も USB 経由で自動復帰 |

---

## ユースケース

### 🏠 ホームラボ・セルフホスト

- `iptables` / `nftables` の設定ミスでサーバーにログインできなくなった
- ネットワーク設定変更のテスト中のフォールバックとして常設
- WiFi カードのないマシンへのリモートアクセス

### 🏢 データセンター・コロケーション

- 管理ネットワークがダウンしたときの緊急アクセス
- IPMI/iDRAC と並ぶ副系 OOB（アウトオブバンド）パス
- "Break glass（非常時用）" オプションとして事前インストール

### 🧪 開発・ラボ

- ネットワーク非依存の組み込みターゲットアクセス
- 本番ネットワークを触らない独立したテストネットワーク
- ライブシステムでのネットワークスタック変更テスト

### 🚗 ポータブル・フィールド

- ラップトップへの USB テザリング WiFi ブリッジ
- デモ用の軽量ネットワークブリッジ

---

## ネットワーク構成

```
                    ┌─────────────────────────────┐
                    │       Raspberry Pi Pico W    │
                    │                             │
[WiFi クライアント]  │  192.168.4.1  ←→  10.0.0.1  │    [USB サーバー]
192.168.4.10 ───────│  WiFi AP            CDC-NCM │──── 10.0.0.2
                    │       ↕ NAPT/ルーティング ↕    │
                    └─────────────────────────────┘

WiFi サブネット : 192.168.4.0/24   (Pico = 192.168.4.1)
USB サブネット  : 10.0.0.0/24      (Pico = 10.0.0.1)
WiFi クライアント: 192.168.4.10    (DHCP 固定)
USB サーバー    : 10.0.0.2         (DHCP 固定)
```

### パケットフロー例：スマホ → ロックアウトされたサーバーへ SSH

```
スマホ (192.168.4.10)
  │  SSH TCP SYN → 10.0.0.2:22
  ▼
Pico W WiFi (192.168.4.1) — パケット受信
  │  NAT: src 192.168.4.10 → 10.0.0.1
  ▼
Pico W USB (10.0.0.1) — CDC-NCM 経由で転送
  │
  ▼
サーバー (10.0.0.2) — 10.0.0.1 からの SSH として受信
  │  SSH SYN-ACK → 10.0.0.1
  ▼
Pico W USB — 受信、NAT テーブル検索
  │  dst 10.0.0.1 → 192.168.4.10 に復元
  ▼
スマホ — SSH セッション確立 ✓
```

---

## ハードウェア要件

- **Raspberry Pi Pico W** (RP2040 + CYW43439)  
  *約 1,000 円 — これだけ*
- USB-A to Micro-USB ケーブル（データ通信対応のもの）
- ホスト: 空き USB ポートのある Linux / macOS / Windows マシン

> **Pico 2 W は？** Pico 2 W (RP2350) は SDK を少し変更すれば動作するはずですが未テストです。PR 歓迎。

---

## ビルド

### ビルド済みファームウェアをダウンロード（初心者向け・推奨）

ビルド環境を用意しなくても、[**GitHub Releases**](https://github.com/mar1mo-41414/PicoW-Rescue-NIC/releases/latest) から最新の `.uf2` ファイルを直接ダウンロードできます。

```
1. Releases ページを開く
2. 最新リリースの「Assets」から picow_nic_vX.X.uf2 をダウンロード
3. Pico W を BOOTSEL モードにして（ボタンを押しながら USB 接続）
4. 現れた RPI-RP2 ドライブにファイルをコピー → 完了
```

ソースコードからビルドする場合は以下を参照してください。

### 前提条件

```bash
# ARM ツールチェーン インストール (Ubuntu/Debian)
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential

# Pico SDK のクローン
git clone https://github.com/raspberrypi/pico-sdk --recurse-submodules
export PICO_SDK_PATH=/path/to/pico-sdk
```

### クローン & ビルド

```bash
git clone https://github.com/mar1mo-41414/PicoW-Rescue-NIC
cd PicoW-Rescue-NIC

mkdir build && cd build
cmake .. -DPICO_BOARD=pico_w -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

出力: `build/picow_nic.uf2`

### WiFi 認証情報の変更

ビルド前に `CMakeLists.txt` を編集:

```cmake
target_compile_definitions(picow_nic PRIVATE
    WIFI_SSID="YourRescueNet"
    WIFI_PASSWORD="YourSecret"
    WIFI_CHANNEL=6
)
```

---

## フラッシュ

### 方法 1: UF2（推奨）

```bash
# BOOTSEL ボタンを押しながら USB に接続
# "RPI-RP2" ドライブが現れたらコピー
cp build/picow_nic.uf2 /media/$USER/RPI-RP2/

# またはヘルパースクリプトを使用
./scripts/flash.sh
```

### 方法 2: SWD (OpenOCD)

```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
  -c "program build/picow_nic.elf verify reset exit"
```

---

## 接続手順

### Step 1: Pico W を USB に接続

USB に挿すと、サーバーは DHCP 経由で CDC-NCM インターフェースを自動設定します。

```bash
# サーバーで確認
ip addr show        # → 10.0.0.2/24 が picow/usb0 などに割り当て済み
ip route            # → 192.168.4.0/24 via 10.0.0.1 が自動設定済み（DHCP opt 121）
```

手動で `ip route add` する必要はありません — DHCP が自動で WiFi サブネットのルートを配布します。

### Step 2: WiFi に接続

スマホやラップトップから **`PicoBridge`**（パスワード: `picobridge123`）に接続。

```
あなたのデバイスに割り当てられる IP: 192.168.4.10
```

### Step 3: サーバーにアクセス

```bash
# スマホ/ラップトップ (PicoBridge WiFi) から
ssh user@10.0.0.2        # ロックアウトされたサーバーに SSH
curl http://10.0.0.2      # HTTP
ping 10.0.0.2             # Ping
scp file user@10.0.0.2:~ # ファイルコピー
```

### WiFi 側からのルート設定（WiFi → USB 方向）

WiFi クライアントから `10.0.0.x` に到達するにはルートが必要です:

```bash
# macOS
sudo route add -net 10.0.0.0/24 192.168.4.1

# Linux
sudo ip route add 10.0.0.0/24 via 192.168.4.1

# Windows
route add 10.0.0.0 mask 255.255.255.0 192.168.4.1
```

---

## デバッグコンソール

Pico は同じ USB ケーブルで CDC-ACM シリアルポートも提供します。

```bash
# Linux
screen /dev/ttyACM0 115200

# macOS
screen /dev/cu.usbmodem* 115200

# Windows
# デバイスマネージャーで COM ポートを確認 → PuTTY
```

### 起動ログ

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

--- Status ---
USB  (us1): IP=10.0.0.1         link=UP
WiFi (w10): IP=192.168.4.1      link=UP  RSSI=-42 dBm
```

### 詳細ログの有効化

パケット単位のトレース（NAT/ルーティングのデバッグに使用）:

```c
// lwipopts.h — 1 に設定してリビルド
#define VERBOSE_LOG  1
```

パケット単位の出力例:
```
USB RX: 98 bytes  IPv4 proto=1 dport=12345
NAT ICMP: inp=w1 src=192.168.4.10 dst=10.0.0.2 type=8
USB TX: 98 bytes  ready=1 can_xmit=1
```

---

## 技術アーキテクチャ

### スタック概要

```
┌─────────────────────────────────────────────────────┐
│                    アプリケーション                    │
│         main.c — 協調ポーリングループ                  │
├──────────────────┬──────────────────────────────────┤
│   TinyUSB        │         lwIP (NO_SYS=1)           │
│   CDC-NCM        │  IP_FORWARD + LWIP_HOOK_IP4_INPUT │
│   CDC-ACM        │         NAPT (nat.c)              │
├──────────────────┼──────────────────────────────────┤
│  USB ペリフェラル │      CYW43439 WiFi ドライバ       │
│  (RP2040 USB)    │      (cyw43_arch_lwip_poll)       │
└──────────────────┴──────────────────────────────────┘
           RP2040 @ 125 MHz — 264 KB SRAM
```

### 主要な設計判断

**なぜカスタム NAPT？**  
Pico SDK 同梱の lwIP スナップショットには `ip4_napt.c`（上流 2.1.x 以降に追加）が含まれていません。  
`LWIP_HOOK_IP4_INPUT` を使って `src/nat.c` に最小限の NAT テーブルを実装しています。

**なぜソフトウェアチェックサム？**  
`ip4_forward()` はチェックサムオフロードを前提に IP/TCP/UDP/ICMP チェックサムをゼロ化します。  
TinyUSB の CDC-NCM も CYW43 ドライバもオフロードを実装していないため、  
両インターフェースの `linkoutput` パスでソフトウェア再計算しています。

**なぜ RTOS なし？**  
TinyUSB と lwIP はどちらも協調ポーリングモードを持ち、クリーンに組み合わせられます。  
RTOS を追加しても、このユースケースではメリットより複雑さが増すだけです。

詳細: [`docs/architecture.md`](docs/architecture.md) / [`docs/investigation.md`](docs/investigation.md)

---

## プロジェクト構成

```
PicoW-Rescue-NIC/
├── README.md               このファイル（日本語）
├── README-EN.md            English version
├── LICENSE
├── CMakeLists.txt          ビルド設定
├── lwipopts.h              lwIP 設定（VERBOSE_LOG、チェックサム、NAT フック）
├── tusb_config.h           TinyUSB 設定（CDC-NCM + CDC-ACM 複合デバイス）
├── pico_sdk_import.cmake
│
├── src/
│   ├── main.c              エントリーポイント、協調ポーリングループ
│   ├── usb_net.c/h         CDC-NCM ↔ lwIP ブリッジ + ソフトウェアチェックサムエンジン
│   ├── wifi_ap.c/h         CYW43 AP モード + DHCP サーバー初期化
│   ├── network.c/h         WiFi TX チェックサムラッパー、network_init()
│   ├── nat.c/h             双方向 NAPT（TCP/UDP/ICMP 対応）
│   ├── dhcpserver.c/h      固定 IP DHCP サーバー（DHCP オプション 121 対応）
│   ├── usb_descriptors.c/h 複合 USB 記述子（IAD）
│   └── stdio_cdc.c         printf → CDC-ACM シリアル
│
├── docs/
│   ├── architecture.md     設計判断、データフロー図（日本語）
│   ├── architecture-EN.md  Architecture (English)
│   ├── investigation.md    バグ解析：チェックサム、NAT、lwIP 内部（日本語）
│   ├── investigation-EN.md Investigation log (English)
│   ├── troubleshooting.md  よくある問題と解決策（日本語）
│   ├── troubleshooting-EN.md Troubleshooting (English)
│   ├── spec.md             完全仕様書（日本語）
│   ├── spec-EN.md          Specification (English)
│   └── release-notes-v1.0.md リリースノート（日本語）
│
├── images/                 写真、図（予定）
└── scripts/
    └── flash.sh            ワンコマンドフラッシュスクリプト
```

---

## 既知の制限事項

| 制限 | 備考 |
|------|------|
| WiFi クライアント 1 台のみ | DHCP 固定 IP のため — 複数対応は dhcpserver 拡張が必要 |
| USB Full Speed | 物理 12 Mbps 制限、NAT スループット 2–4 Mbps 程度 |
| IPv6 非対応 | `LWIP_IPV6=0` — IPv4 のみ |
| WiFi クライアントの手動ルート | WiFi→USB 方向は手動 `ip route add` が必要（USB→WiFi は DHCP opt 121 で自動） |
| 追加認証なし | WiFi は WPA2 だがブリッジ自体には追加の認証層なし |
| Pico W のみ | RP2040 + CYW43439 でのみテスト済み |

---

## ロードマップ / TODO

- [ ] **複数 WiFi クライアント** — DHCP リーステーブル実装
- [ ] **ステーションモード** — 上流 WiFi に接続して USB にブリッジ（逆方向）
- [ ] **Web ステータスページ** — lwIP httpd でリンク状態・NAT テーブル・RSSI 表示
- [ ] **DNS プロキシ** — USB 側からの DNS クエリを WiFi 経由で転送
- [ ] **WireGuard** — WiFi リンク上の暗号化トンネル
- [ ] **RNDIS サポート** — 古い Windows バージョン向け
- [ ] **Pico 2 W (RP2350)** — 移植・テスト
- [ ] **USB High Speed** — 外部 PHY または RP2350 で 12 Mbps 超

---

## コントリビュート

Issue や PR は歓迎です。提出前に:

1. [`docs/architecture.md`](docs/architecture.md) で設計の背景を確認
2. [`docs/investigation.md`](docs/investigation.md) で既知の NAT/チェックサムの注意点を確認
3. 提出前に両方向で ping + SSH のベーシックテストを実行

---

## ライセンス

MIT — [LICENSE](LICENSE) を参照

---

## 謝辞

- [Raspberry Pi Foundation](https://www.raspberrypi.com/) — Pico W ハードウェア + SDK
- [TinyUSB](https://github.com/hathach/tinyusb) — USB デバイススタック
- [lwIP](https://savannah.nongnu.org/projects/lwip/) — TCP/IP スタック
- [Pico SDK](https://github.com/raspberrypi/pico-sdk) — ボードサポート + CYW43 ドライバ

---

<details>
<summary>📷 写真・デモ（クリックして展開）</summary>

<!-- 写真をここに追加 -->
> *写真: サーバーの USB ポートに挿さった Pico W、スマホから WiFi 接続中*

<!-- GIF をここに追加 -->
> *GIF: Pico W ブリッジ越しに確立した SSH セッション*

</details>

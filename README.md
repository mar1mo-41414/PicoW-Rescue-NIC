# PicoW-NIC — USB-WiFi Bridge Firmware

Raspberry Pi Pico W を超小型 USB 無線 NIC / デバッグブリッジとして動作させる
ネイティブ C ファームウェア。

```
PC A
 ↕ WiFi (192.168.4.0/24)
Raspberry Pi Pico W  ← NAT + IP forwarding
 ↕ USB Ethernet (10.0.0.0/24)
PC B
```

---

## 機能

| 機能 | 詳細 |
|------|------|
| USB Ethernet | CDC-NCM (Linux/macOS/Windows10+) |
| デバッグコンソール | CDC-ACM (UART over USB) |
| WiFi AP | WPA2-AES-PSK, SSID=PicoBridge |
| DHCP | USB側: 10.0.0.2〜、WiFi側: 192.168.4.2〜 |
| NAT | NAPT on WiFi interface (USB→WiFi透過) |
| IP forwarding | 双方向ルーティング |

---

## プロジェクト構成

```
PicoW-NIC/
├── CMakeLists.txt       ビルド定義
├── tusb_config.h        TinyUSB設定 (CDC-ACM + CDC-NCM composite)
├── lwipopts.h           lwIP設定 (NO_SYS, IP_FORWARD, IP_NAPT)
├── pico_sdk_import.cmake← SDKからコピー (後述)
└── src/
    ├── main.c           エントリポイント / メインループ
    ├── usb_descriptors.c USB device/config/string descriptor
    ├── usb_descriptors.h interface/EP番号定義
    ├── usb_net.c        TinyUSB NCM ↔ lwIP ブリッジ
    ├── usb_net.h
    ├── wifi_ap.c        CYW43 AP mode + DHCP
    ├── wifi_ap.h
    ├── network.c        IP forwarding / NAPT 設定
    ├── network.h
    ├── dhcpserver.c     軽量 DHCP サーバー (2インスタンス対応)
    └── dhcpserver.h
```

---

## ビルド手順

### 前提

- Raspberry Pi Pico SDK ≥ 1.5  (pico-sdk 2.x 推奨)
- ARM GCC toolchain (`arm-none-eabi-gcc`)
- CMake ≥ 3.13
- `PICO_SDK_PATH` 環境変数が設定済みであること

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
```

### 1. pico_sdk_import.cmake を用意

```bash
cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .
# または
ln -s $PICO_SDK_PATH/external/pico_sdk_import.cmake .
```

### 2. ビルド

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

成功すると `picow_nic.uf2` が生成される。

### WiFi 設定変更

`CMakeLists.txt` の `target_compile_definitions` を編集：

```cmake
WIFI_SSID="YourSSID"
WIFI_PASSWORD="YourPassword"
WIFI_CHANNEL=6
```

---

## Pico W への書き込み

### UF2 (推奨)

1. BOOTSEL ボタンを押しながら USB 接続
2. `RPI-RP2` ドライブが現れる
3. `picow_nic.uf2` をドラッグ&ドロップ
4. 自動的に再起動してファームウェア起動

### openocd (SWD)

```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
  -c "program build/picow_nic.elf verify reset exit"
```

---

## 接続確認

### Linux (PC B — USB ECM/NCM)

```bash
# NCM インタフェースが現れる (通常 usb0 または enp0s20u1 など)
ip link show
# DHCP で 10.0.0.2 が割り当てられる
ip addr show

# Pico への疎通確認
ping 10.0.0.1

# WiFi 側への到達確認 (PC A の IP が 192.168.4.2 の場合)
ping 192.168.4.2

# WiFi 側へのルート追加 (NAPT が有効なら不要)
ip route add 192.168.4.0/24 via 10.0.0.1
```

### macOS (PC B)

```bash
# NetworkManager で自動認識、または
networksetup -listallnetworkservices
# "Pico W USB-WiFi Bridge" が現れる

ping 10.0.0.1

# WiFi→USB 方向ルート追加
sudo route add 192.168.4.0/24 10.0.0.1
```

### Windows (PC B)

Windows 10 build 1903 以降は CDC-NCM を自動認識（ドライバ不要）。

```powershell
# デバイスマネージャーで "PicoW USB-WiFi Bridge" を確認
# DHCP で 10.0.0.2 が付与される
ping 10.0.0.1

# ルート追加
route add 192.168.4.0 mask 255.255.255.0 10.0.0.1
```

**旧 Windows (RNDIS) 対応** → 後述の「RNDIS ビルド」参照。

### PC A (WiFi)

1. SSID `PicoBridge` (パスワード `picobridge123`) に接続
2. 192.168.4.2 が割り当てられる

```bash
ping 192.168.4.1     # Pico WiFi

# USB 側へのルート (NAPT 有効時は不要、NAT でマスカレード)
ip route add 10.0.0.0/24 via 192.168.4.1

ping 10.0.0.2        # USB PC B
```

---

## デバッグ

デバッグ出力は **UART** (GP0=TX, GP1=RX, 115200 baud)。

```bash
# Linux/macOS
screen /dev/ttyUSB0 115200
# または
minicom -D /dev/ttyUSB0 -b 115200
```

USB が CDC-NCM で占有されているため、stdio USB は無効化してある。

---

## USB Descriptor 設計解説

### Composite Device (IAD)

`bDeviceClass=0xEF / SubClass=0x02 / Protocol=0x01` により、
OS は複数の Interface Association Descriptor (IAD) を持つ Composite として扱う。

```
Configuration 1
 ├─ IAD: CDC-ACM  (IF 0+1)
 │   ├─ IF 0: CDC Communication (INT EP 0x81)
 │   └─ IF 1: CDC Data (BULK 0x02 out / 0x82 in)
 └─ IAD: CDC-NCM  (IF 2+3)
     ├─ IF 2: CDC Communication (INT EP 0x83)
     │    Functional Descriptors: Header / Union / Ethernet / NCM
     ├─ IF 3 alt 0: CDC Data (no endpoints — inactive)
     └─ IF 3 alt 1: CDC Data (BULK 0x04 out / 0x84 in — active)
```

NCM の Data Interface が 2 つの AlternateSetting を持つのは CDC 仕様の要件。
ホストが `SET_INTERFACE alt=1` を送ると Data IF がアクティブになり、
`tud_network_init_cb()` が呼ばれる。

### CDC-NCM vs CDC-ECM

| 項目 | CDC-ECM | CDC-NCM |
|------|---------|---------|
| フレーム集約 | なし (1転送=1フレーム) | あり (NTB: 複数フレームを1転送) |
| スループット | 低い | 高い |
| ホスト対応 | Linux/macOS (古いWindows非対応) | Linux/macOS/Windows 10+ |
| 実装難度 | 低い | やや高い |

本実装は NCM を採用。ECM に変更するには `CFG_TUD_NET` クラスの設定を変更し、
descriptor を ECM 用に置き換える。

### CDC-ECM vs RNDIS (Windows)

RNDIS は Microsoft 独自拡張。古い Windows では RNDIS が必要だが、
Windows 10 build 1903 以降は NCM / ECM も標準ドライバで動作する。

---

## lwIP NAT 構成解説

`ip4_napt.c` (lwIP contrib) を使用：

```
USB PC (10.0.0.2) → [USB IF: 10.0.0.1] Pico [WiFi IF: 192.168.4.1] → WiFi Client (192.168.4.2)
                                              ↑ NAPT masquerade ここで発動
```

`ip_napt_enable(192.168.4.1, 1)` により、
WiFi AP インタフェースから OUT するパケットがマスカレードされる。

- USB PC → WiFi Client: src=10.0.0.2 → NAT → src=192.168.4.1 として WiFi 側へ
- 返りパケット: dst=192.168.4.1 → NAT で逆変換 → dst=10.0.0.2 として USB 側へ

WiFi クライアントは NAT の存在を意識しない。USB PC 側もデフォルトゲートウェイ
(10.0.0.1) さえ設定すれば追加設定不要。

---

## メモリ使用量見積もり

| 領域 | サイズ | 備考 |
|------|--------|------|
| lwIP ヒープ (MEM_SIZE) | 20 KB | PCB・ARP cache 等 |
| pbuf プール (20 × 1514) | ~30 KB | Ethernet フレームバッファ |
| TinyUSB (NCM + ACM) | ~6 KB | EP バッファ含む |
| CYW43 ドライババッファ | ~16 KB | SDK 内部 |
| NAPT テーブル (64 エントリ) | ~2 KB | ip4_napt.c 内部 |
| コードスタック等 | ~8 KB | |
| **合計 (概算)** | **~82 KB** | 264 KB SRAM の 31% |

コードサイズは Flash に収まる（通常 100–200 KB 程度）。

> **注意**: pbuf プール (`PBUF_POOL_SIZE`) を増やすと RAM 消費が大きく増える。
> 20 → 32 で約 18 KB 増加。Pico W の 264 KB SRAM に注意。

---

## 実効速度見積もり

USB 2.0 FS (12 Mbps) の理論上限。実効は：

| 経路 | 推定スループット |
|------|----------------|
| USB → WiFi (TX) | 2–4 Mbps |
| WiFi → USB (RX) | 2–4 Mbps |
| USB ↔ USB (loopback) | ~8 Mbps |

lwIP のコピー・処理オーバーヘッドと RP2040 の CPU 速度 (125 MHz) で上限が決まる。
CDN-NCM のフレーム集約により ECM より 1.5–2× 程度高速。

---

## RNDIS ビルド (Windows 旧版対応)

1. `tusb_config.h` を編集:
   ```c
   // CFG_TUD_NET は維持、NCM → RNDIS に切替
   // (TinyUSB の設定方法は tusb.h のコメント参照)
   ```
2. `usb_descriptors.c` で `TUD_CDC_NCM_DESCRIPTOR` を `TUD_RNDIS_DESCRIPTOR` に変更
3. Windows: RNDIS ドライバは `%windir%\inf\netrndis.inf` が使われる

Windows 10 以降では NCM のほうが安定・高速なため推奨。

---

## 将来の拡張案

| 拡張 | 概要 |
|------|------|
| Web UI | lwIP httpd で接続状態・RSSI・クライアント一覧を表示 |
| DNS プロキシ | USB 側の DNS を WiFi AP 経由で転送 |
| RNDIS composite | Windows 旧版サポート (alternate config) |
| Station + AP | WiFi を STA モードにして上流 WiFi に接続し USB PC へ転送 |
| VPN | WireGuard-lwIP で暗号化トンネル |
| USB HS | RP2350 / 外付け USB HS PHY で 480 Mbps |

---

## トラブルシュート

### USB デバイスが認識されない

- BOOTSEL ボタンを押してファームウェアが正常に書き込まれているか確認
- `lsusb` (Linux) / デバイスマネージャー (Windows) で `0xCAFE:0x4010` を確認
- `pico_enable_stdio_usb` が **0** になっているか確認 (USB を CDC-NCM が占有)

### IP アドレスが取得できない

- DHCP サーバーのログを UART で確認 (`screen /dev/ttyUSB0 115200`)
- Linux: `sudo dhclient usb0` で手動 DHCP 要求
- macOS: ネットワーク環境設定 → 接続インタフェース → DHCP を更新

### WiFi AP に接続できない

- パスワードが `picobridge123` であることを確認
- チャンネル 6 が他の AP と干渉していないか確認
- `CMakeLists.txt` の `WIFI_CHANNEL` を 1 または 11 に変更してリビルド

### NAT が動かない / 疎通しない

- `ip4_napt.c` がビルドに含まれているか cmake 出力を確認
  (`NAPT: found ...` メッセージが表示されるはず)
- 含まれていない場合は静的ルートを手動追加 (README「接続確認」参照)
- ファイアウォールが有効な場合は転送ルールを確認

### スループットが低い

- USB FS (12 Mbps) が物理上限 — これは仕様
- `TCP_WND` を大きくすると TCP スループット向上 (要 RAM 確認)
- `PBUF_POOL_SIZE` を増やすとバースト耐性が上がる (RAM 増加)

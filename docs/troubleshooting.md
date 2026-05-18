# トラブルシューティング — PicoW-Rescue-NIC

英語版: [troubleshooting-EN.md](troubleshooting-EN.md)

---

## クイック診断チェックリスト

1. USB インターフェースがサーバーに表示されているか？（`ip addr` で `10.0.0.2/24` が見える）
2. サーバーに WiFi ルートがあるか？（`ip route` で `192.168.4.0/24 via 10.0.0.1` が見える）
3. サーバーから Pico に ping できるか？（`ping 10.0.0.1`）
4. WiFi クライアントから Pico に ping できるか？（`ping 192.168.4.1`）
5. WiFi クライアントからサーバーに ping できるか？（`ping 10.0.0.2`）

1〜4 が通って 5 が失敗する場合、NAT またはルーティングの問題です。詳細ログを有効にして診断してください。

---

## 問題: Pico が USB ネットワークインターフェースとして現れない

### 症状

Pico を接続した後、`ip addr` に新しいネットワークインターフェースが表示されない。

### 原因と解決策

**ファームウェアが動作していない**
- 確認: LED が点灯し続けているべき（WiFi AP 動作中）。LED が消灯またはチカチカしている場合、ファームウェアが書き込まれていない可能性があります。
- 解決: 再フラッシュ。BOOTSEL を押しながら USB 接続、`.uf2` を RPI-RP2 ドライブにコピー。

**OS が CDC-NCM に対応していない**
- Windows 7/8 や一部の古い Windows 10 ビルドはドライバなしで CDC-NCM に対応していない場合があります。
- 解決: Windows 10 バージョン 1903 以降または Windows 11 を使用してください。古い Windows 向けの RNDIS サポートはロードマップにあります。

**USB ケーブルの問題**
- 充電専用ケーブルはデータ線がありません。
- 解決: データ通信対応と確認できているケーブルを使用してください。

**カーネルモジュールが読み込まれていない（Linux）**
```bash
sudo modprobe cdc_ncm
lsmod | grep cdc_ncm
```

---

## 問題: USB インターフェースは見えるが IP アドレスが割り当てられない

### 症状

`ip addr` にインターフェース（例: `picow`、`usb0`、`enp0s26u1u1u4`）は表示されるが、IPv4 アドレスがない。

### 原因と解決策

**DHCP クライアントが動作していない**
```bash
sudo dhclient picow          # またはお使いのインターフェース名
# または systemd-networkd の場合:
sudo networkctl               # インターフェースが管理されているか確認
```

**インターフェース名が異なる**
```bash
ip link | grep -E "usb|picow|enp.*u"
```
実際のインターフェース名を DHCP に使用してください。

**Pico の DHCP サーバーがまだ準備できていない**
- Pico は WiFi AP を初期化するまで約 2 秒かかります。USB の DHCP が準備できる前に接続した可能性があります。
- 解決: 3 秒後に USB を差し直してください。

---

## 問題: USB IP は通るが WiFi サブネットへのルートがない

### 症状

`ping 10.0.0.1` は成功するが `ping 192.168.4.10` が「ネットワーク到達不能」で失敗する。

### 原因

WiFi サブネットのルートがインストールされていません。DHCP オプション 121 で自動的に設定されるはずです。

### 解決策

**ルートが配布されているか確認:**
```bash
ip route | grep 192.168.4
# 期待値: 192.168.4.0/24 via 10.0.0.1 dev picow proto dhcp
```

**表示されない場合、DHCP を強制更新:**
```bash
sudo dhclient -r picow && sudo dhclient picow
```

**それでも表示されない場合（DHCP クライアントがオプション 121 未対応）:**
```bash
sudo ip route add 192.168.4.0/24 via 10.0.0.1
```

**注意:** 一部の最小限 DHCP クライアント（例: `busybox udhcpc`）はオプション 121 を処理しない場合があります。`systemd-networkd`、`NetworkManager`、`dhclient` はいずれも対応しています。

---

## 問題: ping は通るが SSH が失敗する

### 症状

WiFi クライアントから `ping 10.0.0.2` は通るが、`ssh user@10.0.0.2` がタイムアウトまたは接続拒否される。

### 原因と解決策

**サーバーで SSH が動作していない**
```bash
# サーバーで（USB または手元からアクセス）:
systemctl status ssh
sudo systemctl start ssh
```

**iptables/nftables がブロックしている**  
まさにそれが今回の問題の理由でこのデバイスが必要なのですが:
```bash
# サーバーで — USB インターフェース経由のトラフィックを許可:
sudo iptables -I INPUT -i picow -j ACCEPT   # インターフェース名を実際のものに変更
# または全ルールをフラッシュ（本番環境では注意）:
sudo iptables -F
```

**NAT テーブルの問題 — 詳細ログを有効にする**
```c
// lwipopts.h で 1 に設定してリビルド:
#define VERBOSE_LOG  1
```
デバッグコンソールで以下を確認:
```
NAT TCP: inp=w1 src=192.168.4.10:XXXXX dst=10.0.0.2:22
```
この行が表示されれば outbound NAT は動作しています。SYN-ACK の応答に対して inbound NAT ログが現れない場合は、戻り方向のパスを確認してください。

---

## 問題: WiFi クライアントが 1 台しか接続できない

### 症状

2 台目のデバイスが `PicoBridge` WiFi に接続できない、または接続できるが IP アドレスが取得できない。

### 原因

DHCP サーバーが固定 IP（`192.168.4.10`）を 1 つだけ割り当てます。DHCP リースは 1 件のみ対応しています。AP 自体には複数台の WiFi 接続が可能ですが、IP を受け取れるのは 1 台だけです。

### 回避策

2 台目のデバイスに手動で静的 IP を設定:
```
IP: 192.168.4.11
サブネット: 255.255.255.0
ゲートウェイ: 192.168.4.1
```

### 将来の修正

`dhcpserver.c` をリーステーブル対応に拡張します。ロードマップに登録済み。

---

## 問題: 最初は通信できるが時間が経つと切れる

### 症状

SSH や ping が 1〜2 分は動作するが、その後停止または切断される。

### 原因と解決策

**NAT テーブルのタイムアウト**  
デフォルト TTL: TCP 120 秒、UDP 30 秒、ICMP 10 秒。長期間アイドルの接続が削除される可能性があります。

SSH のキープアライブを設定:
```
# ~/.ssh/config
Host *
    ServerAliveInterval 30
    ServerAliveCountMax 3
```

**NAT テーブルが満杯**  
多数の接続が同時に存在すると 64 エントリのテーブルが枯渇する可能性があります。
```c
// nat.h — テーブルサイズを増やす:
#define NAT_TABLE_SIZE 128
```

**WiFi 電波干渉**  
電波環境の悪い場所では Pico の AP がクライアントとの関連付けを失う可能性があります。  
デバッグコンソールのステータス出力で RSSI を確認してください。

---

## 問題: デバッグコンソールに何も表示されない

### 症状

`/dev/ttyACM0` を開いても出力がない。

### 原因と解決策

**デバイスファイルが違う**
```bash
ls /dev/ttyACM*          # Linux
ls /dev/cu.usbmodem*     # macOS
```
他の USB シリアルデバイスが接続されている場合、複数の ACM デバイスがある可能性があります。

**Pico の起動後にターミナルを開いた**  
起動時の 5 秒間のウィンドウで CDC 接続を待ってからバナーを表示します。  
そのウィンドウを過ぎてターミナルを開いた場合、バナーは見られませんが 15 秒ごとにステータス行が表示されます。

**一部のターミナルでボーレートが重要**  
115200 ボー、8N1 を使用してください。`screen`、`picocom`、PuTTY いずれでも動作します。

```bash
screen /dev/ttyACM0 115200
# または
picocom -b 115200 /dev/ttyACM0
```

---

## 問題: パケットは転送されるがチェックサムが誤っている

### 症状

いずれかのインターフェースで `tcpdump -vv` が `bad cksum` 警告を表示する。宛先でパケットがドロップされる。

### 原因

v1.0 では修正済みのはずです。これが見える場合、ソフトウェアチェックサムエンジンが動作していません。

### 診断
```bash
sudo tcpdump -i picow -n -vv icmp
# 確認: ICMP echo reply, bad cksum 0 (->xxxx)!
```

### 解決策

`lwipopts.h` を確認:
```c
#define CHECKSUM_GEN_IP     1
#define CHECKSUM_GEN_TCP    1
#define CHECKSUM_GEN_UDP    1
#define CHECKSUM_GEN_ICMP   0   // 必ず 0 に — ip4_forward がゼロ化するため
```

`usb_fill_checksums()` が `usb_netif_linkoutput()` 内で呼ばれているか、  
`wifi_fill_checksums()` が `network_init()` の linkoutput ラッパー経由で呼ばれているかも確認してください。

詳細は [`docs/investigation.md`](investigation.md) のチェックサムバグ解析を参照してください。

---

## 詳細なパケット単位ログの有効化

詳細なデバッグには詳細ログを有効化:

```c
// lwipopts.h
#define VERBOSE_LOG  1   // 1 に設定してリビルド、再フラッシュ
```

デバッグコンソールにパケット単位の出力が表示されます:

```
USB RX: 98 bytes  IPv4 proto=1 dport=12345
NAT ICMP: inp=w1 src=192.168.4.10 dst=10.0.0.2 type=8
USB TX: 98 bytes  ready=1 can_xmit=1
```

**ログのプレフィックス:**
- `USB RX:` — USB ホストからパケット受信
- `USB TX:` — USB ホストへパケット送信
- `NAT ICMP/TCP/UDP:` — NAT テーブル検索・書き換え
- `DHCP recv:` — DHCP パケット処理

通常使用では `0` に戻してください — 詳細モードは ping 1 回につき約 10 行を出力し、パフォーマンスに影響します。

---

## ファームウェアの再フラッシュ

```bash
# 方法 1: UF2 ドラッグ＆ドロップ
# 1. Pico を抜く
# 2. BOOTSEL ボタンを押す
# 3. USB を接続 — "RPI-RP2" ドライブが現れる
# 4. ボタンを離す
cp build/picow_nic.uf2 /media/$USER/RPI-RP2/
# Pico が自動的に再起動

# 方法 2: ヘルパースクリプト
./scripts/flash.sh

# 方法 3: OpenOCD (SWD)
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
  -c "program build/picow_nic.elf verify reset exit"
```

---

## それでも解決しない場合

1. [`docs/investigation.md`](investigation.md) を確認 — 開発中に見つかったすべてのバグを記録しています
2. `VERBOSE_LOG=1` を有効にし、デバッグコンソールの出力をキャプチャして GitHub に Issue を開く
3. Issue に含めてほしい情報: OS バージョン、`ip addr`、`ip route`、`dmesg | tail -20`、Pico デバッグコンソールのログ

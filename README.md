# 🆘 PicoW-Rescue-NIC

> **サーバーのネットワーク緊急脱出口 — OS のネットワークスタックとは独立した、挿すだけの非常口。**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Raspberry%20Pi%20Pico%20W-red)](https://www.raspberrypi.com/products/raspberry-pi-pico/)
[![SDK](https://img.shields.io/badge/Pico%20SDK-2.x-blue)](https://github.com/raspberrypi/pico-sdk)
[![CI Build](https://github.com/mar1mo-41414/PicoW-Rescue-NIC/actions/workflows/build.yml/badge.svg)](https://github.com/mar1mo-41414/PicoW-Rescue-NIC/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/mar1mo-41414/PicoW-Rescue-NIC)](https://github.com/mar1mo-41414/PicoW-Rescue-NIC/releases/latest)

[English version → README-EN.md](README-EN.md)

---

## 🧭 このツールの大前提（最初に読んで）

これは個人の趣味で作った、**一点物の緊急復旧ツール**です。汎用の USB-WiFi ブリッジ製品ではありません。

想定している状況はひとつだけ:

> **目の前のマシンのネットワーク設定をいじっている最中にやらかして、自分で自分を締め出した。モニタもキーボードも繋がっていない（ヘッドレス運用）。でもマシン自体は生きている。**

**大前提：あなたはそのマシンの目の前にいます**（サーバールーム、ラックの前、机の上…）。これは**リモートアクセスの道具ではありません。現地に行く手間を省く道具でもありません。** そうではなく、**目の前にあるのに入れなくなったマシンに、その場で入り口を作り直す**ための道具です。モニタとキーボード（やクラッシュカート）を引っ張り出す代わりに、Pico W を挿して、手持ちのスマホ／ノートを WiFi で繋いで SSH で入る — それだけ。

Pico W を挿すと、サーバーを**一切触らずに**「もう1本の到達経路（追加 NIC）」が生え、復旧作業のための**入り口までの道**が戻ります。SSH が通る程度の最低限の到達性が戻って、設定を直して通常の管理体制に戻れれば、それでゴールです。

**ここが一番大事:**

- このツールが回復させるのは **「ネットワーク層の到達性」だけ** です。
- 使えるのは、**サーバーの内部に「ログインできる入り口」がまだ生きている場合に限ります。** その入り口は SSH である必要はありません。SSH が死んでいても、別の方法で入れるコンソール（シリアルログイン、生きている他のサービス等）が残っていれば復旧はできます。
- 逆に、**入り口が一切残っていない**（sshd も他のログイン手段も全滅、サーバーが電源断、ディスク故障でログイン不能…）なら、このツールでは何もできません。
- つまり、**「入り口はあるのに、そこへ至る道（ネットワーク）だけが壊れた」状態**を直す道具です。入り口そのものを作る道具ではありません。その入り口に**入りに行くための道**を用意します。

常設（挿しっぱなし）する前提のデバイスではありません。普段は引き出しにしまっておき、**非常時に挿して、終わったら抜く**。「常備」しておいて、いざという時に使うものです。

---

## 問題

あなたにも経験があるはず。**目の前のヘッドレスなマシン**をセットアップ中:

```
[あなた + ノート/スマホ]   [目の前のサーバー]
         ✗ ── ネットワークに入れない ── ┘
         （でもマシンは元気）
```

`ip link set eth0 down` を戻し忘れた。
静的 IP を重複させてしまった。
`ifupdown` / `NetworkManager` の設定を書き換えたら、再起動後に IP が付かなくなった。
テスト中に NetworkManager を止めたまま忘れた。
ルーティングテーブルのエントリを誤って消した。

**マシンは生きている。ディスクも正常。OS も動いている。SSH も待ち受けている。でもネットワークから到達できない。**

マシンは目の前にある。でもモニタもキーボードも繋がっていない。
クラッシュカートを探すか、誰かに頼むしかない。

---

## 解決策

**約 1,000 円の Raspberry Pi Pico W** をサーバーの空き USB ポートに挿すだけ。

```
[目の前のスマホ / ノート]
      │
      ▼ WiFi (192.168.4.0/24)
 ┌────────────┐
 │  Pico W    │  ← 4cm × 2cm、USB 給電
 │  Rescue NIC│
 └────────────┘
      │ USB CDC-NCM (10.0.0.0/24)
      ▼
[ロックアウトされたサーバー]
  - サーバーから見れば「もう1枚の NIC（10.0.0.2）」
  - この NIC 経由で SSH / curl / ping が通る
  - → 生きている入り口にlog inして設定を直す
```

Pico W はサーバーに **標準 USB イーサネットアダプター (CDC-NCM)** として認識されます。
同時に **WiFi アクセスポイント** を動作させます。
スマホやラップトップから WiFi 経由で接続し、ブリッジ越しにサーバーへ SSH して問題を修正できます。

**ドライバ不要。クラウド不要。ベンダーロックインなし。オフラインでも動作。**

> ⚠️ 注意: サーバーに新しい NIC を足しても、`iptables -P INPUT DROP` のように
> **全インターフェースを対象にした締め出し**や、**sshd 自体の停止**は救えません。
> 何が救えて何が救えないかは、次の表を必ず確認してください。

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

## 救える故障・救えない故障

このツールは「**ネットワーク層の到達性は壊れたが、OS とログインの入り口は生きている**」状態を救うためのもの。それ以外は対象外です。

### ✅ 救える（ネットワーク／NIC／ルート／リンク層の故障）

| 故障 | 理由 |
|------|------|
| NIC の IP 設定ミス / インターフェース down | 直結の別 NIC（10.0.0.0/24）から L3 到達性を回復できる |
| デフォルトルートの削除・誤ったルート追加 | 直結サブネットは経路（ルート）を必要としない |
| NetworkManager / systemd-networkd / ifupdown の停止・設定破損 | カーネル側の USB NCM と Pico 側 DHCP だけで繋がる |
| 誤った静的 IP によるサブネット衝突 | 10.0.0.0/24 は独立しているため衝突しない |
| 再起動後に IP が付かなくなった | Pico が DHCP で IP とルートを配る |
| WiFi アダプタ／ドライバの故障 | WiFi 側は Pico が持っている |
| ファイアウォールが **インターフェース限定**（`-i eth0`）で設定されていた | 新しく生えた NIC はそのルールの対象外になる |

### ❌ 救えない（入り口そのもの／電源／ストレージの故障）

| 故障 | 理由 | 代替手段 |
|------|------|----------|
| `-P INPUT DROP` / 全 IF 対象の DROP ルール | netfilter の判定は NIC を増やしても変わらない | 物理コンソール / PiKVM |
| sshd の停止・設定破損で、かつ**他に入れるコンソールも無い** | ログインできる入り口が残っていない | 物理コンソール / シリアル / PiKVM |
| ディスクフル / fsck 要求 / カーネルパニック | そもそもログインできない | PiKVM / シリアルコンソール |
| サーバー自体が電源断・起動しない | Pico は USB バスパワー。給電が無ければ Pico も動かない | 現地対応 / 電源系 |

> **`iptables` の締め出しは「救えるとは限らない」。**
> ルールが `-i <if>` でインターフェース限定なら、新しい NIC からは通ります。
> しかし `-P INPUT DROP` のように**全インターフェースに効いている**場合は、
> NIC を何枚増やしても通信は通りません。
> 使う前に、自分の環境のルール構成を確認してください。

---

## これは何ではないか（非目標）

- **汎用の USB-WiFi ブリッジではない。** 用途は「サーバーからの締め出し」の復旧に限られる。
- **常設デバイスではない。** 平常時は引き出しにしまい、非常時に挿して、終わったら抜く。
- **管理用 PC 1 台をつなぐためのもの。** 複数クライアント、社内 LAN の延長、常用回線の代替は想定しない。
- **高機能ルータではない。** Web UI、DNS フォワーダ、VPN、ポートフォワード、パケットキャプチャは実装しない。そういうものが要るなら普通のネットワーク機器を使う。
- **大量転送用ではない。** 実効 2〜4 Mbps。SCP で巨大ファイルを流すためのものではない。
- **業務用製品ではない。** 個人の趣味開発。サポート・保証・SLA は無い。

---

## ⚠️ 使う前に：デフォルトのパスワードを変更する

リポジトリに書いてある **SSID `PicoBridge` / パスワード `picobridge123` は「公開値」であって、認証情報ではありません。** README を読んだ人は全員知っています。**必ず自分の値に変えてからビルドして使ってください。**

```bash
# ファイルを一切編集せず、引数で指定して一発ビルド:
./scripts/build.sh --ssid "MyRescueNet" --pass "好きな長いパスフレーズ"
```

（詳しくは [ビルド](#ビルド) を参照。）

### これは非常口であり、使い方を誤ればバックドアにもなる

このファームを焼いた Pico W は、**USB を挿すだけでそのマシンへのネットワーク経路を作ります。** それが設計そのものです。裏を返せば、挿しっぱなしにすれば「常時開いた裏口」になり得ます。以下を守ってください。

1. **既定の SSID / パスワードを変更してから使う。**（上記）
2. **使い終わったら USB から抜く。** 常設しない。これが事実上の有効化スイッチ（アーミング）です。
3. **信頼できない場所に挿したままにしない。** WiFi の到達範囲内にいる者は AP に接続を試みられます。
4. **使用後はサーバーの状態を確認する。** 追加された NIC・設定が残っていないか、想定外のログインがないかを確認。

**脅威モデル:** これは「**作業するマシンの目の前にいる人**が、設定中に自分を締め出した時、その場で入り口を作り直すための道具」です。離れた場所から侵入するための道具ではありません（USB を挿せる＝物理的に目の前にいることが大前提であり、物理セキュリティを破るものではない）。

**記録について:** 接続の履歴は既定では残りません。復旧作業中に何が起きたかを後から見たい場合は、作業中にホスト側で CDC-ACM シリアルログを保存しておいてください（`sudo cat /dev/ttyACM0 | tee rescue-$(date +%F).log`）。

---

## 機能

| 機能 | 詳細 |
|------|------|
| 🔌 **USB イーサネット (CDC-NCM)** | Linux でプラグアンドプレイ（他 OS は[対応表](#動作確認済み環境対応表)参照） |
| 📡 **WiFi アクセスポイント** | WPA2-AES、SSID・パスワード・チャンネル設定可能 |
| 🔄 **双方向 IP ルーティング** | 自作 NAPT — SSH・curl・ping |
| 🏷️ **自動ルート配布** | DHCP オプション 121 で WiFi ルートを USB ホストに自動設定 |
| 💡 **LED ステータス表示** | 単色 LED の点滅パターンで状態／エラーコードを表示（[LED 状態表示](#led-状態表示)） |
| 🖥️ **デバッグコンソール** | CDC-ACM シリアル — 同じ USB ケーブルでファームウェアログ確認 |
| ⚡ **依存ゼロ** | OS 不要・RTOS 不要・カーネルモジュール不要 |
| 🔋 **USB バス給電** | 約 100mA — サーバー再起動後も USB 経由で自動復帰 |

---

## ユースケース

いずれも「**目の前のマシンでネットワーク設定ミスをして締め出された**状態からの、その場での緊急復旧」という同じ用途の変種です（リモートアクセス用途ではありません。冒頭の「このツールの大前提」を参照）。

### 🏠 ホームラボ・セルフホスト

- ヘッドレスのサーバー／ミニ PC のネットワーク設定をいじっていて、`iptables` / `nftables` / ルーティングのミスでログインできなくなった（[救える故障](#救える故障救えない故障)の範囲に限る）
- モニタもキーボードも常時繋いでいない機械の、**その場の非常用コンソール代わり**（スマホ1台あれば入れる）

### 🏢 サーバールーム・ラック

- ラックのヘッドレスサーバーで設定作業中に締め出した時、**目の前のマシンにその場で入り口を作り直す**
- クラッシュカート（モニタ＋キーボードの台車）や KVM が無い／空いていない時の、ポケットに入る代替

> ※ リモート OOB パスでも「常設の break-glass」でもありません。**目の前にいる前提**で、非常時に挿し、復旧したら抜く運用です。

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
- USB-A to Micro-USB ケーブル（**データ通信対応**のもの。充電専用はダメ）
- ホスト: 空き USB ポートのある Linux / macOS / Windows マシン（[対応表](#動作確認済み環境対応表)参照）

> **Pico 2 W は？** Pico 2 W (RP2350) は SDK を少し変更すれば動作するはずですが未テストです。PR 歓迎。

---

## 動作確認済み環境（対応表）

**実機は作者の手元にある分しかありません。** 下表の「✅ 検証済み」以外は、作者が実機で確認できていないという意味です（動かないという意味ではない）。手持ちの環境で動いた／動かなかった報告や PR を歓迎します。

### USB ホスト側（サーバー = CDC-NCM を認識する側）

| 環境 | 状態 | 備考 |
|------|------|------|
| Linux Ubuntu 22.04 / 24.04 (kernel 5.15+) | ✅ 検証済み | `cdc_ncm` 標準ロード。動作実績あり |
| Linux その他ディストリ（`cdc_ncm` 有効なカーネル） | ⚪ 未検証（高確率で動作） | `sudo modprobe cdc_ncm` で確認 |
| macOS | ⚪ 未検証 | CDC-NCM は標準対応のはず |
| Windows 10 (1903+) / 11 | ⚪ 未検証 | CDC-NCM 標準対応。ただし USB 再列挙・省電力まわりで不安定の報告が一般にある |
| Windows 7 / 8 / 組込 Windows | ❌ 非対応 | CDC-NCM 非対応。RNDIS が必要（未実装・ロードマップ） |
| ESXi / Windows Server / FreeBSD / OpenWrt / Proxmox / TrueNAS 等 | ❓ 未検証 | 作者が実機を持っておらず未確認。CDC-NCM ドライバの有無に依存。検証報告・PR 歓迎 |

### WiFi クライアント側（管理用 PC / スマホ = AP に接続する側）

| 環境 | 状態 | 備考 |
|------|------|------|
| iPhone (iOS) | ✅ 検証済み | |
| macOS 14 (Sonoma) | ✅ 検証済み | |
| Android | ⚪ 未検証（動く見込み） | WPA2-PSK なので一般に接続可能。[WiFi 側ルート](#wifi-側からのルート設定wifi--usb-方向)が必要 |
| Windows / Linux ノート | ⚪ 未検証（動く見込み） | 接続後に手動でルート追加が必要 |

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

> ⚠️ **ただし配布版は SSID/パスワードが既定値（`PicoBridge` / `picobridge123`）です。**
> この公開値のまま使うのは危険なので、実運用前に自分の値でビルドし直してください（下記）。

### 前提条件

```bash
# ARM ツールチェーン インストール (Ubuntu/Debian)
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential

# Pico SDK のクローン
git clone https://github.com/raspberrypi/pico-sdk --recurse-submodules
export PICO_SDK_PATH=/path/to/pico-sdk
```

### 一発ビルド（推奨）— ファイル編集なしで設定変更

`scripts/build.sh` に引数で SSID・パスワード・チャンネルを渡せます。**`CMakeLists.txt` を編集する必要はありません。**

```bash
git clone https://github.com/mar1mo-41414/PicoW-Rescue-NIC
cd PicoW-Rescue-NIC

# 自分の SSID / パスワードでビルド（必ず変えること！）
./scripts/build.sh --ssid "MyRescueNet" --pass "好きな長いパスフレーズ"

# チャンネルも指定する場合
./scripts/build.sh --ssid "MyRescueNet" --pass "..." --channel 11

# ビルドディレクトリを作り直してからビルド
./scripts/build.sh --clean --ssid "MyRescueNet" --pass "..."

# オプション一覧
./scripts/build.sh --help
```

出力: `build/picow_nic.uf2`

> パスフレーズは WPA2 の仕様上 8〜63 文字。スクリプトが長さをチェックします。
> `--pass` を省略すると既定値（公開値）のままになり、cmake が警告を出します。

### 手動ビルド（従来どおり）

```bash
mkdir build && cd build
cmake .. -DPICO_BOARD=pico_w -DCMAKE_BUILD_TYPE=Release \
  -DWIFI_SSID="MyRescueNet" -DWIFI_PASSWORD="好きな長いパスフレーズ" -DWIFI_CHANNEL=6
make -j$(nproc)
```

`-DWIFI_*` を省略すると既定値（`PicoBridge` / `picobridge123` / ch 6）が使われます。

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

スマホやラップトップから、**ビルド時に設定した SSID**（既定: `PicoBridge` / パスワード `picobridge123`）に接続。

```
あなたのデバイスに割り当てられる IP: 192.168.4.10
```

### Step 3: サーバーにアクセス

```bash
# スマホ/ラップトップ (WiFi) から
ssh user@10.0.0.2        # ロックアウトされたサーバーに SSH
curl http://10.0.0.2      # HTTP
ping 10.0.0.2             # Ping
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

## LED 状態表示

Pico W には**単色の LED が1つ**しかないので、**点滅のパターン**で状態を表します（Raspberry Pi 4 の PWR ランプのように、長い点滅と短い点滅でコードを出す方式）。

| LED の様子 | 意味 |
|------------|------|
| ほぼ点灯（2 秒に一度だけ一瞬消える） | ✅ 正常稼働中。USB ホスト認識済み＋WiFi クライアント接続済み。復旧作業できる状態 |
| 1 秒ごとの等間隔点滅 | 稼働中。WiFi クライアント未接続。AP に接続すると正常稼働へ |
| **長 1 回＋短 2 回**（繰り返し＝コード2） | USB ホストが見えない。サーバーが NIC を認識していない（OS 側 USB 未列挙／ケーブル／ポート） |
| **長 1 回＋短 3 回**（繰り返し＝コード3） | WiFi AP が起動していない。ファーム異常の可能性 |
| 完全に消灯（給電はあるのに1秒以上光らない） | WiFi チップ初期化失敗（致命的）。LED は WiFi チップ上にあるため点滅すら出せない状態 |
| 完全に消灯（そもそも光らない） | 給電なし＝サーバー電源断／USB が給電していない／ケーブル不良。または起動直後の1秒以内 |

> コードの読み方と各状態の切り分けは [`docs/troubleshooting.md`](docs/troubleshooting.md#led-ステータスコード) にまとめてあります。

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
 Pico Rescue Bridge v1.0
 RP2040 USB Rescue NIC
================================================
 USB subnet : 10.0.0.0/24
 WiFi subnet: 192.168.4.0/24  SSID=PicoBridge
================================================

WiFi AP: SSID=PicoBridge  Pico=192.168.4.1  Client=192.168.4.10
Network: IP forwarding enabled (IP_FORWARD=1)
Network: NAPT active
Network: WiFi TX checksum wrapper installed

Ready — connect PC to USB and/or WiFi AP "PicoBridge"
LED: near-solid=OK  1Hz blink=waiting for WiFi client
LED: long+2short=USB host not seen  long+3short=WiFi AP down

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
├── CMakeLists.txt          ビルド設定（WiFi 設定はキャッシュ変数 = -D で上書き可）
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
│   ├── status_led.c/h      単色 LED のステータス／点滅コード表示
│   ├── usb_descriptors.c/h 複合 USB 記述子（IAD）
│   └── stdio_cdc.c         printf → CDC-ACM シリアル
│
├── docs/
│   ├── architecture.md     設計判断、データフロー図（日本語）
│   ├── investigation.md    バグ解析：チェックサム、NAT、lwIP 内部（日本語）
│   ├── troubleshooting.md  よくある問題と解決策 + LED コード表（日本語）
│   ├── spec.md             完全仕様書（日本語）
│   └── release-notes-v1.0.md リリースノート（日本語）
│   └── *-EN.md             各ドキュメントの英語版
│
├── images/                 写真、図
└── scripts/
    ├── build.sh            引数で設定を渡す一発ビルドスクリプト
    └── flash.sh            ワンコマンドフラッシュスクリプト
```

---

## 既知の制限事項

| 制限 | 備考 |
|------|------|
| WiFi クライアント 1 台のみ | DHCP 固定 IP のため。管理用 1 台で足りる前提 |
| USB Full Speed | 物理 12 Mbps 制限、NAT スループット 2–4 Mbps 程度 |
| IPv6 非対応 | `LWIP_IPV6=0` — IPv4 のみ。IPv6 のみの環境では詰む（後日対応予定） |
| WiFi クライアントの手動ルート | WiFi→USB 方向は手動 `ip route add` が必要（USB→WiFi は DHCP opt 121 で自動） |
| 設定はビルド時 | SSID/パス/チャンネルはビルド時に決まる（ただし `build.sh` の引数で、ファイル編集なしに指定可） |
| 追加認証なし | WiFi は WPA2 だがブリッジ自体には追加の認証層なし。使用時間を短く・長い PSK で代替 |
| 監査ログなし | 接続履歴は既定で残らない（[記録について](#これは非常口であり使い方を誤ればバックドアにもなる)参照） |
| Pico W のみ | RP2040 + CYW43439 でのみテスト済み |

救える／救えない故障の切り分けは [救える故障・救えない故障](#救える故障救えない故障) を参照。

---

## ロードマップ / TODO

- [ ] **IPv6 対応** — まず SLAAC（要否判断）
- [ ] **複数 WiFi クライアント** — DHCP リーステーブル実装
- [ ] **RNDIS サポート** — 古い Windows バージョン向け
- [ ] **Pico 2 W (RP2350)** — 移植・テスト
- [ ] **実機対応表の拡充** — ESXi / Windows Server / BSD 等（実機をお持ちの方の検証・PR 歓迎）
- [x] **ビルドなしでの設定変更** — `build.sh` の引数で対応（SSID/パス/チャンネル）
- [x] **LED ステータス表示** — 点滅コードで状態表示

---

## コントリビュート

Issue や PR は歓迎です。提出前に:

1. [`docs/architecture.md`](docs/architecture.md) で設計の背景を確認
2. [`docs/investigation.md`](docs/investigation.md) で既知の NAT/チェックサムの注意点を確認
3. 提出前に両方向で ping + SSH のベーシックテストを実行
4. 手持ちの OS/ハードで動いた／動かなかった報告は [動作確認済み環境](#動作確認済み環境対応表) の拡充に役立ちます

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
> *写真: Pico Wと、Pico Wを刺す前のサーバーとip route *
[`images/1.png`](images/1.png) 

> *写真: Pico Wを刺したサーバーのip route (ルートが追加されている)*
[`images/2.png`](images/2.png) 

> *写真: Pico WのAPに接続したiPhoneのWiFi設定 *
[`images/3.png`](images/3.png) 

> *写真: Pico W経由で接続したSSHセッション (neofetchと、PCからiPhoneへのpingを実行) *
[`images/4.png`](images/4.png) 

> *写真: Pico W経由でiPhoneへping*
[`images/5.png`](images/5.png) 

</details>

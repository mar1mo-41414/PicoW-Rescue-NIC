# リリースノート — v1.0

**リリース日:** 2026-05-18

英語版: [release-notes-v1.0-EN.md](release-notes-v1.0-EN.md)

---

## 概要

PicoW-Rescue-NIC の初回安定版リリース。すべてのコア機能が動作確認済みです。

1,000 円の Raspberry Pi Pico W が、プラグアンドプレイの緊急ネットワークアクセスデバイスになります:  
USB イーサネットを一方に、WiFi アクセスポイントをもう一方に、  
その間で双方向 IP ルーティング/NAPT を実行します。

---

## 動作確認済み機能

- ✅ **USB イーサネット（CDC-NCM）** — Linux・macOS・Windows 10+ でドライバ不要のプラグアンドプレイ
- ✅ **WiFi アクセスポイント** — WPA2-AES、SSID・パスワード・チャンネル設定可能
- ✅ **双方向 NAPT** — ping・SSH・curl・SCP が双方向（USB↔WiFi）で動作
- ✅ **DHCP オプション 121** — WiFi サブネットルートを USB ホストに自動インストール（`ip route add` 不要）
- ✅ **デバッグコンソール** — CDC-ACM シリアルでリアルタイムファームウェアログ（同じ USB ケーブル）
- ✅ **詳細ログ** — パケット単位トレースモード（`VERBOSE_LOG=1`）

---

## 開発中に修正したバグ

v1.0 前に 6 つの非自明なバグを発見・修正しました。詳細は [`docs/investigation.md`](investigation.md) を参照してください。

| # | バグ | 影響 |
|---|-----|------|
| 1 | `ip4_forward()` による USB TX チェックサムのゼロ化 | USB 側のパケットをすべて Linux がドロップ |
| 2 | `ip4_forward()` による WiFi TX チェックサムのゼロ化 | WiFi 側のパケットをすべて Mac がドロップ |
| 3 | NAT における ICMP echo ID のダブルバイトスワップ | ICMP reply の ID が誤り; ping が 100% ロスと表示 |
| 4 | TCP/UDP NAT 照合のバイトオーダー誤り | SSH/curl が動作しない; ICMP は偶然のバグ相殺で動作 |
| 5 | USB ホストに WiFi サブネットルートなし | 起動のたびに手動 `ip route add` が必要 |
| 6 | （調査）lwIP のフック順序 | NAT フックがルーティング判断より前に呼ばれることを確認 |

バグ 1〜4 の根本原因は 1 つの lwIP 設計前提です:  
`ip4_forward()` はハードウェアオフロードを前提としてチェックサムをゼロ化しますが、  
TinyUSB CDC-NCM も CYW43439 もそれを実装していません。  
修正は両インターフェースの linkoutput パスにソフトウェアチェックサムエンジンを追加することです。

---

## 既知の制限事項

- **WiFi クライアント 1 台のみ** — DHCP が固定 IP を 1 件のみ割り当て。2 台目は静的 IP 設定で接続可能
- **IPv4 のみ** — `LWIP_IPV6=0`
- **USB Full Speed** — 物理 12 Mbps、NAT スループット 2〜4 Mbps 程度
- **WiFi→USB 方向のルート** — WiFi クライアントは `10.0.0.x` へのルートを手動設定が必要（USB→WiFi は自動）
- **Pico W のみ** — RP2040 + CYW43439 でのみテスト済み。Pico 2 W（RP2350）は未テスト

---

## v1.1 以降のロードマップ

- [ ] 複数 WiFi クライアント（DHCP リーステーブル）
- [ ] ステーションモード（上流 WiFi に接続して USB にブリッジ）
- [ ] Web ステータスページ（lwIP httpd）
- [ ] DNS プロキシ
- [ ] 古い Windows 向け RNDIS サポート
- [ ] Pico 2 W（RP2350）移植

---

## GitHub リポジトリ説明文

> ネットワークが壊れたサーバーへの緊急脱出口。Raspberry Pi Pico W（約1,000円）を USB ポートに挿すだけで WiFi 経由のアクセスを確保。ドライバ不要・クラウド不要。

英語版:
> Emergency USB-WiFi bridge NIC for servers. Plug a $6 Raspberry Pi Pico W into any USB port — get WiFi access to your locked-out machine, no drivers, no cloud.

## トピックタグ

```
raspberry-pi-pico  pico-w  rp2040  tinyusb  lwip  cdc-ncm  wifi
network-bridge  napt  out-of-band  homelab  rescue  embedded-c
```

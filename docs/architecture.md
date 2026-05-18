# アーキテクチャ — PicoW-Rescue-NIC

このドキュメントではファームウェアの設計判断、データフロー、内部構造を説明します。

英語版: [architecture-EN.md](architecture-EN.md)

---

## システム全体像

```
┌───────────────────────────────────────────────────────────────┐
│                     Raspberry Pi Pico W                        │
│                                                               │
│  ┌────────────────────┐        ┌──────────────────────────┐  │
│  │      TinyUSB        │        │    lwIP  (NO_SYS=1)      │  │
│  │  ┌──────────────┐  │        │  ┌────────┐ ┌─────────┐  │  │
│  │  │  CDC-NCM     │◄─┼────────┼─►│usb_netif│ │wifi_netif│  │  │
│  │  │  (イーサネット)│  │        │  │10.0.0.1 │ │192.168.4│  │  │
│  │  └──────────────┘  │        │  └────────┘ └─────────┘  │  │
│  │  ┌──────────────┐  │        │      ↕  IP_FORWARD  ↕     │  │
│  │  │  CDC-ACM     │  │        │      nat.c  (NAPT)         │  │
│  │  │  (シリアル)   │  │        │  LWIP_HOOK_IP4_INPUT       │  │
│  │  └──────────────┘  │        └──────────────────────────┘  │
│  └────────────────────┘                    │                  │
│           │ USB                     CYW43 ドライバ             │
│           │                               │ WiFi              │
└───────────┼───────────────────────────────┼──────────────────┘
            │                               │
       [USB ホスト]                   [WiFi クライアント]
        10.0.0.2                      192.168.4.10
```

---

## 各レイヤーの役割

### TinyUSB（USB デバイススタック）

**役割:** Pico W を複合 USB デバイスとして提示します。

| インターフェース | クラス | 機能 |
|----------------|-------|------|
| CDC-NCM (IAD) | Communications | USB イーサネット NIC（CDC ネットワーク制御モデル） |
| CDC-ACM | Communications | デバッグシリアルコンソール（`printf` → `/dev/ttyACM*`） |

CDC-NCM を RNDIS より選択した理由:
- CDC-NCM は正式な IEEE 標準であり、Linux・macOS・Windows 10+ でドライバ不要
- RNDIS は Microsoft 独自プロトコルで古い Windows ではドライバが必要

複合デバイスは IAD（インターフェースアソシエーション記述子）を使用し、  
OS が CDC-NCM の 2 つのインターフェース（Communication + Data）を  
1 つの論理デバイスとして正しく認識できるようにしています。

**主要コールバック — `tud_network_recv_cb()`**: TinyUSB が USB 経由でイーサネットフレームを受信したときに呼ばれます。`usb_netif->input()` を通じて直接 lwIP に渡します。

**主要コールバック — `tud_network_xmit_cb()`**: TinyUSB がキューされたフレームを送信できるようになったときに呼ばれます。pbuf のペイロードを USB 転送バッファにコピーします。

### lwIP（TCP/IP スタック）

**役割:** IP ネットワーキング、ルーティング、DHCP、NAT の全体。

設定: `NO_SYS=1`（RTOS なし、協調ポーリングモード）。すべてのドライバはメインループからポーリングされます — 割り込みなし、スレッドなし。

**2 つのネットワークインターフェース:**

| Netif | 名前 | IP | ドライバ |
|-------|------|----|---------|
| USB | `us` | `10.0.0.1/24` | `usb_net.c` |
| WiFi | `w1` | `192.168.4.1/24` | CYW43 (`pico_cyw43_arch_lwip_poll`) |

`IP_FORWARD=1` によりインターフェース間のパケット転送が有効になります。  
あるインターフェースにパケットが届き、別のインターフェース経由で到達可能なホスト宛の場合、`ip4_forward()` がルーティングします。

**DHCP サーバー:** インターフェースごとに独立した 2 つの DHCP サーバーインスタンス（`dhcpserver.c` を使用）。どちらも固定 IP 割り当てです。

### nat.c — 双方向 NAPT

**役割:** 2 つのサブネット間のネットワークアドレス・ポート変換。

lwIP 組み込みの NAPT（`ip4_napt.c`）は Pico SDK の lwIP スナップショットに含まれていません（上流 2.1.x 以降で追加）。そのため `src/nat.c` に最小限かつ完全な NAPT を独自実装しています。

NAT フックは `lwipopts.h` の `LWIP_HOOK_IP4_INPUT` で登録:

```c
#define LWIP_HOOK_IP4_INPUT(p, inp) nat_ip4_input_hook(p, inp)
```

このフックは `ip4.c:491` で呼ばれます。`ip_data.current_iphdr_dest` がコピーされる 549 行目より前です。この順序が重要で、フックが `iphdr->dest.addr` を書き換えると、その後のルーティング判断は書き換え後の宛先を使います。

---

## NAT データフロー

### 4 つの変換関数

| トリガー条件 | 関数 | 処理内容 |
|------------|------|---------|
| USB 着信、dst が WiFi サブネット | `nat_outbound` | USB クライアント src → Pico WiFi IP (`192.168.4.1`)、NAT ポート割当 |
| WiFi 着信、dst が Pico WiFi IP | `nat_inbound` | 元の USB クライアント IP を NAT テーブルから復元 |
| WiFi 着信、dst が USB サブネット | `nat_outbound2` | WiFi クライアント src → Pico USB IP (`10.0.0.1`)、NAT ポート割当 |
| USB 着信、dst が Pico USB IP | `nat_inbound2` | dst を元の WiFi クライアント IP に書き換え、lwIP が WiFi に転送 |

### パケットフロー例: WiFi クライアント → USB サーバーへ SSH

```
WiFi クライアント (192.168.4.10) が送信:
  src=192.168.4.10:52000 dst=10.0.0.2:22 proto=TCP

nat_outbound2():
  新しい src = 10.0.0.1:49152 (Pico USB IP, NAT ポート)
  NAT テーブル: {orig_src=192.168.4.10, orig_sport=52000, nat_port=49152}

USB サーバー (10.0.0.2) に転送:
  src=10.0.0.1:49152 dst=10.0.0.2:22

USB サーバーが応答:
  src=10.0.0.2:22 dst=10.0.0.1:49152

nat_inbound2():
  nat_port=49152 を検索 → orig_src=192.168.4.10, orig_sport=52000
  dst 書き換え: 10.0.0.1 → 192.168.4.10
  lwIP が WiFi インターフェースに転送

WiFi クライアントが受信:
  src=10.0.0.2:22 dst=192.168.4.10:52000  ✓
```

### NAT テーブル構造

```c
typedef struct {
    uint32_t  orig_src;      // 元の送信元 IP（ホストバイトオーダー）
    uint32_t  orig_dst;      // 元の宛先 IP
    uint16_t  orig_sport;    // 元の送信元ポート / ICMP ID（BE 読み出し値）
    uint16_t  orig_dport;    // 元の宛先ポート
    uint16_t  nat_port;      // 割り当て NAT ポート（ホストバイトオーダー、49152-65535）
    uint8_t   proto;         // IP_PROTO_TCP / IP_PROTO_UDP / IP_PROTO_ICMP
    bool      active;
    uint32_t  last_seen_ms;  // TTL 期限切れ用
} nat_entry_t;
```

テーブルサイズ: `NAT_TABLE_SIZE`（デフォルト 64 エントリ）。TTL: TCP 120 秒、UDP 30 秒、ICMP 10 秒。

---

## チェックサムアーキテクチャ

### 問題

lwIP の `ip4_forward()`（ip4.c:332–366）は `CHECKSUM_GEN_*=1` のときすべてのチェックサムをゼロ化します:

```c
// ip4.c:340 — CHECKSUM_GEN_IP=1 のとき
IPH_CHKSUM_SET(iphdr, 0);  // IP チェックサムをゼロ（HW オフロード前提）
// TCP, UDP, ICMP も同様
```

これは NIC がハードウェアでチェックサムを計算する（チェックサムオフロード）前提の設計です。  
TinyUSB の CDC-NCM ドライバも CYW43439 WiFi ドライバも、ハードウェアチェックサムオフロードを実装していません。

### 解決策: ソフトウェアチェックサムエンジン

インターフェースごとに 2 つのソフトウェアチェックサムラッパー:

**USB 側（`usb_net.c`）:** `usb_fill_checksums()` を `usb_netif_linkoutput()` 内で、各フレームを TinyUSB に渡す前に呼び出します。

**WiFi 側（`network.c`）:** `wifi_ap_init()` が CYW43 AP をセットアップした後、`network_init()` が元の `wifi->linkoutput` ポインタを保存して `wifi_linkoutput_chksum()` に差し替えます。この関数が `wifi_fill_checksums()` を呼んでから元の CYW43 linkoutput に委譲します。

両エンジンとも同一ロジック:
1. `ethertype == 0x0800`（IPv4）を確認
2. IP ヘッダチェックサムを再計算（`inet_chksum`）
3. プロトコルに応じてトランスポートチェックサムを再計算:
   - **ICMP**: `inet_chksum(icmph, icmp_payload_len)`
   - **TCP**: `inet_chksum_pseudo()`（IP 疑似ヘッダを含む）
   - **UDP**: `inet_chksum_pseudo()`（チェックサムフィールドが 0 の場合はスキップ）

`LWIP_NETIF_TX_SINGLE_PBUF=1` により送信時の pbuf は常に単一セグメントが保証されるため、チェーン pbuf の処理は不要です。

---

## 協調ポーリングループ

RTOS なし。メインループがすべてのドライバをポーリング:

```c
while (true) {
    tud_task();            // TinyUSB: USB 列挙、NCM rx/tx
    cyw43_arch_poll();     // CYW43: WiFi ドライバ + lwIP rx/tx
    sys_check_timeouts();  // lwIP: TCP keepalive, ARP, DHCP 更新
    status_task();         // 15 秒ごとの UART ステータス
}
```

**重要な制約:** `usb_netif_linkoutput()` の中で `tud_task()` を呼んではなりません。

`tud_task() → tud_network_recv_cb() → DHCP ハンドラ → lwIP 出力 → usb_netif_linkoutput()` という呼び出しチェーンは正常です。しかし `linkoutput` の中から再び `tud_task()` を呼ぶと、TinyUSB の内部 DMA 状態を壊す再帰的な呼び出しになります。

USB TX バッファが満杯（`tud_network_can_xmit()` が false）の場合、`linkoutput` は `ERR_BUF` を返します。上位プロトコル（TCP、DHCP）が再送を処理します。

---

## DHCP オプション 121（Classless Static Route）

RFC 3442 で定義される DHCP オプション 121 を使い、DHCP クライアントに静的ルートのインストールを指示します。これにより `192.168.4.0/24 via 10.0.0.1` を USB ホストのルーティングテーブルに自動で追加します。

このオプションがなければ、複数の NIC を持つ Linux ホストは WiFi サブネット向けトラフィックを Pico の USB インターフェース経由でルーティングすべきとわからず、再起動のたびに手動で `ip route add` が必要になります。

Linux の systemd-networkd と dhclient はどちらもオプション 121 に対応しています。ルートは `proto dhcp` として現れ、DHCP リースとともに管理（更新・削除）されます。

---

## USB 記述子

デバイスは複合デバイス（bDeviceClass=0xEF、IAD）として提示します。文字列記述子インデックス 6 が CDC-NCM MAC アドレスを ASCII で保持:

```
MAC: 02:02:84:69:60:00  →  文字列: "020284696000"
```

この MAC は `tud_network_mac_address[6]`（ホストから見た Pico の「リモート」MAC）にもコピーされます。ローカル MAC（ホスト側）は OS がネゴシエーションします。

MAC プレフィックス `02:xx` はローカル管理ビットを立てており、登録済み OUI 範囲との衝突を避けます。

---

## なぜ RTOS なしか

TinyUSB と lwIP はどちらも協調ポーリングモードを提供しており、クリーンに組み合わせられます:

- `tud_task()` は保留中の USB イベントをすべて処理して返る
- `cyw43_arch_poll()`（`lwip_poll` モード）は WiFi イベントと lwIP タイマーを処理する
- `sys_check_timeouts()` は保留中の lwIP タイマーコールバックを起動する

FreeRTOS を追加した場合の追加コスト:
- lwIP コアへのスレッドセーフアクセス（ミューテックスまたは `LWIP_TCPIP_CORE_LOCKING=1`）
- TinyUSB の USB ISR と lwIP ネットワークコールバック間の注意深い同期
- FreeRTOS カーネル最低 ~4 KB の RAM オーバーヘッド

このユースケース（複雑なアプリケーションロジックのない小型ブリッジデバイス）では、協調ポーリングがより単純で予測可能な動作をオーバーヘッドなしに提供します。

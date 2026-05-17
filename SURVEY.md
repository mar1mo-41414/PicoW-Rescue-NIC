# PicoW-NIC v1.0 調査・実装詳細

## 目的

Raspberry Pi Pico W を USB CDC-NCM NIC + WiFi AP ブリッジとして機能させ、  
USB 側 PC と WiFi 側デバイス間で TCP/UDP/ICMP の双方向通信を実現する。

---

## アーキテクチャ概要

```
[USB PC]                          [WiFi デバイス]
  10.0.0.2                           192.168.4.10
     │                                    │
  CDC-NCM (USB Ethernet)          WiFi AP (CYW43439)
     │                                    │
  usb_netif (10.0.0.1)         wifi_netif (192.168.4.1)
     └──────────────┬───────────────────┘
                    │
             lwIP IP_FORWARD=1
                    │
              nat.c (NAPT)
         LWIP_HOOK_IP4_INPUT
```

スタック:
- **TinyUSB** (0.15+): USB CDC-NCM デバイス実装
- **lwIP** (NO_SYS=1): IP スタック、ポーリングモード
- **CYW43 ドライバ**: Pico SDK 同梱の WiFi ドライバ
- **nat.c**: 独自 NAPT 実装（pico-sdk の lwIP に ip4_napt.c が含まれないため）

---

## 調査で判明した問題と解決策

### 問題 1: USB TX チェックサムが 0 になる

**症状**: Linux が USB から受け取る ICMP/TCP/UDP パケットのチェックサムが 0。  
tcpdump で `bad cksum 0 (->xxxx)!` が表示され、Linux がパケットをドロップ。

**原因**: lwIP の `ip4_forward()` (ip4.c:332–366) は、`CHECKSUM_GEN_IP/TCP/UDP=1` のとき  
転送パケットのチェックサムをすべて 0 にゼロ化する。  
これはハードウェアチェックサムオフロード (NIC が補完する) 前提の設計。  
TinyUSB の CDC-NCM ドライバはソフトウェアのみで、チェックサムを補完しない。

```c
// ip4.c:340（CHECKSUM_GEN_IP=1 のとき）
if (CHECKSUM_GEN_IP || NETIF_CHECKSUM_ENABLED(inp, NETIF_CHECKSUM_GEN_IP)) {
    IPH_CHKSUM_SET(iphdr, 0);   // IP チェックサムをゼロ化
}
// 同様に TCP, UDP, ICMP もゼロ化
```

**解決策**: `usb_net.c` の USB `linkoutput` にソフトウェアチェックサムエンジン  
`usb_fill_checksums()` を追加。送信直前に IP/ICMP/TCP/UDP チェックサムをすべて再計算。

```c
static err_t usb_netif_linkoutput(struct netif *netif, struct pbuf *p) {
    ...
    usb_fill_checksums(p);   // ← ip4_forward がゼロにしたチェックサムを再計算
    tud_network_xmit(p, 0);
    return ERR_OK;
}
```

---

### 問題 2: WiFi TX チェックサムが 0 になる (Mac がパケットをドロップ)

**症状**: Mac → Linux ping は通るのに、Linux → Mac への応答が返らない（Mac 側でドロップ）。

**原因**: 問題 1 と同根。`ip4_forward()` が WiFi TX パケットのチェックサムもゼロ化するが、  
CYW43 ドライバにもハードウェアチェックサムオフロード機能がない。  
USB 方向は `usb_fill_checksums()` で対処済みだが、WiFi 方向は未対処だった。

**解決策**: `network.c` の `network_init()` で WiFi netif の `linkoutput` をラップ。  
`wifi_fill_checksums()` → 元の CYW43 linkoutput の順で呼ぶことで  
WiFi TX でも全チェックサムをソフトウェア再計算する。

```c
// network.c
static netif_linkoutput_fn wifi_orig_linkoutput = NULL;

static err_t wifi_linkoutput_chksum(struct netif *netif, struct pbuf *p) {
    wifi_fill_checksums(p);              // チェックサムを再計算
    return wifi_orig_linkoutput(netif, p);
}

void network_init(void) {
    ...
    struct netif *wifi = wifi_ap_get_netif();
    wifi_orig_linkoutput = wifi->linkoutput;
    wifi->linkoutput = wifi_linkoutput_chksum;   // ← ラップ
}
```

---

### 問題 3: ICMP ID の復元でバイト逆転バグ

**症状**: ICMP チェックサムは正しくなったが、Mac の ping に reply が届かない。

**原因**: NAT の inbound 処理（`nat_inbound`/`nat_inbound2`）で ICMP echo ID を復元する際、  
`htons(e->orig_sport)` を使っていた。`orig_sport` はパケットバイトのビッグエンディアン読み出し  
`(tp[4]<<8)|tp[5]` で格納されており、これに `htons()` をかけるとバイトが逆転する。

```c
// 修正前（バグ）
uint16_t new_id = htons(e->orig_sport);  // バイト逆転
tp[4] = (uint8_t)(new_id >> 8);
tp[5] = (uint8_t)(new_id & 0xff);

// 修正後
uint16_t new_id = e->orig_sport;   // そのままビッグエンディアンバイト列として書き込む
tp[4] = (uint8_t)(new_id >> 8);
tp[5] = (uint8_t)(new_id & 0xff);
```

---

### 問題 4: TCP/UDP の NAT テーブル照合失敗（SSH/curl が通らない）

**症状**: ping (ICMP) は通るが SSH/curl (TCP) が通らない。

**原因**: `nat_inbound`/`nat_inbound2` の `find_inbound()` 呼び出しに余計な `ntohs()` があった。

TCP ポートはパケットバイトのビッグエンディアン読み出し `(tp[2]<<8)|tp[3]` により、  
`e->nat_port`（`alloc_port()` のホストバイトオーダー値）と一致する整数になる。  
ここに `ntohs()` をかけると LE 環境でバイトが逆転し、照合に失敗する。

```
例: nat_port = 49152 (0xC000)
TCP パケットバイト: [0xC0, 0x00]  →  dport = (0xC0<<8)|0x00 = 0xC000 = 49152
ntohs(49152) on LE = 0x00C0 = 192  ← nat_port=49152 と一致しない!
```

ICMP は outbound 側で `htons(nat_port)` を手動バイト書き込みしていたため、  
偶然 inbound 側の `ntohs()` と相殺されて動いていた。TCP はこの偶然がなかった。

**解決策**:
1. `nat_outbound`/`nat_outbound2` の ICMP id 書き込みを `htons()` なしに統一
2. `nat_inbound`/`nat_inbound2` の `find_inbound()` 呼び出しから `ntohs()` を除去

```c
// 修正前
nat_entry_t *e = find_inbound(orig_dst_he, ntohs(dport), proto);

// 修正後
nat_entry_t *e = find_inbound(orig_dst_he, dport, proto);
```

これにより ICMP/TCP/UDP すべてで同一の照合規約となった。

---

### 問題 5: Linux が WiFi サブネットへのルートを持たない

**症状**: Linux から `ping 192.168.4.10` が失敗（ルートなし）。  
毎回 `sudo ip route add 192.168.4.0/24 via 10.0.0.1` が必要。

**原因**: USB 側の DHCP サーバーがデフォルトゲートウェイ (option 3) は配布していたが、  
Linux が複数 NIC を持つ場合、他の NIC のデフォルトルートが優先され  
`192.168.4.x` 向けルートが設定されなかった。

**解決策**: DHCP オプション 121（Classless Static Route, RFC 3442）を実装。  
`dhcpserver.h` に `route_net / route_prefix_len / route_gw` フィールドを追加し、  
`usb_net_init()` で USB 側 DHCP に `192.168.4.0/24 via 10.0.0.1` を設定。

```c
// dhcpserver.c: send_reply() 内
if (d->route_prefix_len > 0) {
    uint8_t sig = (d->route_prefix_len + 7u) / 8u;
    o[n++] = OPT_CLASSLESS_ROUTE;
    o[n++] = (uint8_t)(1u + sig + 4u);
    o[n++] = d->route_prefix_len;
    // 有効ネットワークオクテット + ゲートウェイ IP を書き込み
    ...
}

// usb_net.c: usb_net_init() 内
IP4_ADDR(&usb_dhcp.route_net, 192, 168, 4, 0);
usb_dhcp.route_prefix_len = 24;
IP4_ADDR(&usb_dhcp.route_gw, 10, 0, 0, 1);
```

Linux 側での確認:
```bash
ip route | grep 192.168.4
# → 192.168.4.0/24 via 10.0.0.1 dev picow proto dhcp src 10.0.0.2 metric 101
```

---

### 問題 6: LWIP_HOOK_IP4_INPUT と ip4_forward の動作順序

**調査内容**: NAT フックがパケットの宛先 IP を書き換えた後、  
lwIP が正しく `ip4_forward()` を呼ぶかどうかの確認。

**lwIP ip4_input() の処理順序** (ip4.c):

```
491: LWIP_HOOK_IP4_INPUT(p, inp)   ← フック呼び出し（ここで NAT が宛先書き換え）
549: ip_addr_copy_from_ip4(ip_data.current_iphdr_dest, iphdr->dest)  ← 書き換え後の dst を使用
577: ip4_input_accept(inp)          ← "このホスト宛か？" チェック
652: if (netif == NULL) → ip4_forward()  ← 宛先が自ホストでなければ転送
```

フックは `current_iphdr_dest` のコピーより**前**に呼ばれるため、  
フックが `iphdr->dest.addr` を書き換えれば、その後の転送判定は書き換え後の宛先で行われる。  
これにより `nat_inbound2` が宛先を `10.0.0.1→192.168.4.10` に書き換えた後、  
lwIP が正しく WiFi 方向に `ip4_forward()` する。

---

## NAPT の設計

### テーブル構造

```c
typedef struct {
    uint32_t  orig_src;    // オリジナル送信元 IP（ホストバイトオーダー）
    uint32_t  orig_dst;    // オリジナル宛先 IP
    uint16_t  orig_sport;  // オリジナル送信元ポート / ICMP ID（BE 読み出し値）
    uint16_t  orig_dport;  // オリジナル宛先ポート
    uint16_t  nat_port;    // NAT 割当ポート（ホストバイトオーダー）
    uint8_t   proto;       // IP_PROTO_TCP / UDP / ICMP
    bool      active;
    uint32_t  last_seen_ms;
} nat_entry_t;
```

### 4 方向のフロー

| フック条件 | 処理関数 | 処理内容 |
|-----------|---------|---------|
| inp=USB, dst∈WiFi subnet | `nat_outbound` | USB クライアント src → WiFi IP (192.168.4.1) にマスカレード |
| inp=WiFi, dst=WiFi IP | `nat_inbound` | WiFi IP dst → 元の USB クライアント IP に復元 |
| inp=WiFi, dst∈USB subnet | `nat_outbound2` | WiFi クライアント src → USB IP (10.0.0.1) にマスカレード |
| inp=USB, dst=USB IP (10.0.0.1) | `nat_inbound2` | USB IP dst → 元の WiFi クライアント IP に復元 |

### チェックサムのインクリメンタル更新

RFC 1624 に基づく差分チェックサム更新:

```c
static uint16_t chksum_adjust(uint16_t chksum, uint16_t old_val, uint16_t new_val) {
    uint32_t c = (~chksum & 0xffffu) + (~old_val & 0xffffu) + new_val;
    while (c >> 16) c = (c & 0xffffu) + (c >> 16);
    return (uint16_t)(~c & 0xffffu);
}
```

ただし、`usb_fill_checksums()` / `wifi_fill_checksums()` がどちらも  
linkoutput 時にチェックサムをフルリコンピュートするため、  
nat.c のインクリメンタル更新は精度の保証よりも「0 でないこと」が重要。

### TTL とチェックサムの処理

`ip4_forward()` は:
1. TTL をデクリメントし、IP チェックサムをインクリメンタル更新
2. `CHECKSUM_GEN_IP=1` のとき IP チェックサムをゼロ上書き（HW オフロード前提）
3. `CHECKSUM_GEN_ICMP/TCP/UDP=1` のとき各チェックサムをゼロ上書き

このためすべての forwarded パケットのチェックサムが 0 になる。  
`CHECKSUM_GEN_ICMP=0` を設定することで ICMP チェックサムのゼロ化は抑制できるが、  
根本解決は linkoutput ラッパーによる再計算であるため、副作用防止の意味で設定を維持している。

---

## ソフトウェアチェックサムエンジンの実装

`usb_fill_checksums()` / `wifi_fill_checksums()` は同一ロジック：

1. Ethernet フレームが IPv4 かどうかを確認（ethertype=0x0800）
2. IP ヘッダのチェックサムを 0 クリアして `inet_chksum()` で再計算
3. プロトコルに応じてトランスポート層チェックサムを再計算:
   - **ICMP**: `inet_chksum(icmph, payload_len)`
   - **TCP**: `inet_chksum_pseudo()` (IP 疑似ヘッダ込み)
   - **UDP**: `inet_chksum_pseudo()` (チェックサム無効 `0` の場合は再計算スキップ)

`LWIP_NETIF_TX_SINGLE_PBUF=1` により送信 pbuf は常に連続メモリが保証されるため、  
チェーン pbuf への対応は不要。

---

## USB 記述子構成

複合デバイスとして 2 インターフェースを提供:

| インターフェース | 用途 |
|----------------|------|
| CDC-NCM (IAD) | Ethernet NIC（IP 通信用） |
| CDC-ACM | シリアルコンソール（デバッグ用 printf） |

CDC-NCM の MAC アドレスは `02:02:84:69:60:00` に固定。  
USB 文字列記述子のインデックス 6 に ASCII 形式で格納し、TinyUSB の  
`tud_network_mac_address[6]` と一致させる必要がある。

---

## 協調ポーリングループ

RTOS を使わない `NO_SYS=1` モードで動作:

```c
while (true) {
    tud_task();            // TinyUSB: USB イベント処理
    cyw43_arch_poll();     // CYW43: WiFi ドライバ + lwIP RX/TX
    sys_check_timeouts();  // lwIP: TCP keepalive, ARP キャッシュ, DHCP 更新
    status_task();         // 15 秒ごとのステータス出力
}
```

`usb_netif_linkoutput` 内で `tud_task()` を呼んではならない。  
`tud_task()` → `tud_network_recv_cb()` → DHCP ハンドラ → `linkoutput` → `tud_task()` の  
再帰呼び出しが発生し TinyUSB の内部状態を破壊する。  
送信バッファが満杯 (`tud_network_can_xmit()=false`) の場合は `ERR_BUF` を返し、  
上位プロトコル（TCP/DHCP）の再送に委ねる。

---

## デバッグ時の診断手順

### パケットが届かない場合

1. `VERBOSE_LOG=1` でリビルドし、シリアルコンソールを確認
2. `NAT ICMP: inp=xx` ログで NAT フックが呼ばれているか確認
3. `USB TX: N bytes ready=1 can_xmit=1` で USB 送信が成功しているか確認
4. tcpdump で実際のパケット内容を確認：
   ```bash
   sudo tcpdump -i <usb_if> -n icmp -vv
   # bad cksum が出る場合 → チェックサムエンジンの問題
   ```

### NAT テーブルが枯渇する場合

`nat.h` の `NAT_TABLE_SIZE`（デフォルト 64）を増やす。  
エントリのタイムアウトは `NAT_TCP_TTL_MS` / `NAT_UDP_TTL_MS` / `NAT_ICMP_TTL_MS` で調整。

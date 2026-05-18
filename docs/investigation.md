# 調査・バグ解析ログ — PicoW-Rescue-NIC

v1.0 ファームウェアに至るまでのデバッグ過程を記録します。  
各バグの症状・根本原因・修正内容を詳細に記載しています。  
同様の問題に直面するコントリビューターのために保存しています。

英語版: [investigation-EN.md](investigation-EN.md)

---

## バグ 1: USB TX チェックサムが 0 になる

### 症状

Pico が USB CDC-NCM NIC として列挙され、Linux が DHCP リースを取得した後、  
Pico から Linux が受け取る ICMP/TCP/UDP パケットのチェックサムがすべて 0 になる。

```bash
sudo tcpdump -i picow -n icmp -vv
# 出力:
# 192.168.4.1 > 10.0.0.2: ICMP echo reply, bad cksum 0 (->a3c1)!
```

Linux はこれをサイレントにドロップします。ping 応答が見えません。

### 根本原因

lwIP の `ip4_forward()`（`lwip/src/core/ipv4/ip4.c` 332〜366 行付近）は、  
`CHECKSUM_GEN_IP / CHECKSUM_GEN_TCP / CHECKSUM_GEN_UDP = 1` のとき  
転送パケットのすべてのチェックサムフィールドをゼロ化します:

```c
// ip4.c:340
if (CHECKSUM_GEN_IP || NETIF_CHECKSUM_ENABLED(inp, NETIF_CHECKSUM_GEN_IP)) {
    IPH_CHKSUM_SET(iphdr, 0);   // IP チェックサムをゼロ — NIC が補完する前提
}
// TCP, UDP, ICMP も同様のパターン
```

これは意図的な設計です: NIC がハードウェアチェックサムオフロードに対応している場合、  
スタックがパケットを渡した後にドライバがチェックサムを補完します。  
TinyUSB の CDC-NCM 実装にはその仕組みがなく、受け取ったものをそのまま送信します。

### 修正

`usb_net.c` の `usb_netif_linkoutput()` で、TinyUSB に渡す前に  
すべてのチェックサムをソフトウェアで再計算します:

```c
static err_t usb_netif_linkoutput(struct netif *netif, struct pbuf *p) {
    if (!tud_ready())                       return ERR_USE;
    if (!tud_network_can_xmit(p->tot_len))  return ERR_BUF;
    usb_fill_checksums(p);   // ip4_forward がゼロ化したチェックサムを再計算
    tud_network_xmit(p, 0);
    return ERR_OK;
}
```

`usb_fill_checksums()` は IPv4 のみ処理（`ethertype == 0x0800` を確認）し、  
IP ヘッダチェックサムを先に再計算してから、ICMP/TCP/UDP をプロトコルに応じて  
`inet_chksum` / `inet_chksum_pseudo` で再計算します。  
`LWIP_NETIF_TX_SINGLE_PBUF=1` により送信 pbuf は常に単一セグメントが保証されます。

---

## バグ 2: WiFi TX チェックサムが 0 になる（Mac がパケットをドロップ）

### 症状

バグ 1 を修正後、Mac → Linux の ping は通るようになった。  
しかし Linux → Mac が失敗: Mac はパケットを受け取るがサイレントにドロップする。  
Mac 側の tcpdump でチェックサムが 0 と表示される。

### 根本原因

バグ 1 と同じ根本原因ですが、もう一方のインターフェースです。  
`ip4_forward()` は WiFi（CYW43）インターフェース経由で送信するパケットのチェックサムもゼロ化します。  
CYW43439 ドライバにもハードウェアチェックサムオフロード機能がありません。

バグ 1 の修正は USB の linkoutput パスのみ対処していました。  
WiFi の linkoutput パスは未対処のままでした。

### 修正

`network_init()`（起動時に呼ばれる）で WiFi netif の `linkoutput` 関数ポインタをラップします:

```c
// network.c
static netif_linkoutput_fn wifi_orig_linkoutput = NULL;

static err_t wifi_linkoutput_chksum(struct netif *netif, struct pbuf *p) {
    wifi_fill_checksums(p);                  // 送信前にチェックサムを再計算
    return wifi_orig_linkoutput(netif, p);   // CYW43 ドライバに委譲
}

void network_init(void) {
    nat_init();
    struct netif *wifi = wifi_ap_get_netif();
    if (wifi && wifi->linkoutput) {
        wifi_orig_linkoutput = wifi->linkoutput;
        wifi->linkoutput = wifi_linkoutput_chksum;
    }
}
```

このラッピングは `wifi_ap_init()` が `cyw43_arch_enable_ap_mode()` を呼んだ **後**  
に行う必要があります。CYW43 ドライバがそのタイミングで自身の `linkoutput` ポインタをインストールするためです。  
早すぎると `wifi->linkoutput` が NULL になります。

---

## バグ 3: ICMP Echo Reply の ID にダブルバイトスワップ

### 症状

チェックサムを修正後、Mac → Linux の ping は通るようになった。  
しかし Linux からの ICMP echo reply が Mac の ping のリクエスト ID と一致せず、  
Mac の `ping` はパケットが届いているのに 100% パケットロスと報告する。

詳細ログ:
```
NAT ICMP: inp=us src=10.0.0.2 dst=10.0.0.1 type=0
USB TX: 78 bytes
```
パケットは Mac に届いているが、`ping` が「ID が違う」として破棄する。

### 根本原因

`nat_inbound()` と `nat_inbound2()` で ICMP echo ID を復元する際、以下のように記述していた:

```c
uint16_t new_id = htons(e->orig_sport);   // バグ
tp[4] = (uint8_t)(new_id >> 8);
tp[5] = (uint8_t)(new_id & 0xff);
```

`e->orig_sport` はパケットバイトから `(tp[4] << 8) | tp[5]` として格納されています。  
これは「ビッグエンディアン整数として読んだ値」であり、  
`new_id >> 8` と `new_id & 0xff` を `tp[4]`/`tp[5]` に書き戻すと元のバイト列が正確に再現されます。

ここに `htons()` を適用するとリトルエンディアン CPU でバイトが入れ替わり、パケットに誤った ID が書き込まれます。

### 修正

`htons()` を除去:

```c
uint16_t new_id = e->orig_sport;   // すでに正しいバイト順
tp[4] = (uint8_t)(new_id >> 8);
tp[5] = (uint8_t)(new_id & 0xff);
```

`nat_inbound` と `nat_inbound2` の両方に適用。

---

## バグ 4: TCP/UDP の NAT テーブル照合失敗（SSH が通らない）

### 症状

バグ 3 を修正後、ping は双方向で通るようになった。  
しかし SSH と curl（TCP）は依然として失敗 — 接続が即座にタイムアウトする。

詳細ログでは SYN パケットが Pico に届いて outbound NAT が処理されているが、  
サーバーからの SYN-ACK に対して inbound NAT 照合が起きない（パケットがドロップまたは誤配信）。

### 根本原因

`nat_inbound()` と `nat_inbound2()` の `find_inbound()` 呼び出しに余分な `ntohs()` があった:

```c
nat_entry_t *e = find_inbound(orig_dst_he, ntohs(dport), proto);  // バグ
```

TCP/UDP 宛先ポートはパケットバイトから `dport = (tp[2] << 8) | tp[3]` として読まれます。  
これは `e->nat_port`（`alloc_port()` で 49152〜65535 の範囲でホストオーダーで割り当て）と同じ整数になります。

ここに `ntohs()` を適用するとリトルエンディアン環境でバイトが入れ替わります。例:

```
nat_port = 49152  (0xC000)
TCP パケットバイト: [0xC0, 0x00]  →  dport = 0xC000 = 49152
ntohs(49152) on LE = 0x00C0 = 192   ← nat_port=49152 と一致しない!
```

**なぜ ICMP だけ偶然動いていたのか:**  
outbound 側（`nat_outbound`）で ICMP ID を `htons(nat_port)` で書き込んでいたため、  
偶然に誤ったバイト順でパケットに書かれていました。  
reply が戻ると ID バイトは `[0x00, 0xC0]` → `dport = 0x00C0 = 192`。  
`ntohs(192) = 0xC000 = 49152 = nat_port` — 2 つのバグが打ち消し合って動いていました。  
TCP にはこの偶然の相殺がなく、失敗していました。

### 修正

統一的な規約を確立するための 2 段階修正:

**その 1 — `find_inbound` 呼び出しから `ntohs()` を除去:**

```c
// nat_inbound() と nat_inbound2() 両方:
nat_entry_t *e = find_inbound(orig_dst_he, dport, proto);  // ntohs なし
```

**その 2 — ICMP outbound の ID 書き込みを統一規約に修正:**

```c
// nat_outbound() と nat_outbound2() 両方:
// 修正前（inbound の ntohs で偶然相殺されていた）:
uint16_t new_id = htons(e->nat_port);

// 修正後（nat_port をそのままビッグエンディアンバイト列として書き込む）:
uint16_t new_id = e->nat_port;
tp[4] = (uint8_t)(new_id >> 8);
tp[5] = (uint8_t)(new_id & 0xff);
```

この修正後、ICMP/TCP/UDP すべてで同一の照合規約が成立します:  
`dport`（パケットバイトを MSB 先読み）== `nat_port`（ホストオーダーの整数）。

---

## バグ 5: Linux が WiFi サブネットへのルートを持たない

### 症状

複数の NIC を持つ Linux ホスト（例: eth0 + Pico の `picow`）で Pico を接続後、  
ホストは DHCP アドレス（`10.0.0.2/24`）を取得するが `192.168.4.0/24` へのルートがない。  
`ping 192.168.4.10` が「ネットワーク到達不能」で失敗する。

回避策として毎回以下が必要だった:
```bash
sudo ip route add 192.168.4.0/24 via 10.0.0.1
```
しかしこれは再起動後に消えるため、セッションごとに繰り返す必要があった。

### 根本原因

DHCP サーバーはオプション 3（デフォルトゲートウェイ = `10.0.0.1`）しか送信していませんでした。  
複数の NIC を持つ Linux はインターフェースごとのデフォルトゲートウェイを、  
そのインターフェースのサブネット外の宛先には使いません（カーネルのルーティングテーブルを使う）。  
オプション 3 だけでは WiFi サブネット向けのホスト固有ルートはインストールされません。

### 修正

DHCP オプション 121（Classless Static Route、RFC 3442）を実装。  
このオプションは DHCP クライアントに対して、デフォルトゲートウェイとは独立した  
特定の静的ルートをインストールするよう指示します。

**dhcpserver.h** — `dhcp_server_t` にルートフィールドを追加:
```c
ip4_addr_t  route_net;
uint8_t     route_prefix_len;   // 0 = 無効
ip4_addr_t  route_gw;
```

**dhcpserver.c** — `send_reply()` でオプション 121 を構築:
```c
if (d->route_prefix_len > 0) {
    uint8_t sig = (d->route_prefix_len + 7u) / 8u;  // 有効オクテット数
    o[n++] = OPT_CLASSLESS_ROUTE;                    // 121
    o[n++] = (uint8_t)(1u + sig + 4u);              // 長さ
    o[n++] = d->route_prefix_len;                    // プレフィックスビット (24)
    for (uint8_t i = 0; i < sig; i++)
        o[n++] = ip4_addr_get_byte(&d->route_net, i); // 192, 168, 4
    o[n++] = ip4_addr1(&d->route_gw); // 10
    o[n++] = ip4_addr2(&d->route_gw); // 0
    o[n++] = ip4_addr3(&d->route_gw); // 0
    o[n++] = ip4_addr4(&d->route_gw); // 1
}
```

**usb_net.c** — USB DHCP サーバーに WiFi サブネットルートを設定:
```c
IP4_ADDR(&usb_dhcp.route_net, 192, 168, 4, 0);
usb_dhcp.route_prefix_len = 24;
IP4_ADDR(&usb_dhcp.route_gw,  10, 0, 0, 1);
```

この修正後、まっさらな Linux マシンで:
```bash
ip route | grep 192.168.4
# → 192.168.4.0/24 via 10.0.0.1 dev picow proto dhcp src 10.0.0.2 metric 101
```
ルートが自動でインストールされ、DHCP クライアントにより管理され、再起動後も維持されます。

---

## 調査 6: LWIP_HOOK_IP4_INPUT と ip4_forward の動作順序

### 疑問

NAT フック（`LWIP_HOOK_IP4_INPUT`）が `iphdr->dest.addr` を書き換えた後、  
lwIP のルーティング判断はフックの前後どちらで行われるのか？  
前なら、宛先の書き換えは意味がない。

### 解析

`lwip/src/core/ipv4/ip4.c` の `ip4_input` 関数:

```
491 行: LWIP_HOOK_IP4_INPUT(p, inp)           ← フックはここで呼ばれる
...
549 行: ip_addr_copy_from_ip4(                 ← current_iphdr_dest のコピーはここ
            ip_data.current_iphdr_dest,
            iphdr->dest)
...
577 行: ip4_input_accept(inp)                  ← "自ホスト宛か？" チェック
652 行: if (netif == NULL) → ip4_forward()     ← 転送判断
```

フックは 491 行目で呼ばれます — `current_iphdr_dest` のセット（549 行目）と  
accept/forward 判断（652 行目）より**前**です。  
フック内で `iphdr->dest.addr` を変更すると、その後のすべての処理は書き換え後の値を使います。

これが重要で: `nat_inbound2()` が宛先を `10.0.0.1`（Pico USB IP）から  
`192.168.4.10`（WiFi クライアント）に書き換えると、フックが返った後に  
lwIP は宛先を `192.168.4.10` と認識し、USB インターフェースのローカルアドレスでないため  
`ip4_forward()` を呼んで WiFi インターフェースに転送します。WiFi クライアントはパケットを正しく受信します。

### 結論

lwIP のフック配置は NAT に適するよう意図的に設計されています。  
順序の回避策は不要です。フックは送信元・宛先 IP とポートを自由に書き換えられ、  
lwIP は書き換え後の値に基づいてルーティングします。

---

## 全修正のまとめ

| # | バグ | 症状 | 修正 |
|---|-----|------|------|
| 1 | USB TX チェックサム=0 | Linux がすべての転送パケットをドロップ | USB linkoutput に `usb_fill_checksums()` |
| 2 | WiFi TX チェックサム=0 | Mac がすべての転送パケットをドロップ | CYW43 linkoutput を `wifi_fill_checksums()` でラップ |
| 3 | ICMP ID ダブルバイトスワップ | ICMP reply が誤った ID で到着 | inbound ID 復元の `htons()` を除去 |
| 4 | TCP/UDP NAT 照合失敗 | SSH/curl タイムアウト、ICMP のみ偶然動作 | `find_inbound` の `ntohs()` 除去、ICMP outbound 書き込みを統一 |
| 5 | Linux に WiFi ルートなし | 手動 `ip route add` が毎回必要 | `dhcpserver.c` に DHCP オプション 121 を実装 |
| 6 | (調査) | フック順序の不確かさ | フックがルーティング判断より前に呼ばれることを確認 |

# 名刺サイト設計（既存 smart_business_card への統合）

電子ペーパー名刺デバイスは、別途稼働している NFC 名刺サイトと連携する。
新規にサーバを立てるのではなく、**既存資産に最小限の追加** で済ませる。

## 1. 既存資産

| 項目 | 内容 |
|------|------|
| **公開 URL** | https://ambit.go2020.tokyo/card/ |
| **リポジトリ** | https://github.com/ambit1977/smart_business_card |
| **ローカルパス** | `/Users/ambit/Documents/個人開発/IoT/NFCタグ名刺` |
| **スタック** | Next.js 14 (Pages Router) + Tailwind + 静的 export |
| **ホスティング** | さくらVPS (AlmaLinux 9.4) + Apache `Alias /card → /var/www/ambit.go2020.tokyo-card` |
| **デプロイ** | `./deploy.sh` (npm run build → rsync → chown) |
| **プロフィール管理** | `lib/profile.js` 1 ファイル、`scripts/build-vcard.cjs` で `public/contact.vcf` 自動生成 |
| **NFC タグ** | NTAG213/215 に **固定 URL** `https://ambit.go2020.tokyo/card/` を 1回書き込み |

## 2. 追加するもの（最小構成）

| 追加要素 | 役割 |
|---------|------|
| **`/card/now.json`** | 「今いる場所 / イベント / トピック」の動的データ（静的 export と並存） |
| **`/card/api/set.php`** | now.json を更新する Bearer 認証 API（PHP 30行程度） |
| **`/card/admin/`** | 秋山スマホ用の管理画面（PWA、Next.js の追加ページ）|
| **`index.html` での fetch** | クライアント側で now.json を取得して動的表示（CSR） |

すべて既存の `Alias /card` 配下に同居。SSL 証明書も Apache 設定も既存のまま。

---

## 3. URL 構造（統合後）

```
https://ambit.go2020.tokyo/card/
  ├── index.html              ← 既存：vCard ダウンロード、SNS、Now 表示も追加
  ├── contact.vcf             ← 既存：ビルド時自動生成
  ├── avatar.svg              ← 既存
  ├── now.json                ← 新規：動的、{ place, venue, event, topic, ... }
  ├── admin/                  ← 新規：スマホ用 PWA、認証付き
  │   └── index.html
  └── api/
      ├── set.php             ← 新規：Bearer 認証で now.json を書き換え
      └── get.php (optional)  ← 新規：now.json をそのまま返すラッパー（CORS 用）
```

---

## 4. now.json データモデル

```json
{
  "version": "v1",
  "current": {
    "place": "渋谷",
    "venue": "WeWork 渋谷スクランブルスクエア",
    "event": "IoT Conference 2026",
    "topic": "Edge AI × E-Paper の話",
    "since": "2026-05-28T13:00:00+09:00",
    "until": "2026-05-28T18:00:00+09:00",
    "public": true
  },
  "updated_at": "2026-05-28T12:55:32+09:00"
}
```

すべてのフィールド任意（空文字なら省略）。`public: false` の時は index.html や `/now` ページに出さない。

---

## 5. `/api/set.php`（PHP 最小実装）

```php
<?php
// /var/www/ambit.go2020.tokyo-card/api/set.php
// Bearer Token で認証して now.json を上書き

declare(strict_types=1);

header('Content-Type: application/json; charset=utf-8');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: POST, OPTIONS');
header('Access-Control-Allow-Headers: Authorization, Content-Type');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(204);
    exit;
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    http_response_code(405);
    echo json_encode(['error' => 'method not allowed']);
    exit;
}

// Bearer token check
$expected = trim(@file_get_contents('/etc/ambit-card/admin_token') ?: '');
$auth = $_SERVER['HTTP_AUTHORIZATION'] ?? '';
if (!$expected || $auth !== "Bearer {$expected}") {
    http_response_code(401);
    echo json_encode(['error' => 'unauthorized']);
    exit;
}

$payload = json_decode(file_get_contents('php://input') ?: '', true);
if (!is_array($payload) || !isset($payload['current'])) {
    http_response_code(400);
    echo json_encode(['error' => 'invalid payload']);
    exit;
}

$now = [
    'version'    => 'v1',
    'current'    => $payload['current'],
    'updated_at' => date('c'),
];

$target = '/var/www/ambit.go2020.tokyo-card/now.json';
$tmp    = $target . '.tmp';
file_put_contents($tmp, json_encode($now, JSON_UNESCAPED_UNICODE | JSON_PRETTY_PRINT));
rename($tmp, $target);

echo json_encode(['ok' => true, 'updated_at' => $now['updated_at']]);
```

### サーバ側準備

```sh
# トークンを秘密ファイルに保存
sudo mkdir -p /etc/ambit-card
sudo sh -c 'openssl rand -hex 32 > /etc/ambit-card/admin_token'
sudo chown apache:apache /etc/ambit-card/admin_token
sudo chmod 640 /etc/ambit-card/admin_token

# PHP モジュール導入（必要時）
sudo dnf install -y php php-fpm
sudo systemctl enable --now php-fpm
sudo systemctl reload httpd

# now.json 初期化
echo '{"version":"v1","current":{"public":false},"updated_at":""}' \
  | sudo tee /var/www/ambit.go2020.tokyo-card/now.json
sudo chown alma:apache /var/www/ambit.go2020.tokyo-card/now.json
sudo chmod 664 /var/www/ambit.go2020.tokyo-card/now.json
```

Apache の Alias 設定に追加：

```apache
# /card/api 以下では PHP を有効化
<Directory /var/www/ambit.go2020.tokyo-card/api>
    AllowOverride All
    Require all granted
    AddHandler php-script .php
    DirectoryIndex index.php
</Directory>
```

---

## 6. `/admin/` の設計（Next.js 追加ページ）

`pages/admin.jsx` を追加して、スマホ最適化の状況入力フォームを作る。

```
pages/
  ├── _app.jsx          (既存)
  ├── _document.jsx     (既存)
  ├── index.jsx         (既存 名刺ページ)
  └── admin.jsx         ★追加 管理画面
```

### admin ページの仕様

- 初回アクセス: URL クエリ `?token=xxx` を localStorage に保存し、URL を `replaceState` でクリーンに
- 以降: localStorage のトークンを `Authorization: Bearer ...` に付けて POST
- 入力項目: place / venue / event / topic / public（チェック）/ 期限プリセット
- プリセットボタン: 「自宅」「オフィス」「外出先」「展示会」などをワンタップで反映
- PWA 化: `next-pwa` または手書きの `manifest.json` + Service Worker

### admin.jsx の骨格

```jsx
import { useEffect, useState } from 'react';

const ENDPOINT = '/card/api/set.php';

export default function Admin() {
  const [token, setToken] = useState(null);
  const [current, setCurrent] = useState({
    place: '', venue: '', event: '', topic: '', public: true
  });
  const [busy, setBusy] = useState(false);

  // 初回ロード: URL ?token=xxx を localStorage に保存
  useEffect(() => {
    if (typeof window === 'undefined') return;
    const p = new URLSearchParams(window.location.search);
    const t = p.get('token') || localStorage.getItem('ambit_card_admin_token');
    if (t) {
      localStorage.setItem('ambit_card_admin_token', t);
      setToken(t);
      if (p.has('token')) {
        history.replaceState({}, '', window.location.pathname);
      }
    }
  }, []);

  const submit = async () => {
    setBusy(true);
    try {
      const res = await fetch(ENDPOINT, {
        method: 'POST',
        headers: {
          'Authorization': `Bearer ${token}`,
          'Content-Type': 'application/json'
        },
        body: JSON.stringify({ current })
      });
      if (!res.ok) throw new Error(await res.text());
      alert('更新しました');
    } catch (e) {
      alert('失敗: ' + e.message);
    } finally {
      setBusy(false);
    }
  };

  // ... 入力 UI 略
}
```

### プリセット

```js
const PRESETS = {
  '自宅':     { place: '自宅',          event: '', topic: '' },
  'オフィス': { place: '渋谷オフィス',  event: '', topic: '' },
  '外出先':   { place: '',              event: '', topic: '' },
  '展示会':   { place: '東京ビッグサイト', event: 'CEATEC', topic: '' },
};
```

---

## 7. `index.html` から now.json を反映する

既存 `pages/index.jsx` に「ステータスバナー」を追加して、クライアント側で now.json を取りに行く。
静的 export のままで OK（Next.js は CSR で fetch できる）。

```jsx
const [now, setNow] = useState(null);

useEffect(() => {
  fetch(asset('/now.json'))
    .then((r) => r.ok ? r.json() : null)
    .then((j) => {
      if (j?.current?.public) setNow(j.current);
    })
    .catch(() => {});
}, []);
```

レンダリング側：

```jsx
{now && (
  <section className="px-6 mt-4">
    <div className="rounded-2xl border border-accent/40 bg-accent/5 px-4 py-3 text-sm">
      <div className="text-xs uppercase tracking-widest text-accent">Now</div>
      <div className="mt-1 font-medium">
        {now.place}
        {now.venue && `（${now.venue}）`}
      </div>
      {now.event && <div className="text-xs text-gray-600">{now.event}</div>}
      {now.topic && <div className="text-xs text-gray-600 mt-1">“{now.topic}”</div>}
    </div>
  </section>
)}
```

これで NFC タップでカードページを開いた相手のスマホに「今ここ」が表示される。

### vCard への反映（任意・将来）

`scripts/build-vcard.cjs` を「クライアント側で動的生成」に切り替えるか、または
サーバサイドで vCard を組み立てる `/card/api/vcard.php` を別途用意し、
NOTE フィールドに `now.json` の内容を埋め込む選択肢もある。
ただし MVP は静的 vCard + 動的 "Now" バナーで十分。

---

## 8. ESP32 側の API 利用

### 取得（GET）

```cpp
bool fetchCurrentContext(CurrentContext& ctx) {
  HTTPClient http;
  http.begin("https://ambit.go2020.tokyo/card/now.json");
  int code = http.GET();
  if (code != 200) { http.end(); return false; }

  JsonDocument doc;
  deserializeJson(doc, http.getString());
  http.end();

  ctx.place = doc["current"]["place"].as<String>();
  ctx.event = doc["current"]["event"].as<String>();
  ctx.topic = doc["current"]["topic"].as<String>();
  ctx.venue = doc["current"]["venue"].as<String>();
  return true;
}
```

### 更新（POST、ボタン押下時のみ）

ESP32 から直接 now.json を更新したい場合（書き換えトリガーボタンの押下時）：

```cpp
bool postContextUpdate(const String& place, const String& event, const String& topic) {
  HTTPClient http;
  http.begin("https://ambit.go2020.tokyo/card/api/set.php");
  http.addHeader("Authorization", "Bearer " + String(ADMIN_TOKEN));
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["current"]["place"] = place;
  doc["current"]["event"] = event;
  doc["current"]["topic"] = topic;
  doc["current"]["public"] = true;

  String body;
  serializeJson(doc, body);
  int code = http.POST(body);
  http.end();
  return code == 200;
}
```

- `ADMIN_TOKEN` は `config.h` に書き、リポジトリには含めない
- 通常時は GET のみ、ボタン押下時のみ POST する運用

---

## 9. デプロイの流れ

既存 deploy.sh を拡張：

```bash
#!/bin/bash
# deploy.sh - 静的 export + PHP API + now.json 初期化
set -e

REMOTE_HOST="sakura-vps"
REMOTE_DIR="/var/www/ambit.go2020.tokyo-card"

echo "▶ ローカルビルド"
npm run build

echo "▶ rsync で out/ を転送（既存）"
rsync -avz --delete \
  --exclude='.DS_Store' \
  --exclude='now.json' \
  out/ "${REMOTE_HOST}:${REMOTE_DIR}/"

echo "▶ api/ を別途配置（PHP は build 対象外なので独立同期）"
rsync -avz --exclude='.DS_Store' api/ "${REMOTE_HOST}:${REMOTE_DIR}/api/"

echo "▶ パーミッション"
ssh "$REMOTE_HOST" \
  "sudo chown -R alma:apache ${REMOTE_DIR} && \
   sudo find ${REMOTE_DIR} -type d -exec chmod 755 {} \\; && \
   sudo find ${REMOTE_DIR} -type f -exec chmod 644 {} \\; && \
   sudo chmod 664 ${REMOTE_DIR}/now.json 2>/dev/null || true"
```

now.json は **デプロイで消さない** ように rsync 対象から除外。

---

## 10. 認証戦略まとめ

| エンドポイント | 認証 | 用途 |
|--------------|------|------|
| `GET /card/`, `/card/now.json` | なし（公開） | 名刺ページ閲覧、ESP32 / 他クライアントが取得 |
| `POST /card/api/set.php` | Bearer Token | 状態更新（秋山スマホ / ESP32 ボタン） |
| `GET /card/admin/` | クライアント側で token 確認、サーバはガードしない | 管理 UI（HTML 自体は公開でも token がないと API 叩けない） |

URL に `?token=xxx` を含めるのは初回のみ。以降 localStorage 保持。

---

## 11. 既存リポジトリへの追加コミット案

`smart_business_card` リポジトリへ：

```
+ pages/admin.jsx              # 管理画面
+ pages/api-fallback.jsx       # (任意) JS 無効環境向け
+ api/set.php                  # PHP エンドポイント
+ api/.htaccess                # PHP ハンドラ宣言（必要時）
+ public/manifest.json         # PWA 化
+ public/sw.js                 # Service Worker (最小)
~ pages/index.jsx              # now.json fetch + バナー追加
~ deploy.sh                    # api/ も同期、now.json 保護
~ DEPLOY.md                    # PHP / トークン設定の節を追加
~ README.md                    # /admin と now.json の説明
```

このリポジトリ（電子ペーパー側）からは、`docs/` の参照リンクで補足する。

---

## 11b. トークン発行 / 追跡 API（名刺交換ログ）

電子ペーパー名刺デバイスからの名刺交換時、ユニークトークンを発行して
NTAG215 に書き込む URL に埋め込む。相手の訪問時にトークン経由で文脈を紐づける。

### 追加エンドポイント

| Path | 認証 | 役割 |
|------|------|------|
| `POST /card/api/issue-token.php` | Bearer Token | ESP32 がトークン発行を要求 |
| `GET /card/?t={token}` | なし | 相手のスマホがアクセス、訪問記録 |
| `GET /card/api/context.php?t={token}` | なし | 名刺ページ JS から、トークンに紐づく状況取得 |
| `GET /card/admin/log` | Bearer Token | 交換履歴ダッシュボード（秋山用） |

### `issue-token.php`

```php
<?php
declare(strict_types=1);
header('Content-Type: application/json; charset=utf-8');

// Bearer 認証（set.php と同じ）
$expected = trim(@file_get_contents('/etc/ambit-card/admin_token') ?: '');
$auth = $_SERVER['HTTP_AUTHORIZATION'] ?? '';
if (!$expected || $auth !== "Bearer {$expected}") {
    http_response_code(401);
    echo json_encode(['error' => 'unauthorized']);
    exit;
}

$body = json_decode(file_get_contents('php://input') ?: '', true) ?: [];

// 8 byte ランダムを hex 化（16文字）
$token = bin2hex(random_bytes(8));

$entry = [
    'token'           => $token,
    'issued_at'       => date('c'),
    'issued_location' => $body['location'] ?? '',
    'issued_event'    => $body['event'] ?? '',
    'issued_topic'    => $body['topic'] ?? '',
    'status'          => 'issued',
];

// JSON Lines に追記
$logFile = '/var/www/ambit.go2020.tokyo-card/_data/tokens.jsonl';
@mkdir(dirname($logFile), 0775, true);
file_put_contents(
    $logFile,
    json_encode($entry, JSON_UNESCAPED_UNICODE) . "\n",
    FILE_APPEND | LOCK_EX
);

echo json_encode([
    'token' => $token,
    'url'   => "https://ambit.go2020.tokyo/card/?t={$token}",
]);
```

### トークン受信時の記録

`/card/?t={token}` でアクセスされた時、JS から context.php を呼ぶ。
サーバ側で tokens.jsonl に "opened" イベントを追記：

```php
// /card/api/context.php
$token = $_GET['t'] ?? null;
if (!$token || !preg_match('/^[0-9a-f]{16}$/', $token)) {
    http_response_code(400);
    echo json_encode(['error' => 'invalid token']);
    exit;
}

// 該当トークンを探す
$entry = lookupToken($token);
if (!$entry) {
    http_response_code(404);
    echo json_encode(['error' => 'unknown token']);
    exit;
}

// 初回アクセスなら "opened" を append
appendEvent($token, [
    'opened_at'  => date('c'),
    'ip'         => $_SERVER['REMOTE_ADDR'] ?? '',
    'user_agent' => $_SERVER['HTTP_USER_AGENT'] ?? '',
]);

echo json_encode([
    'token'           => $token,
    'issued_at'       => $entry['issued_at'],
    'issued_location' => $entry['issued_location'],
    'issued_event'    => $entry['issued_event'],
    'issued_topic'    => $entry['issued_topic'],
]);
```

### `/card/index.html` での読み込み

```js
useEffect(() => {
  const url = new URL(window.location.href);
  const token = url.searchParams.get('t');
  if (!token) return;
  fetch(`./api/context.php?t=${token}`)
    .then((r) => r.ok ? r.json() : null)
    .then((ctx) => {
      if (ctx) setExchangeContext(ctx);
    });
}, []);
```

`exchangeContext` を画面に表示：
```jsx
{exchangeContext && (
  <div className="text-xs text-gray-500">
    {exchangeContext.issued_at} に
    {exchangeContext.issued_location && ` 「${exchangeContext.issued_location}」 で`}
    {exchangeContext.issued_event && `（${exchangeContext.issued_event}）`}
    お渡ししました
  </div>
)}
```

### tokens.jsonl ストレージ

```jsonl
{"token":"abc123...","issued_at":"2026-05-30T14:23+09:00","issued_location":"渋谷","status":"issued"}
{"token":"abc123...","opened_at":"2026-05-30T14:25+09:00","ip":"203.0.113.42","user_agent":"Mozilla/..."}
{"token":"abc123...","downloaded_at":"2026-05-30T14:26+09:00"}
```

- 1行 = 1 イベント
- 同じ token が複数行に登場（issued / opened / downloaded など）
- 集計時は token でグルーピング

月次でローテート：
```bash
# crontab で
0 0 1 * * mv /var/www/.../tokens.jsonl /var/www/.../tokens-$(date +%Y-%m).jsonl
```

### `/card/admin/log`

JSON Lines を読んで集計表示。秋山スマホで「今日 N 人と交換、内 M 人が開いた」を見られる。

```
+──────────────────────────────────────────────+
│ 名刺交換ログ                                   │
+──────────────────────────────────────────────+
│ Today:     5 issued / 3 opened / 1 saved     │
│ This week: 28 issued / 19 opened             │
│                                              │
│ Recent:                                      │
│ 14:23  渋谷 / IoT Conf       opened 2:32後   │
│ 11:45  自宅                  opened 5:10後   │
│  9:15  渋谷オフィス          unopened ⏳     │
+──────────────────────────────────────────────+
```

## 12. 将来拡張

- `index.html` で fetch する `/now.json` を Cache-Control で 60秒キャッシュ
- 過去履歴ログ（`/log.json`）の自動アーカイブ
- LINE / Slack 通知連携（場所変更を SNS に投稿）
- vCard の NOTE フィールドへの動的反映（`/api/vcard.php`）
- ESP32 の `taps_today` を ESP32 から POST し、index に表示

---

## 13. 関連ドキュメント

- 全体構成: [smart_business_card_design.md](smart_business_card_design.md)
- ハードウェア: [bare_module_design.md](bare_module_design.md)
- ケース: [case_design_spec.md](case_design_spec.md)
- 既存サイト: https://github.com/ambit1977/smart_business_card

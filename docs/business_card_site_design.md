# 名刺サイト設計（akiyama.<domain>）

ESP32 名刺デバイスと連携する Web サービスの設計。
本人が意図的に「今の状況」を発信し、ESP32 / `/now` ページ両方から参照される
"信頼できる唯一の情報源" (Single Source of Truth) として機能する。

---

## 1. 役割

1. **本人の現状を一元管理**（場所・イベント・トピック）
2. **ESP32 名刺デバイスの参照先**（API として JSON を返す）
3. **公開ステータスページ `/now`** として SNS にも貼れる
4. **自己紹介 / 連絡先の単一情報源**（vCard の元データ）

---

## 2. URL 構造

| Path | 公開 | 内容 |
|------|------|------|
| `/` | 公開 | プロフィール、SNS、QR、自己紹介 |
| `/now` | 公開 (public フラグによる) | 「今、何してる」状態ページ |
| `/admin` | 認証 | スマホで状況更新する管理画面 |
| `/api/now` | 公開（読み取り専用） | ESP32 と外部が取得する JSON |
| `/api/set` | 認証（Bearer Token） | 状況更新の POST |

---

## 3. データモデル

```json
{
  "version": "v1",
  "owner": {
    "name": "Taishi Akiyama",
    "company": "ezmag",
    "title": "Engineer / Designer",
    "email": "ambit.akiyama@gmail.com",
    "phone": "+81-90-XXXX-XXXX",
    "urls": [
      "https://akiyama.example.com",
      "https://linkedin.com/in/ambit",
      "https://twitter.com/ambit"
    ]
  },
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

### フィールドの意味

| フィールド | 必須 | 用途 |
|----------|-----|------|
| `place` | ✓ | 大雑把な場所（渋谷、自宅、東京駅 など） |
| `venue` | - | 具体的な会場名 |
| `event` | - | イベント名 |
| `topic` | - | 今日の話題、活動内容 |
| `since` / `until` | - | 有効期間（未設定なら永続） |
| `public` | ✓ | `/now` ページに公開するか |

ESP32 はこの JSON を `/api/now` から取得し、NDEF NOTE を組み立てる。

---

## 4. 管理画面 `/admin`（スマホ最適化）

### 画面構成

```
┌─────────────────────────────────────┐
│  ⚙️  Akiyama Status                  │
├─────────────────────────────────────┤
│  📍 場所         [ 渋谷            ] │
│  🏢 会場         [ WeWork 渋谷     ] │
│  🎫 イベント     [ IoT Conf 2026   ] │
│  💬 トピック     [ Edge AI...      ] │
│  ⏰ 期限         [ 今日中   ▼     ] │
│  ☑ /now に公開する                  │
│                                     │
│  [   今の状況として設定する   ]      │
│                                     │
│  📝 プリセット                       │
│  [自宅][オフィス][外出先][クリア]    │
└─────────────────────────────────────┘
```

### プリセット

```javascript
const PRESETS = {
  '自宅':     { place: '自宅',          event: '', topic: '' },
  'オフィス': { place: '渋谷オフィス',    event: '', topic: '' },
  '外出先':   { place: '',              event: '', topic: '' },
  '展示会':   { place: '東京ビッグサイト', event: 'CEATEC',    topic: '...' }
};
```

### PWA 化

- `manifest.json` + Service Worker
- スマホのホーム画面に追加可能
- アイコンタップで即起動
- オフライン時は最後の状態を表示

### 認証

- Bearer Token を `Authorization` ヘッダに付ける
- ブラウザの `localStorage` に保存
- 初回だけ URL `?token=xxx` で渡し、保存後は URL を `replaceState` で消す
- トークンは Cloudflare の環境変数 `ADMIN_TOKEN` と照合

---

## 5. ESP32 側の API 利用

```cpp
struct CurrentContext {
  String place;
  String event;
  String topic;
  String venue;
};

bool fetchCurrentContext(CurrentContext& ctx) {
  HTTPClient http;
  http.begin("https://akiyama.example.com/api/now");
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

String formatVcardNote(const CurrentContext& ctx, const String& timestamp, int exchangeCount) {
  String note = timestamp;
  if (!ctx.place.isEmpty())  note += " / " + ctx.place;
  if (!ctx.event.isEmpty())  note += " / " + ctx.event;
  if (!ctx.topic.isEmpty())  note += " - \"" + ctx.topic + "\"";
  note += " #" + String(exchangeCount);
  return note;
}
```

サンプル NOTE：
```
"2026-05-28 14:23 / 渋谷 / IoT Conference 2026 - "Edge AI × E-Paper" #3"
```

### 取得失敗時のフォールバック

```cpp
if (!fetchCurrentContext(ctx)) {
  // RTC RAM に保存していた前回値を使う
  ctx = loadFromRtcRam();
}
```

---

## 6. バックエンド（Cloudflare Workers + KV）

### なぜ Cloudflare Workers + KV

- 無料枠が広い（10万 req/日、KV 1000書/日・10万読/日）
- 全世界に分散して低レイテンシ
- 1ファイルで API + 静的ページの両方をホスト可
- KV は eventually consistent だが本用途では十分

### 骨格コード

```javascript
// worker.js
const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type, Authorization",
};

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    // OPTIONS (CORS preflight)
    if (request.method === "OPTIONS") return new Response(null, { headers: corsHeaders });

    // ESP32 / 公開クライアント向け
    if (url.pathname === "/api/now") {
      const data = await env.BCARD.get("current", "json");
      return Response.json(data || {}, { headers: corsHeaders });
    }

    // 状況更新
    if (url.pathname === "/api/set" && request.method === "POST") {
      const auth = request.headers.get("Authorization");
      if (auth !== `Bearer ${env.ADMIN_TOKEN}`) {
        return new Response("Unauthorized", { status: 401, headers: corsHeaders });
      }
      const body = await request.json();
      const prev = await env.BCARD.get("current", "json") || {};
      const next = {
        ...prev,
        current: body.current,
        updated_at: new Date().toISOString()
      };
      await env.BCARD.put("current", JSON.stringify(next));
      return Response.json({ ok: true }, { headers: corsHeaders });
    }

    // 管理画面 (Basic Auth は省略、トークンクエリで初回認証)
    if (url.pathname === "/admin") {
      return new Response(adminHtml, {
        headers: { "Content-Type": "text/html; charset=utf-8" }
      });
    }

    // /now ステータスページ
    if (url.pathname === "/now") {
      const data = await env.BCARD.get("current", "json");
      if (!data?.current?.public) {
        return new Response("(現在は非公開)", {
          headers: { "Content-Type": "text/html; charset=utf-8" }
        });
      }
      return new Response(renderNowHtml(data), {
        headers: { "Content-Type": "text/html; charset=utf-8" }
      });
    }

    // トップページ
    if (url.pathname === "/") {
      const data = await env.BCARD.get("current", "json");
      return new Response(renderTopHtml(data), {
        headers: { "Content-Type": "text/html; charset=utf-8" }
      });
    }

    return new Response("Not found", { status: 404 });
  },
};
```

### 環境変数

| 名前 | 用途 |
|------|------|
| `ADMIN_TOKEN` | `/api/set` 用のシークレット |
| `BCARD` | KV namespace のバインディング名 |

---

## 7. デプロイ手順（概略）

```bash
# 1. wrangler インストール
npm install -g wrangler

# 2. プロジェクト作成
wrangler init akiyama-bcard
cd akiyama-bcard

# 3. KV namespace 作成
wrangler kv:namespace create BCARD

# 4. wrangler.toml に kv_namespaces 追記
# [[kv_namespaces]]
# binding = "BCARD"
# id = "..."

# 5. 環境変数設定
wrangler secret put ADMIN_TOKEN

# 6. デプロイ
wrangler deploy

# 7. カスタムドメイン設定（任意）
# Cloudflare ダッシュボードで akiyama.<domain> を Worker にバインド
```

---

## 8. ドメインとコスト

| 項目 | 価格 |
|------|------|
| Cloudflare Workers Free Plan | 無料 |
| Cloudflare KV Free Tier | 無料 |
| カスタムドメイン（年） | 1,000円程度（`.dev` `.com` `.me` など） |
| Cloudflare 経由のドメイン管理 | 原価 |

サブドメイン（`*.workers.dev`）を使えばドメイン代もゼロ。

---

## 9. 将来拡張案

| 機能 | 説明 |
|------|------|
| **過去ログ** | KV に履歴を残し、`/log` で確認できるように |
| **複数デバイス対応** | 名刺デバイスを追加配布した時、デバイス ID で識別 |
| **タップカウント集計** | ESP32 の FD ピンで検知したタップ回数をクラウドに記録 |
| **AI ジェネレーション** | トピックを AI が提案（その日のニュースから） |
| **Google Calendar 連携** | 予定のタイトルを自動でトピックに反映 |
| **LIFF (LINE)** | LINE で「今ここで会いました」を即送信 |

---

## 10. 関連ドキュメント

- 全体構成と NDEF 仕様: [smart_business_card_design.md](smart_business_card_design.md)
- ESP32 ハードウェア: [bare_module_design.md](bare_module_design.md)
- ケース: [case_design_spec.md](case_design_spec.md)

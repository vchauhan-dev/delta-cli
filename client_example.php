<?php
/**
 * Example PHP client — two-step flow:
 *   1. POST /api/token  →  get encrypted token
 *   2. Use token for all other calls
 */

define('API_BASE', 'http://localhost:8000');

function apiCall($method, $path, $body = null, $token = null) {
    $url = API_BASE . $path;
    $ch = curl_init($url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_CUSTOMREQUEST, $method);
    $headers = ['Content-Type: application/json'];
    if ($token) $headers[] = 'X-Auth-Token: ' . $token;
    if ($body !== null) curl_setopt($ch, CURLOPT_POSTFIELDS, json_encode($body));
    curl_setopt($ch, CURLOPT_HTTPHEADER, $headers);
    $response = curl_exec($ch);
    $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    $err = curl_error($ch);
    curl_close($ch);
    if ($err) return [$httpCode, ['error' => "cURL Error: $err"]];
    return [$httpCode, json_decode($response, true)];
}

// ── Step 1: Get token ──────────────────────────────────
$cfg = include 'config.php';
echo "═══ Step 1: Get Token ═══\n";
[$code, $data] = apiCall('POST', '/api/token', [
    'api_key'    => $cfg['api_key'],
    'api_secret' => $cfg['api_secret'],
    'base_url'   => $cfg['base_url']
]);
if (isset($data['error'])) { echo "  Error: {$data['error']}\n"; exit; }
$token = $data['token'];
echo "  Token received\n\n";

// Helper — pass token automatically
function tCall($method, $path, $body = null) {
    global $token;
    return apiCall($method, $path, $body, $token);
}
function tGet($p)   { return tCall('GET', $p); }
function tPost($p,$b) { return tCall('POST', $p, $b); }
function tPut($p,$b)  { return tCall('PUT', $p, $b); }
function tDel($p,$b)  { return tCall('DELETE', $p, $b); }

function check($d) {
    if (isset($d['error'])) { echo "  ERROR: {$d['error']}\n"; return true; }
    return false;
}

// ── Step 2: Use token for all calls ────────────────────
echo "═══ 2. Products ═══\n";
[$code, $data] = tGet('/api/products');
if (!check($data)) {
    echo "  Loaded: " . count($data['result'] ?? []) . "\n";
    $productId = null;
    foreach ($data['result'] ?? [] as $p) {
        if (str_contains($p['symbol'] ?? '', 'C-BTC') && !$productId) {
            $productId = $p['id']; $symbol = $p['symbol'];
            echo "  Using: $symbol (id=$productId)\n";
        }
    }
}
if (!isset($productId)) { echo "No BTC option.\n"; exit; }

echo "\n═══ 3. Ticker ═══\n";
[$code, $data] = tGet('/api/ticker?symbol=' . urlencode($symbol));
if (!check($data)) {
    $t = $data['result'][0] ?? [];
    echo "  Mark: {$t['mark_price']}, Bid: {$t['quotes']['best_bid']}, Ask: {$t['quotes']['best_ask']}\n";
}

echo "\n═══ 4. Wallet ═══\n";
[$code, $data] = tGet('/api/wallet');
if (!check($data)) {
    foreach ($data['result'] ?? [] as $w) echo "  {$w['asset_symbol']}: {$w['balance']} (avail {$w['available_balance']})\n";
}

echo "\n═══ 5. Positions ═══\n";
[$code, $data] = tGet('/api/positions');
if (!check($data)) {
    if (empty($data['result'])) echo "  None\n";
    else foreach ($data['result'] as $p) echo "  {$p['product_symbol']}: size={$p['size']} entry={$p['entry_price']}\n";
}

echo "\n═══ 6. Leverage ═══\n";
[$code, $data] = tGet("/api/leverage/$productId");
check($data) || printf("  %sx\n", $data['result']['leverage']);

echo "\n═══ 7. Market Order ═══\n";
[$code, $data] = tPost('/api/orders/market', ['product_id'=>$productId,'size'=>1,'side'=>'buy']);
check($data) || printf("  Order %s state=%s\n", $data['result']['id'], $data['result']['state']);

echo "\n═══ 8. Limit Order ═══\n";
[$code, $data] = tPost('/api/orders/limit', ['product_id'=>$productId,'size'=>1,'side'=>'buy','limit_price'=>5]);
$limitId = null;
if (!check($data)) { $limitId = $data['result']['id']; echo "  Order $limitId\n"; }

echo "\n═══ 9. Modify Order ═══\n";
if ($limitId) {
    [$code, $data] = tPut("/api/orders/$limitId", ['product_id'=>$productId,'limit_price'=>10]);
    check($data) || printf("  Modified to %s\n", $data['result']['limit_price']);
}

echo "\n═══ 10. Cancel Order ═══\n";
if ($limitId) {
    [$code, $data] = tDel("/api/orders/$limitId", ['product_id'=>$productId]);
    if (!check($data)) echo "  Cancelled\n";
}

echo "\n═══ 11. Open Orders ═══\n";
[$code, $data] = tGet("/api/orders?product_id=$productId");
if (!check($data)) {
    $orders = $data['result'] ?? [];
    echo "  Count: " . count($orders) . "\n";
    foreach ($orders as $o) {
        $tag = !empty($o['bracket_order']) ? ' [BRACKET]' : '';
        echo "    {$o['id']}: {$o['side']} {$o['size']} @ {$o['limit_price']} ({$o['state']})$tag\n";
    }
}

echo "\nDone.\n";

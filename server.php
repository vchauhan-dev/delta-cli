<?php
/**
 * Delta Exchange India REST API Server
 *
 * Auth: X-Auth-Email + X-Auth-Key + X-Auth-Apisec + X-Auth-Baseurl + X-Auth-License
 *
 * Run:   \php\php.exe -S localhost:8000 server.php
 */

include "delta.php";

header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type, X-Auth-Email, X-Auth-Key, X-Auth-Apisec, X-Auth-Baseurl, X-Auth-License');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(204); exit;
}

$uri    = $_SERVER['REQUEST_URI'];
$path   = rtrim(parse_url($uri, PHP_URL_PATH), '/');
$query  = $_GET;
$method = $_SERVER['REQUEST_METHOD'];

// ── Serve static files (HTML/JS/CSS) ───────────────────
$staticFile = __DIR__ . $path;
$ext = pathinfo($path, PATHINFO_EXTENSION);
$allowedExt = ['html', 'htm', 'css', 'js', 'png', 'jpg', 'svg', 'ico', 'json'];
$realBase = realpath(__DIR__);
$realFile = realpath(dirname($staticFile)) ? realpath($staticFile) : false;
if ($realFile && strpos($realFile, $realBase) === 0 && in_array($ext, $allowedExt)) {
    $mime = ['html' => 'text/html', 'htm' => 'text/html', 'css' => 'text/css', 'js' => 'application/javascript', 'png' => 'image/png', 'jpg' => 'image/jpeg', 'svg' => 'image/svg+xml', 'ico' => 'image/x-icon', 'json' => 'application/json'];
    header('Content-Type: ' . ($mime[$ext] ?? 'text/plain') . '; charset=utf-8');
    readfile($realFile);
    exit;
}

header('Content-Type: application/json; charset=utf-8');

function jsonBody() {
    return json_decode(file_get_contents('php://input'), true) ?? [];
}

function respond($data, $code = 200) {
    http_response_code($code);
    echo json_encode($data, JSON_PRETTY_PRINT);
    exit;
}

// ── Load admin key ─────────────────────────────────────
$adminKey = (include 'delta_master.php')['admin_key'] ?? '';

// ── Auth headers ───────────────────────────────────────
$authEmail     = $_SERVER['HTTP_X_AUTH_EMAIL'] ?? null;
$authApikey    = $_SERVER['HTTP_X_AUTH_KEY'] ?? null;
$authApisec    = $_SERVER['HTTP_X_AUTH_APISEC'] ?? null;
$authBaseurl   = $_SERVER['HTTP_X_AUTH_BASEURL'] ?? null;
$authToken     = $_SERVER['HTTP_X_AUTH_TOKEN'] ?? null;
$authLicense   = $_SERVER['HTTP_X_AUTH_LICENSE'] ?? null;

$usersFile = __DIR__ . '/users.json';
$users = file_exists($usersFile) ? json_decode(file_get_contents($usersFile), true) : [];

$api = null;
$authIdentity = null;
$authUserEmail = null;

// ── Auto-register (no admin_key needed) ────────────────
if ($path === '/api/register' && $method === 'POST') {
    $b = jsonBody();
    $email   = trim($b['email'] ?? '');
    $apikey  = trim($b['apikey'] ?? '');
    $apisec  = trim($b['apisec'] ?? '');
    $baseurl = trim($b['baseurl'] ?? '');
    $name    = trim($b['name'] ?? '');
    if (!$email || !$apikey || !$apisec || !$baseurl) {
        respond(['error' => 'email, apikey, apisec, baseurl required'], 400);
    }
    if (isset($users[$email])) {
        $license = $users[$email]['license'];
        if (!$license) {
            $license = base64_encode(hash('sha256', $email . $apikey . time(), true));
            $users[$email]['license'] = $license;
            $users[$email]['apikey_hash'] = md5($apikey);
            $users[$email]['name'] = $name;
            if (!isset($users[$email]['day_pass'])) $users[$email]['day_pass'] = null;
            file_put_contents($usersFile, json_encode($users, JSON_PRETTY_PRINT));
        }
        respond(['success' => true, 'email' => $email, 'license' => $license]);
    }
    $license = base64_encode(hash('sha256', $email . $apikey . time(), true));
    $users[$email] = [
        'created' => time(),
        'license' => $license,
        'blocked' => false,
        'name' => $name,
        'apikey_hash' => md5($apikey),
        'day_pass' => null
    ];
    file_put_contents($usersFile, json_encode($users, JSON_PRETTY_PRINT));
    respond(['success' => true, 'email' => $email, 'license' => $license]);
}

// ── Auth check ─────────────────────────────────────────
if ($authEmail && $authApikey && $authApisec && $authBaseurl) {
    if (!isset($users[$authEmail])) {
        respond(['error' => 'Not registered. Call POST /api/register first.'], 401);
    }
    if (!empty($users[$authEmail]['blocked'])) {
        respond(['error' => 'Your account has been blocked. Contact support.'], 403);
    }
    // Verify license if sent
    if ($authLicense && $authLicense !== ($users[$authEmail]['license'] ?? '')) {
        respond(['error' => 'License mismatch. Re-run delta config file to re-register.'], 401);
    }
    $authIdentity = $authEmail;
    $authUserEmail = $authEmail;
    $api = new DeltaExchangeIndiaAPI($authApikey, $authApisec, $authBaseurl);
} elseif ($authToken) {
    include "crypto.php";
    $creds = decryptCredentials($authToken);
    if (!$creds || !isset($creds['api_key'], $creds['api_secret'])) {
        respond(['error' => 'Invalid or expired token.'], 401);
    }
    $authIdentity = md5($authToken);
    $api = new DeltaExchangeIndiaAPI($creds['api_key'], $creds['api_secret'], $creds['base_url'] ?? '');
} else {
    respond(['error' => 'Send X-Auth-Email + X-Auth-Key + X-Auth-Apisec + X-Auth-Baseurl'], 401);
}

// ── Usage Tracking (daily) ─────────────────────────────
$usageFile = __DIR__ . '/usage.json';
$qrCfgFile = __DIR__ . '/qr_config.json';
$today = date('Y-m-d');

function getQrConfig() {
    global $qrCfgFile;
    return file_exists($qrCfgFile) ? json_decode(file_get_contents($qrCfgFile), true) : [];
}

function getUsageEntry($id) {
    global $usageFile, $today;
    if (!$id) $id = 'unknown';
    $cfg = getQrConfig();
    $limit = $cfg['free_limit'] ?? 100;
    $data = file_exists($usageFile) ? json_decode(file_get_contents($usageFile), true) : [];
    $key = md5($id . '_' . $today);
    $entry = $data[$key] ?? ['count' => 0, 'limit' => $limit, 'date' => $today];
    return [$data, $key, $entry];
}

function deductUsage($id) {
    list($data, $key, $entry) = getUsageEntry($id);
    if ($entry['count'] >= $entry['limit']) return false;
    $data[$key] = $entry;
    $data[$key]['count']++;
    file_put_contents(__DIR__ . '/usage.json', json_encode($data, JSON_PRETTY_PRINT));
    return true;
}

function buildQrResponse($id) {
    $cfg = getQrConfig();
    $limit = $cfg['free_limit'] ?? 100;
    $link = $cfg['upi_link'] ?? 'upi://pay?pa=abcdefgh@ybl&pn=Delta%20CLI&am=100.00&cu=INR&tn=Renewal%20Payment';
    $qrFile = __DIR__ . '/qr.txt';
    $qr = file_exists($qrFile) ? file_get_contents($qrFile) : '';
    if (!trim($qr)) $qr = "Scan to pay:\n$link";
    list($d, $k, $e) = getUsageEntry($id);
    $remaining = max(0, $limit - $e['count']);
    return [
        'error' => 'payment_required',
        'message' => "Daily free limit ($limit) reached. Renew to continue.",
        'qr' => $qr,
        'remaining' => $remaining
    ];
}

function checkUsageMiddleware($path, $method, $id) {
    global $authUserEmail, $users;
    // Day pass active — unlimited access for rest of day
    if ($authUserEmail && isset($users[$authUserEmail]['day_pass']) && $users[$authUserEmail]['day_pass'] > time()) {
        return true;
    }
    if ($path === '/api/usage' || $path === '/api/pay/utr' || $path === '/api/pay/reset') return true;
    if ($path === '/api/pay/status') return true;
    if ($path === '/api/license' && $method === 'GET') return true;
    if (preg_match('#^/api/admin/#', $path)) return true;
    if (preg_match('#^/api/products(?:/.*)?$#', $path)) return true;
    if ($path === '/api/ticker') return true;
    if (!deductUsage($id)) {
        respond(buildQrResponse($id), 402);
    }
    return true;
}

// ── Router ─────────────────────────────────────────────
try {
    checkUsageMiddleware($path, $method, $authIdentity);

if ($path === '/api/usage' && $method === 'GET') {
    list($d, $k, $e) = getUsageEntry($authIdentity);
    $res = ['count' => $e['count'], 'limit' => $e['limit'], 'remaining' => max(0, $e['limit'] - $e['count']), 'date' => $today];
    if ($authUserEmail) {
        $u = $users[$authUserEmail] ?? null;
        if ($u) {
            $res['licensed'] = !empty($u['license']);
            $res['day_pass_active'] = !empty($u['day_pass']) && $u['day_pass'] > time();
            if ($res['day_pass_active']) $res['day_pass_expiry'] = $u['day_pass'];
            if (!empty($u['blocked'])) $res['blocked'] = true;
        }
    }
    respond($res);
}

if ($path === '/api/pay/reset' && $method === 'POST') {
    list($d, $k, $e) = getUsageEntry($authIdentity);
    $e['count'] = 0;
    $d[$k] = $e;
    file_put_contents($usageFile, json_encode($d, JSON_PRETTY_PRINT));
    respond(['success' => true, 'message' => 'Usage reset to 0']);
}

if ($path === '/api/pay/utr' && $method === 'POST') {
    $b = jsonBody();
    $utr = $b['utr'] ?? '';
    if (!preg_match('/^[A-Za-z0-9]{12,30}$/', $utr)) {
        respond(['error' => 'Invalid UTR. Must be 12-30 alphanumeric characters.'], 400);
    }
    $regEmail = $b['email'] ?? $authEmail;
    if (!$regEmail) {
        respond(['error' => 'email required in body or headers'], 400);
    }
    list($d, $k, $e) = getUsageEntry($regEmail);
    $e['count'] = 0;
    $d[$k] = $e;
    file_put_contents($usageFile, json_encode($d, JSON_PRETTY_PRINT));
    $paymentsFile = __DIR__ . '/payments.json';
    $pmts = file_exists($paymentsFile) ? json_decode(file_get_contents($paymentsFile), true) : [];
    $pmts[] = ['key' => $k, 'utr' => $utr, 'time' => time(), 'ip' => $_SERVER['REMOTE_ADDR'] ?? '', 'email' => $regEmail];
    file_put_contents($paymentsFile, json_encode($pmts, JSON_PRETTY_PRINT));
    respond(['success' => true, 'message' => "UTR $utr recorded. Usage reset. Waiting for verification."]);
}

if ($path === '/api/products' && $method === 'GET') {
    respond($api->getProducts());
}
if (preg_match('#^/api/products/([^/]+)$#', $path, $m) && $method === 'GET') {
    respond($api->getProductBySymbol($m[1]));
}

if ($path === '/api/ticker' && $method === 'GET') {
    respond($api->getTicker($query['symbol'] ?? ''));
}

if (preg_match('#^/api/orderbook/(\d+)$#', $path, $m) && $method === 'GET') {
    respond($api->getOrderbook((int)$m[1]));
}

if ($path === '/api/orders' && $method === 'GET') {
    $pid = isset($query['product_id']) ? (int)$query['product_id'] : null;
    respond($api->getOpenOrders($pid));
}

if ($path === '/api/orders/market' && $method === 'POST') {
    $b = jsonBody();
    respond($api->placeMarketOrder((int)$b['product_id'], (int)$b['size'], $b['side']));
}

if ($path === '/api/orders/limit' && $method === 'POST') {
    $b = jsonBody();
    respond($api->placeLimitOrder((int)$b['product_id'], (int)$b['size'], $b['side'], (float)$b['limit_price']));
}

if ($path === '/api/orders/stop-limit' && $method === 'POST') {
    $b = jsonBody();
    respond($api->placeStopLimitOrder((int)$b['product_id'], (int)$b['size'], $b['side'], (float)$b['limit_price'], (float)$b['stop_price']));
}

if ($path === '/api/orders/bracket' && $method === 'POST') {
    $b = jsonBody();
    respond($api->placeBracketOrder((int)$b['product_id'], (int)$b['size'], $b['side'], (float)$b['stop_loss_price'], (float)$b['take_profit_price'], $b['order_type'] ?? 'market_order', isset($b['limit_price']) ? (float)$b['limit_price'] : null));
}

if ($path === '/api/orders/bracket/attach' && $method === 'POST') {
    $b = jsonBody();
    respond($api->attachBracket((int)$b['product_id'], (float)$b['stop_loss_price'], (float)$b['take_profit_price'], $b['stop_order_type'] ?? 'market_order', $b['take_profit_order_type'] ?? 'market_order'));
}

if ($path === '/api/orders/bracket' && $method === 'PUT') {
    $b = jsonBody();
    respond($api->editBracket((int)$b['order_id'], (int)$b['product_id'], isset($b['stop_loss_price']) ? (float)$b['stop_loss_price'] : null, isset($b['take_profit_price']) ? (float)$b['take_profit_price'] : null));
}

if (preg_match('#^/api/orders/(\d+)$#', $path, $m) && $method === 'PUT') {
    $b = jsonBody();
    respond($api->modifyOrder((int)$m[1], (int)$b['product_id'], isset($b['size']) ? (int)$b['size'] : null, isset($b['limit_price']) ? (float)$b['limit_price'] : null, isset($b['stop_price']) ? (float)$b['stop_price'] : null));
}

if (preg_match('#^/api/orders/(\d+)$#', $path, $m) && $method === 'DELETE') {
    $b = jsonBody();
    $pid = $b['product_id'] ?? $query['product_id'] ?? null;
    if (!$pid) respond(['error' => 'product_id required in body or ?product_id='], 400);
    respond($api->cancelOrder((int)$m[1], (int)$pid));
}

if ($path === '/api/candles' && $method === 'GET') {
    respond($api->getCandles($query['symbol'] ?? '', $query['resolution'] ?? '5m', $query['start'] ?? '0', $query['end'] ?? '0'));
}

if ($path === '/api/positions' && $method === 'GET') {
    respond($api->getPositions());
}

if ($path === '/api/wallet' && $method === 'GET') {
    respond($api->getWalletBalances());
}

if (preg_match('#^/api/leverage/(.+)$#', $path, $m) && $method === 'GET') {
    $id = is_numeric($m[1]) ? (int)$m[1] : $api->getProductBySymbol(strtoupper($m[1]))['result']['id'] ?? null;
    if (!$id) respond(['error' => 'Product not found'], 404);
    respond($api->getLeverage((int)$id));
}
if (preg_match('#^/api/leverage/(.+)$#', $path, $m) && $method === 'POST') {
    $b = jsonBody();
    $id = is_numeric($m[1]) ? (int)$m[1] : $api->getProductBySymbol(strtoupper($m[1]))['result']['id'] ?? null;
    if (!$id) respond(['error' => 'Product not found'], 404);
    respond($api->changeLeverage((int)$id, (int)$b['leverage']));
}

// ── License & Status ──────────────────────────────────
if ($path === '/api/license' && $method === 'GET') {
    if (!$authUserEmail) respond(['error' => 'Auth required'], 401);
    $u = $users[$authUserEmail] ?? null;
    if (!$u) respond(['error' => 'User not found'], 404);
    $dayPassActive = !empty($u['day_pass']) && $u['day_pass'] > time();
    respond([
        'email' => $authUserEmail,
        'licensed' => !empty($u['license']),
        'license' => $u['license'] ?? null,
        'blocked' => !empty($u['blocked']),
        'name' => $u['name'] ?? '',
        'day_pass_active' => $dayPassActive,
        'day_pass_expiry' => $u['day_pass'] ?? null
    ]);
}

// ── Pay status (polled by pay.html) ───────────────────
if ($path === '/api/pay/status' && $method === 'GET') {
    $email = $_GET['email'] ?? ($authUserEmail ?? '');
    if (!$email) respond(['error' => 'email required'], 400);
    $u = $users[$email] ?? null;
    if (!$u) respond(['registered' => false, 'licensed' => false, 'blocked' => false]);
    $dayPassActive = !empty($u['day_pass']) && $u['day_pass'] > time();
    respond([
        'registered' => true,
        'licensed' => !empty($u['license']),
        'license' => $u['license'] ?? null,
        'blocked' => !empty($u['blocked']),
        'day_pass_active' => $dayPassActive,
        'day_pass_expiry' => $u['day_pass'] ?? null
    ]);
}

// ── Admin endpoints ───────────────────────────────────
if ($path === '/api/admin/verify' && $method === 'POST') {
    $b = jsonBody();
    if (($b['admin_key'] ?? '') !== $adminKey) respond(['error' => 'Invalid admin key'], 403);
    $email = trim($b['email'] ?? '');
    if (!isset($users[$email])) respond(['error' => 'User not found'], 404);
    if (!empty($users[$email]['blocked'])) respond(['error' => 'User is blocked. Unblock first.'], 400);
    $dayPass = strtotime('tomorrow 00:00:00');
    $users[$email]['day_pass'] = $dayPass;
    file_put_contents($usersFile, json_encode($users, JSON_PRETTY_PRINT));
    $expiryStr = date('Y-m-d H:i', $dayPass);
    respond(['success' => true, 'email' => $email, 'day_pass' => $dayPass, 'message' => "Day pass activated until $expiryStr. No limits!"]);
}

if ($path === '/api/admin/block' && $method === 'POST') {
    $b = jsonBody();
    if (($b['admin_key'] ?? '') !== $adminKey) respond(['error' => 'Invalid admin key'], 403);
    $email = trim($b['email'] ?? '');
    if (!isset($users[$email])) respond(['error' => 'User not found'], 404);
    $users[$email]['blocked'] = true;
    file_put_contents($usersFile, json_encode($users, JSON_PRETTY_PRINT));
    respond(['success' => true, 'email' => $email, 'message' => 'User blocked']);
}

if ($path === '/api/admin/unblock' && $method === 'POST') {
    $b = jsonBody();
    if (($b['admin_key'] ?? '') !== $adminKey) respond(['error' => 'Invalid admin key'], 403);
    $email = trim($b['email'] ?? '');
    if (!isset($users[$email])) respond(['error' => 'User not found'], 404);
    $users[$email]['blocked'] = false;
    file_put_contents($usersFile, json_encode($users, JSON_PRETTY_PRINT));
    respond(['success' => true, 'email' => $email, 'message' => 'User unblocked']);
}

respond(['error' => 'Not found', 'path' => $path, 'method' => $method], 404);

} catch (Exception $e) {
    respond(['error' => $e->getMessage()], 400);
}

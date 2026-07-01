<?php
/**
 * Comprehensive Delta Exchange India API usage examples.
 *
 * Sections:
 *   1. Option Chain & Strike Selection
 *   2. Order Management (Market, Limit, Stop-Limit, Bracket, Modify, Cancel)
 *   3. Position Management (View, Close, Bracket on existing position)
 *   4. Leverage (Get & Change)
 *   5. Wallet Management (Balances, Equity, Margin)
 */

include "delta.php";
$config = include "config.php";
$api = new DeltaExchangeIndiaAPI($config['api_key'], $config['api_secret'], $config['base_url']);

// ──────────────────────────────────────────────
// SECTION 1 — Option Chain & Strike Selection
// ──────────────────────────────────────────────

echo "══════════════════════════════════════════\n";
echo "  1. OPTION CHAIN & STRIKE SELECTION\n";
echo "══════════════════════════════════════════\n\n";

// Get all products, filtered by type and expiry
$allProducts = $api->getProducts()['result'];

// Pick a future and its options for a given underlying & expiry
$underlying = 'BTC';
$expiryDate = '28JUN26'; // format varies per product

$futures = [];
$callOptions = [];
$putOptions = [];

foreach ($allProducts as $p) {
    if ($p['underlying_asset']['symbol'] !== $underlying) continue;
    if ($p['contract_type'] === 'futures') {
        $futures[] = $p;
    } elseif ($p['contract_type'] === 'call_options') {
        $callOptions[] = $p;
    } elseif ($p['contract_type'] === 'put_options') {
        $putOptions[] = $p;
    }
}

// Show available futures
echo "BTC Futures:\n";
foreach ($futures as $f) {
    echo "  {$f['symbol']}  (id={$f['id']}, tick={$f['tick_size']})\n";
}

// Show a few sample call options to understand naming
echo "\nSample call options:\n";
$sampleCalls = array_slice($callOptions, 0, 5);
$strikes = [];
foreach ($sampleCalls as $c) {
    $strikes[] = $c;
    echo "  {$c['symbol']}  strike={$c['strike_price']}\n";
}
if (!empty($callOptions)) {
    echo "  ... (" . count($callOptions) . " total)\n";
}

// If no strikes matched by name, use the first available
if (empty($strikes) && !empty($callOptions)) {
    $strikes = [reset($callOptions)];
}
if (empty($strikes) && !empty($futures)) {
    echo "No options found; will use futures instead.\n";
}

// Get spot index price to determine ATM
$btcFuture = $futures[0]['symbol'] ?? null;
if ($btcFuture) {
    $tickers = $api->getTicker($btcFuture)['result'];
    $spotPrice = null;
    foreach ($tickers as $t) {
        if (isset($t['spot_index'])) { $spotPrice = $t['spot_index']; break; }
    }
    $spotPrice = $spotPrice ?: ($tickers[0]['mark_price'] ?? null);
    echo "\nCurrent BTC spot / mark: " . ($spotPrice ?? 'N/A') . "\n";

    // Find the ATM (At-The-Money) strike — closest to spot
    if ($spotPrice && !empty($strikes)) {
        $atm = null;
        $minDiff = INF;
        foreach ($strikes as $s) {
            $diff = abs($s['strike_price'] - $spotPrice);
            if ($diff < $minDiff) { $minDiff = $diff; $atm = $s; }
        }
        if ($atm) {
            echo "ATM call: {$atm['symbol']}  strike={$atm['strike_price']}\n";
            echo "  Tick size: {$atm['tick_size']}, Contract value: {$atm['contract_value']}\n";
        }
    }
}

// ──────────────────────────────────────────────
// SECTION 2 — ORDER MANAGEMENT
// ──────────────────────────────────────────────

echo "\n\n══════════════════════════════════════════\n";
echo "  2. ORDER MANAGEMENT\n";
echo "══════════════════════════════════════════\n\n";

// Pick a product to use: prefer the first found option, fall back to a future
$productId = null;
$productSymbol = null;
if (!empty($strikes)) {
    $productId = $strikes[0]['id'];
    $productSymbol = $strikes[0]['symbol'];
    echo "\nUsing option: $productSymbol (id=$productId)\n";
} elseif (!empty($futures)) {
    $productId = $futures[0]['id'];
    $productSymbol = $futures[0]['symbol'];
    echo "\nNo options found; using future: $productSymbol (id=$productId)\n";
}

if ($productId) {
    echo "Using product_id = $productId\n";

    // --- Market Order ---
    echo "\n--- Market Order ---\n";
    try {
        $r = $api->placeMarketOrder($productId, 1, 'buy');
        echo "Market order placed: {$r['result']['id']}, state={$r['result']['state']}\n";
    } catch (Exception $e) {
        echo "Market order: " . $e->getMessage() . "\n";
    }

    // --- Limit Order ---
    echo "\n--- Limit Order ---\n";
    $ticker = $api->getTicker($strikes[0]['symbol'] ?? $futures[0]['symbol']);
    $bestBid = null;
    foreach ($ticker['result'] as $t) {
        if ($t['product_id'] === $productId) {
            $bestBid = $t['quotes']['best_bid'] ?? null;
            break;
        }
    }
    $limitPrice = $bestBid ? ($bestBid - 1) : 10;
    try {
        $r = $api->placeLimitOrder($productId, 1, 'buy', $limitPrice);
        $limitOrderId = $r['result']['id'];
        echo "Limit order placed: id=$limitOrderId, price=$limitPrice, state={$r['result']['state']}\n";
    } catch (Exception $e) {
        echo "Limit order: " . $e->getMessage() . "\n";
        $limitOrderId = null;
    }

    // --- Stop-Limit Order ---
    echo "\n--- Stop-Limit Order ---\n";
    try {
        $r = $api->placeStopLimitOrder($productId, 1, 'buy', 1000, 1005);
        echo "Stop-limit placed: id={$r['result']['id']}, limit=1000, stop=1005\n";
    } catch (Exception $e) {
        echo "Stop-limit: " . $e->getMessage() . "\n";
    }

    // --- Modify Order ---
    if (!empty($limitOrderId)) {
        echo "\n--- Modify Order ---\n";
        $newPrice = $limitPrice + 2;
        try {
            $r = $api->modifyOrder($limitOrderId, $productId, null, $newPrice);
            echo "Order $limitOrderId modified: limit_price -> $newPrice\n";
        } catch (Exception $e) {
            echo "Modify order: " . $e->getMessage() . "\n";
        }
    }

    // --- Cancel Order ---
    echo "\n--- Cancel Order ---\n";
    $openOrders = $api->getOpenOrders($productId)['result'];
    foreach ($openOrders as $o) {
        try {
            $api->cancelOrder($o['id'], $productId);
            echo "Cancelled order {$o['id']}\n";
        } catch (Exception $e) {
            echo "Cancel order {$o['id']}: " . $e->getMessage() . "\n";
        }
    }

    // --- Bracket Order (market entry with SL/TP) ---
    echo "\n--- Bracket Order (market entry + SL/TP) ---\n";
    try {
        $r = $api->placeBracketOrder($productId, 1, 'buy', 300, 600);
        echo "Bracket order placed: id={$r['result']['id']}\n";
    } catch (Exception $e) {
        echo "Bracket order: " . $e->getMessage() . "\n";
    }

    // --- Cancel all orders to leave clean state ---
    echo "\n--- Clean up orders ---\n";
    foreach ($api->getOpenOrders($productId)['result'] as $o) {
        $api->cancelOrder($o['id'], $productId);
        echo "Cleaned order {$o['id']}\n";
    }
}

// ──────────────────────────────────────────────
// SECTION 3 — POSITION MANAGEMENT
// ──────────────────────────────────────────────

echo "\n\n══════════════════════════════════════════\n";
echo "  3. POSITION MANAGEMENT\n";
echo "══════════════════════════════════════════\n\n";

// --- View All Positions ---
echo "--- Current Positions ---\n";
$positions = $api->getPositions()['result'];
if (empty($positions)) {
    echo "  No open positions.\n";
} else {
    foreach ($positions as $pos) {
        $pnl = $pos['pnl'] ?? $pos['unrealized_pnl'] ?? 'N/A';
        $liq = $pos['liquidation_price'] ?? 'N/A';
        echo "  {$pos['product_symbol']}: size={$pos['size']}, entry={$pos['entry_price']}, "
           . "liq=$liq, pnl=$pnl\n";
    }
}

// --- Close a specific position ---
$targetPos = null;
foreach ($positions as $pos) {
    if ($pos['size'] != 0) {
        $targetPos = $pos;
        break;
    }
}
if ($targetPos) {
    $closeSide = $targetPos['size'] > 0 ? 'sell' : 'buy';
    $closeSize = abs($targetPos['size']);
    echo "\n--- Close Position ---\n";
    echo "Closing {$targetPos['product_symbol']}: {$closeSide} {$closeSize}\n";
    try {
        $r = $api->placeMarketOrder($targetPos['product_id'], $closeSize, $closeSide);
        echo "Position closed: order {$r['result']['id']}\n";
    } catch (Exception $e) {
        echo "Close position: " . $e->getMessage() . "\n";
    }
}

// --- Open a fresh position for bracket demo ---
echo "\n--- Open position for bracket demo ---\n";
if ($productId) {
    try {
        $r = $api->placeMarketOrder($productId, 1, 'buy');
        echo "Opened position: order {$r['result']['id']}, state={$r['result']['state']}\n";
    } catch (Exception $e) {
        echo "Open position: " . $e->getMessage() . "\n";
        // Fallback: try a limit order at a very low price
        try {
            $r = $api->placeLimitOrder($productId, 1, 'buy', 5);
            echo "Placed limit order to build position: {$r['result']['id']}\n";
        } catch (Exception $e2) {
            echo "Fallback limit: " . $e2->getMessage() . "\n";
        }
    }
}

// --- Attach Bracket to an existing position ---
echo "\n--- Attach Bracket to Existing Position ---\n";
$positions = $api->getPositions()['result'];
$openPos = null;
foreach ($positions as $pos) {
    if ($pos['size'] != 0 && $pos['product_id'] === $productId) {
        $openPos = $pos;
        break;
    }
}
if ($openPos) {
    $entry = $openPos['entry_price'];
    $sl = $entry - 30;
    $tp = $entry + 50;
    echo "Position: {$openPos['product_symbol']}, entry=$entry\n";
    echo "Attaching bracket: SL=$sl, TP=$tp\n";
    try {
        $r = $api->attachBracket($productId, $sl, $tp);
        echo "Bracket attached!\n";
    } catch (Exception $e) {
        echo "Attach bracket: " . $e->getMessage() . "\n";
    }
}

// --- Cancel bracket by cancelling both legs ---
echo "\n--- Cancel bracket (if any) ---\n";
$openOrders = $api->getOpenOrders($productId)['result'];
foreach ($openOrders as $o) {
    if (!empty($o['bracket_order'])) {
        $api->cancelOrder($o['id'], $productId);
        echo "Cancelled bracket leg {$o['id']}\n";
    }
}

// ──────────────────────────────────────────────
// SECTION 4 — LEVERAGE MANAGEMENT
// ──────────────────────────────────────────────

echo "\n\n══════════════════════════════════════════\n";
echo "  4. LEVERAGE MANAGEMENT\n";
echo "══════════════════════════════════════════\n\n";

if ($productId) {
    echo "--- Get Current Leverage ---\n";
    try {
        $r = $api->getLeverage($productId);
        echo "  Current leverage: " . json_encode($r) . "\n";
    } catch (Exception $e) {
        echo "  Get leverage: " . $e->getMessage() . "\n";
    }

    echo "\n--- Change Leverage ---\n";
    try {
        $r = $api->changeLeverage($productId, 10);
        echo "  Leverage changed to 10x\n";
    } catch (Exception $e) {
        echo "  Change leverage: " . $e->getMessage() . "\n";
    }
}

// ──────────────────────────────────────────────
// SECTION 5 — WALLET MANAGEMENT
// ──────────────────────────────────────────────

echo "\n\n══════════════════════════════════════════\n";
echo "  5. WALLET MANAGEMENT\n";
echo "══════════════════════════════════════════\n\n";

// --- Get All Wallet Balances ---
echo "--- Wallet Balances ---\n";
$response = $api->getWalletBalances();
$balances = $response['result'];

$totalBalance = 0;
$totalAvailable = 0;
$totalPositionMargin = 0;

foreach ($balances as $b) {
    $currency = $b['asset_symbol'] ?? '?';
    $totalBal = $b['balance'] ?? 0;
    $available = $b['available_balance'] ?? 0;
    $positionMargin = $b['position_margin'] ?? 0;
    $orderMargin = $b['order_margin'] ?? 0;

    echo "  $currency:\n";
    echo "    Total balance:       $totalBal\n";
    echo "    Available balance:   $available\n";
    echo "    Position margin:     $positionMargin\n";
    echo "    Order margin:        $orderMargin\n";

    $totalBalance += $totalBal;
    $totalAvailable += $available;
    $totalPositionMargin += $positionMargin;
}

echo "\n  --- Summary ---\n";
echo "  Total balance (all assets):  $totalBalance\n";
echo "  Total available:             $totalAvailable\n";
echo "  Total position margin:       $totalPositionMargin\n";

echo "\nDone.\n";

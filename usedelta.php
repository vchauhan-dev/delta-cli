<?php

include "delta.php";
$config = include "config.php";
$api = new DeltaExchangeIndiaAPI($config['api_key'], $config['api_secret'], $config['base_url']);

$symbol = 'C-BTC-60000-280626';

// Find product
$products = $api->getProducts();
$productId = null;
foreach ($products['result'] as $p) {
    if ($p['symbol'] === $symbol) { $productId = $p['id']; break; }
}

// Find the take profit bracket leg
$orders = $api->getOpenOrders($productId)['result'];
$bracketOrderId = null;
foreach ($orders as $o) {
    if (!empty($o['bracket_order']) && $o['stop_order_type'] === 'take_profit_order') {
        $bracketOrderId = $o['id'];
        echo "Found take profit bracket leg: {$o['id']}, current TP stop_price: {$o['stop_price']}\n";
    }
    if (!empty($o['bracket_order']) && $o['stop_order_type'] === 'stop_loss_order') {
        echo "Found stop loss bracket leg: {$o['id']}, current SL stop_price: {$o['stop_price']}\n";
    }
}

if (!$bracketOrderId) { echo "No bracket found.\n"; exit; }

$newSl = 350;  // tighten stop loss
$newTp = 450;  // extend take profit

echo "\nCancelling bracket first, then recreating with new SL=$newSl, TP=$newTp\n";
// Cancel both legs
foreach ($orders as $o) {
    if (!empty($o['bracket_order'])) {
        $api->cancelOrder($o['id'], $productId);
        echo "Cancelled bracket leg {$o['id']}\n";
    }
}
// Recreate bracket with new prices
$r = $api->attachBracket($productId, $newSl, $newTp);
echo "New bracket: " . json_encode($r['result']) . "\n";

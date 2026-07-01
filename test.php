<?php
include "delta.php";
$config = include "config.php";
$api = new DeltaExchangeIndiaAPI($config['api_key'], $config['api_secret'], $config['base_url']);

echo "Products: " . count($api->getProducts()['result']) . "\n";
echo "Balances: " . count($api->getWalletBalances()['result']) . " wallets\n";
echo "Positions: " . count($api->getPositions()['result']) . " positions\n";
echo "All good!\n";

<?php
/**
 * Generate an encrypted auth token for the Delta Exchange REST API.
 *
 * Usage:
 *   php generate_token.php
 *
 * You will be prompted for:
 *   - API Key
 *   - API Secret
 *   - Base URL (press Enter for default: https://api.india.delta.exchange)
 */

include "crypto.php";

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
echo " Delta Exchange — Auth Token Generator\n";
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n";

echo "API Key: ";
$apiKey = trim(fgets(STDIN));

echo "API Secret: ";
$apiSecret = trim(fgets(STDIN));

echo "Base URL (Enter for default https://api.india.delta.exchange): ";
$baseUrl = trim(fgets(STDIN));
if ($baseUrl === '') $baseUrl = 'https://api.india.delta.exchange';

$token = encryptCredentials($apiKey, $apiSecret, $baseUrl);

echo "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
echo " YOUR AUTH TOKEN (one line):\n";
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n";
echo $token . "\n\n";
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
echo " Use this as the X-Auth-Token header in every API call.\n";
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";

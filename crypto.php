<?php
/**
 * Portable credential token for Delta Exchange REST API.
 *
 * Credentials are bundled into a single encrypted token using a well-known
 * application key. The encryption is NOT for security — use HTTPS for that.
 * It simply packs api_key + api_secret + base_url into a header-safe string
 * that any client can generate without needing a server-side secret.
 */

define('CIPHER', 'aes-256-cbc');

/**
 * Well-known application key (not a secret — same for all clients & server).
 * Changes only with a major version of this API.
 */
define('APP_KEY', 'delta-exchange-php-api-v1');

function deriveKey($passphrase) {
    return hash('sha256', $passphrase, true);
}

/**
 * Encrypt credentials into a portable token.
 */
function encryptCredentials($apiKey, $apiSecret, $baseUrl) {
    $data = json_encode([
        'api_key'    => $apiKey,
        'api_secret' => $apiSecret,
        'base_url'   => $baseUrl
    ]);
    $key = deriveKey(APP_KEY);
    $iv  = openssl_random_pseudo_bytes(16);
    $ciphertext = openssl_encrypt($data, CIPHER, $key, OPENSSL_RAW_DATA, $iv);
    return base64_encode($iv . $ciphertext);
}

/**
 * Decrypt a token back into credentials array.
 */
function decryptCredentials($token) {
    $data = base64_decode($token, true);
    if ($data === false || strlen($data) < 17) return null;
    $iv         = substr($data, 0, 16);
    $ciphertext = substr($data, 16);
    $key = deriveKey(APP_KEY);
    $decrypted = openssl_decrypt($ciphertext, CIPHER, $key, OPENSSL_RAW_DATA, $iv);
    if ($decrypted === false) return null;
    return json_decode($decrypted, true);
}

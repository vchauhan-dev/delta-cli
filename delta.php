<?php

class DeltaExchangeIndiaAPI {
    private $baseUrl = "https://api.india.delta.exchange";
    private $apiKey;
    private $apiSecret;
    private $client;

    public function __construct($apiKey, $apiSecret, $baseUrl = null) {
        $this->apiKey = $apiKey;
        $this->apiSecret = $apiSecret;
        if ($baseUrl) {
            $this->baseUrl = $baseUrl;
        }
        $this->client = curl_init();
    }

    /**
     * Generates the HMAC SHA256 signature required by Delta Exchange
     */
    private function generateSignature($method, $path, $timestamp, $queryString = '', $body = '') {
        $message = $method . $timestamp . $path;
        if (!empty($queryString)) {
            $message .= '?' . $queryString;
        }
        if (!empty($body)) {
            $message .= $body;
        }
        
        return hash_hmac('sha256', $message, $this->apiSecret);
    }

    /**
     * Executes the cURL request
     */
    private function executeRequest($method, $endpoint, $params = [], $body = []) {
        $path = $endpoint;
        $queryString = http_build_query($params);
        $bodyJson = empty($body) ? '' : json_encode($body);
        $timestamp = time();

        $signature = $this->generateSignature($method, $path, $timestamp, $queryString, $bodyJson);

        $url = $this->baseUrl . $path;
        if (!empty($queryString)) {
            $url .= '?' . $queryString;
        }

        curl_reset($this->client);
        curl_setopt($this->client, CURLOPT_URL, $url);
        curl_setopt($this->client, CURLOPT_RETURNTRANSFER, true);
        curl_setopt($this->client, CURLOPT_CUSTOMREQUEST, $method);
        
        $headers = [
            'Content-Type: application/json',
            'Accept: application/json',
            'User-Agent: php-rest-client',
            'api-key: ' . $this->apiKey,
            'timestamp: ' . $timestamp,
            'signature: ' . $signature
        ];
        
        if (!empty($bodyJson)) {
            curl_setopt($this->client, CURLOPT_POSTFIELDS, $bodyJson);
        }
        
        curl_setopt($this->client, CURLOPT_HTTPHEADER, $headers);

        $response = curl_exec($this->client);
        $err = curl_error($this->client);
        $httpCode = curl_getinfo($this->client, CURLINFO_HTTP_CODE);

        if ($err) {
            throw new Exception("cURL Error: " . $err);
        }

        $decoded = json_decode($response, true);
        
        if ($httpCode >= 400) {
            throw new Exception("API Error (HTTP $httpCode): " . $response);
        }

        return $decoded;
    }

    /**
     * Place a Bracket Order (market entry)
     */
    public function placeBracketOrder($productId, $size, $side, $stopLossPrice, $takeProfitPrice, $orderType = 'market_order', $limitPrice = null) {
        $endpoint = "/v2/orders";
        $method = "POST";
        
        $body = [
            'product_id' => $productId,
            'size' => $size,
            'side' => $side,
            'order_type' => $orderType,
            'time_in_force' => 'gtc',
            'bracket_stop_loss_price' => (string)$stopLossPrice,
            'bracket_take_profit_price' => (string)$takeProfitPrice,
            'bracket_stop_trigger_method' => 'last_traded_price'
        ];

        if ($orderType === 'limit_order' && $limitPrice !== null) {
            $body['limit_price'] = (string)$limitPrice;
        }

        return $this->executeRequest($method, $endpoint, [], $body);
    }

    /**
     * Place a standard Stop Limit Order
     */
    public function placeStopLimitOrder($productId, $size, $side, $limitPrice, $stopPrice) {
        $endpoint = "/v2/orders";
        $method = "POST";
        
        $body = [
            'product_id' => $productId,
            'size' => $size,
            'side' => $side,
            'order_type' => 'limit_order',
            'limit_price' => (string)$limitPrice,
            'stop_price' => (string)$stopPrice,
            'stop_order_type' => 'stop_loss_order'
        ];

        return $this->executeRequest($method, $endpoint, [], $body);
    }

    /**
     * Modify an existing order
     */
    public function modifyOrder($orderId, $productId, $newSize = null, $newLimitPrice = null, $newStopPrice = null) {
        $endpoint = "/v2/orders";
        $method = "PUT";
        
        $body = [
            'id' => $orderId,
            'product_id' => $productId
        ];

        if ($newSize !== null) $body['size'] = $newSize;
        if ($newLimitPrice !== null) $body['limit_price'] = (string)$newLimitPrice;
        if ($newStopPrice !== null) $body['stop_price'] = (string)$newStopPrice;

        return $this->executeRequest($method, $endpoint, [], $body);
    }

    /**
     * Cancel an order
     */
    public function cancelOrder($orderId, $productId) {
        $endpoint = "/v2/orders";
        $method = "DELETE";
        
        $body = [
            'id' => $orderId,
            'product_id' => $productId
        ];

        return $this->executeRequest($method, $endpoint, [], $body);
    }

    /**
     * Get Wallet Balances (Available & Position Margin)
     */
    public function getWalletBalances() {
        $endpoint = "/v2/wallet/balances";
        $method = "GET";
        
        return $this->executeRequest($method, $endpoint);
    }

    /**
     * Get Open Positions
     */
    public function getPositions() {
        $endpoint = "/v2/positions/margined";
        $method = "GET";
        
        return $this->executeRequest($method, $endpoint);
    }

    /**
     * Get all tradable products
     */
    public function getProducts() {
        return $this->executeRequest("GET", "/v2/products");
    }

    /**
     * Get ticker for a product symbol (empty string returns all tickers)
     */
    public function getTicker($symbol = '') {
        $params = $symbol ? ['symbol' => $symbol] : [];
        return $this->executeRequest("GET", "/v2/tickers", $params);
    }

    /**
     * Get single product by symbol
     */
    public function getProductBySymbol($symbol) {
        return $this->executeRequest("GET", "/v2/products/" . urlencode($symbol));
    }

    /**
     * Change leverage for a product
     */
    public function changeLeverage($productId, $leverage) {
        return $this->executeRequest("POST", "/v2/products/$productId/orders/leverage", [], [
            'leverage' => $leverage
        ]);
    }

    /**
     * Get current leverage for a product
     */
    public function getLeverage($productId) {
        return $this->executeRequest("GET", "/v2/products/$productId/orders/leverage");
    }

    public function getCandles($symbol, $resolution, $start, $end) {
        return $this->executeRequest("GET", "/v2/history/candles", [
            'symbol' => $symbol,
            'resolution' => $resolution,
            'start' => $start,
            'end' => $end
        ]);
    }

    /**
     * Get L2 orderbook for a product
     */
    public function getOrderbook($productId) {
        return $this->executeRequest("GET", "/v2/orderbook", ['product_id' => $productId]);
    }

    /**
     * Get open orders (pass productId or null for all)
     */
    public function getOpenOrders($productId = null) {
        $params = ['state' => 'open'];
        if ($productId !== null) $params['product_id'] = $productId;
        return $this->executeRequest("GET", "/v2/orders", $params);
    }

    /**
     * Place a simple market order (no bracket)
     */
    public function placeMarketOrder($productId, $size, $side) {
        return $this->executeRequest("POST", "/v2/orders", [], [
            'product_id' => $productId,
            'size' => $size,
            'side' => $side,
            'order_type' => 'market_order'
        ]);
    }

    /**
     * Place a limit order
     */
    public function placeLimitOrder($productId, $size, $side, $limitPrice) {
        return $this->executeRequest("POST", "/v2/orders", [], [
            'product_id' => $productId,
            'size' => $size,
            'side' => $side,
            'order_type' => 'limit_order',
            'limit_price' => (string)$limitPrice,
            'time_in_force' => 'gtc'
        ]);
    }

    /**
     * Attach bracket (stop loss + take profit) to an existing position
     */
    public function attachBracket($productId, $stopLossPrice, $takeProfitPrice, $stopOrderType = 'market_order', $takeProfitOrderType = 'market_order') {
        $body = [
            'product_id' => $productId,
            'stop_loss_order' => [
                'order_type' => $stopOrderType,
                'stop_price' => (string)$stopLossPrice
            ],
            'take_profit_order' => [
                'order_type' => $takeProfitOrderType,
                'stop_price' => (string)$takeProfitPrice
            ],
            'bracket_stop_trigger_method' => 'last_traded_price'
        ];

        return $this->executeRequest("POST", "/v2/orders/bracket", [], $body);
    }

    /**
     * Edit an existing bracket order
     */
    public function editBracket($orderId, $productId, $stopLossPrice = null, $takeProfitPrice = null) {
        $body = [
            'id' => $orderId,
            'product_id' => $productId
        ];
        if ($stopLossPrice !== null) $body['bracket_stop_loss_price'] = (string)$stopLossPrice;
        if ($takeProfitPrice !== null) $body['bracket_take_profit_price'] = (string)$takeProfitPrice;

        return $this->executeRequest("PUT", "/v2/orders/bracket", [], $body);
    }
}

?>
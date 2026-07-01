#include <windows.h>
#include <winhttp.h>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <cstdlib>
#pragma comment(lib, "winhttp.lib")

std::string toUpper(std::string s) {
    for (auto& c : s) c = toupper(c);
    return s;
}

#define CONFIG_FILE "delta.json"

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

std::string trimNum(const std::string& s, int maxDec = 4) {
    std::string r = trim(s);
    auto dot = r.find('.');
    if (dot == std::string::npos) return r;
    if (dot + maxDec + 1 < r.size()) r = r.substr(0, dot + maxDec + 1);
    auto end = r.find_last_not_of('0');
    if (end == dot) return r.substr(0, dot);
    return r.substr(0, end + 1);
}

std::string configGet(const std::string& key) {
    std::ifstream f(CONFIG_FILE);
    std::string line;
    std::string prefix = key + "=";
    while (std::getline(f, line)) {
        if (line.compare(0, prefix.size(), prefix) == 0)
            return line.substr(prefix.size());
    }
    return "";
}

void configSet(const std::string& key, const std::string& value) {
    std::map<std::string, std::string> cfg;
    std::ifstream f(CONFIG_FILE);
    std::string line;
    while (std::getline(f, line)) {
        auto pos = line.find('=');
        if (pos != std::string::npos)
            cfg[line.substr(0, pos)] = line.substr(pos + 1);
    }
    f.close();
    cfg[key] = value;
    std::ofstream o(CONFIG_FILE);
    for (auto& p : cfg)
        o << p.first << "=" << p.second << "\n";
}

std::wstring toWide(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::wstring ws(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
    if (!ws.empty()) ws.resize(len - 1);
    return ws;
}

std::string toUTF8(const std::wstring& ws) {
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, NULL, 0, NULL, NULL);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, &s[0], len, NULL, NULL);
    if (!s.empty()) s.resize(len - 1);
    return s;
}

struct UrlParts {
    std::string host;
    int port;
    bool https;
};

UrlParts parseUrl(const std::string& server) {
    UrlParts p = {"", INTERNET_DEFAULT_HTTPS_PORT, true};
    std::string s = server;
    if (s.find("://") == std::string::npos) s = "https://" + s;
    p.https = (s.substr(0, 5) == "https");
    s = s.substr(s.find("://") + 3);
    size_t colon = s.find(':');
    size_t slash = s.find('/');
    if (colon != std::string::npos && (slash == std::string::npos || colon < slash)) {
        p.host = s.substr(0, colon);
        p.port = std::stoi(s.substr(colon + 1, slash - colon - 1));
    } else {
        p.host = s.substr(0, slash);
    }
    if (p.host.empty()) p.host = "api.deltacharts.in";
    return p;
}

void showPaymentRequired(const std::string& json);
void showQRGraphics(const std::string& qrText);

std::string httpRequest(const std::string& method, const std::string& server,
                        const std::string& path, const std::string& extraHeaders,
                        const std::string& body, int* statusCode = nullptr) {
    UrlParts p = parseUrl(server);
    HINTERNET hSession = WinHttpOpen(L"DeltaCLI/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     NULL, NULL, 0);
    if (!hSession) return "Error: WinHttpOpen failed";
    HINTERNET hConnect = WinHttpConnect(hSession, toWide(p.host).c_str(), p.port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return "Error: WinHttpConnect failed"; }
    DWORD flags = p.https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, toWide(method).c_str(),
                                             toWide(path).c_str(), NULL, NULL, NULL, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return "Error: WinHttpOpenRequest failed"; }
    std::wstring whdrs = toWide(extraHeaders);
    LPCWSTR hdrs = whdrs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : whdrs.c_str();
    DWORD hdrLen = whdrs.empty() ? 0 : whdrs.size();
    LPVOID opt = body.empty() ? NULL : (LPVOID)body.c_str();
    DWORD optLen = body.size();
    BOOL sent = WinHttpSendRequest(hRequest, hdrs, hdrLen, opt, optLen, optLen, 0);
    if (!sent) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return "Error: WinHttpSendRequest failed (" + std::to_string(err) + ")";
    }
    WinHttpReceiveResponse(hRequest, NULL);
    if (statusCode) {
        DWORD slen = sizeof(DWORD);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            NULL, statusCode, &slen, NULL);
    }
    std::string result;
    char buf[4096];
    DWORD read;
    while (WinHttpReadData(hRequest, buf, sizeof(buf) - 1, &read) && read > 0) {
        buf[read] = 0;
        result += buf;
    }
    WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    if (statusCode && *statusCode == 402 && result.find("\"payment_required\"") != std::string::npos)
        showPaymentRequired(result);
    return result;
}

bool isNumber(const std::string& s) {
    if (s.empty()) return false;
    for (auto c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

std::string extractJsonString(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    pos = json.find(":", pos + key.size() + 2);
    if (pos == std::string::npos) return "";
    pos++; // skip ':'
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n')) pos++;
    if (pos >= json.size()) return "";
    if (json[pos] == '\"') { // string value
        pos++;
        std::string val;
        while (pos < json.size() && json[pos] != '\"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) { val += json[pos + 1]; pos += 2; }
            else { val += json[pos++]; }
        }
        return val;
    }
    // number or other
    size_t end = json.find_first_of(",}\n\r", pos);
    return json.substr(pos, end - pos);
}

std::string authHeaders() {
    std::string hdrs;
    std::string email = configGet("email");
    std::string apikey = configGet("apikey");
    std::string apisec = configGet("apisec");
    std::string baseurl = configGet("baseurl");
    if (!email.empty() && !apikey.empty() && !apisec.empty() && !baseurl.empty()) {
        hdrs += "X-Auth-Email: " + email + "\r\n";
        hdrs += "X-Auth-Key: " + apikey + "\r\n";
        hdrs += "X-Auth-Apisec: " + apisec + "\r\n";
        hdrs += "X-Auth-Baseurl: " + baseurl + "\r\n";
    } else {
        std::string token = configGet("token");
        if (!token.empty())
            hdrs += "X-Auth-Token: " + token + "\r\n";
    }
    std::string license = configGet("license");
    if (!license.empty())
        hdrs += "X-Auth-License: " + license + "\r\n";
    return hdrs;
}

std::string resolveProductId(const std::string& input) {
    if (isNumber(input)) return input;
    std::string cached = configGet("sym_" + input);
    if (!cached.empty()) { return cached; }
    std::string server = configGet("server");
    if (server.empty()) server = "https://api.deltacharts.in";
    std::string hdrs = authHeaders();
    int code = 0;
    std::string resp = httpRequest("GET", server, "/api/products/" + input, hdrs, "", &code);
    if (code != 200) { std::cerr << "Error: product lookup failed for '" << input << "'\n"; return ""; }
    // Find "result":{ and scan for "id" at root level of result (depth 1)
    // Find "result": and then the opening { of the result object
    auto resPos = resp.find("\"result\":");
    if (resPos == std::string::npos) { std::cerr << "Error: result not found\n"; return ""; }
    // Scan forward to find the opening { of result
    while (resPos < resp.size() && resp[resPos] != '{') resPos++;
    int depth = 0; std::string pid;
    for (size_t i = resPos; i + 4 < resp.size(); i++) {
        if (resp[i] == '{') depth++;
        else if (resp[i] == '}') { depth--; if (depth == 0) break; }
        else if (depth == 1 && resp.substr(i, 4) == "\"id\"") {
            auto colon = resp.find(':', i + 4);
            if (colon == std::string::npos) break;
            size_t ns = colon + 1;
            while (ns < resp.size() && (resp[ns] == ' ' || resp[ns] == '\n')) ns++;
            while (ns < resp.size() && resp[ns] >= '0' && resp[ns] <= '9') pid += resp[ns++];
            if (!pid.empty()) break;
        }
    }
    if (pid.empty()) { std::cerr << "Error: could not find product ID for '" << input << "'\n"; return ""; }
    configSet("sym_" + input, pid);
    return pid;
}

void showPaymentRequired(const std::string& json) {
    std::cout << "\n========================================\n";
    std::cout << "   FREE LIMIT REACHED\n";
    std::cout << "   SCAN QR TO PAY AND CONTINUE\n";
    std::cout << "========================================\n\n";
    auto qp = json.find("\"qr\":\"");
    if (qp == std::string::npos) qp = json.find("\"qr\": \"");
    if (qp != std::string::npos) {
        qp = json.find("\"", qp + 4);
        if (qp != std::string::npos) {
            qp = json.find_first_not_of(" :\"", qp);
            if (qp == std::string::npos || qp >= json.size()) qp = json.find("\"", qp > 10 ? qp - 10 : 0);
            if (qp != std::string::npos && qp < json.size()) {
                if (json[qp] == '\"') qp++;
                std::string qr;
                while (qp < json.size() && json[qp] != '\"') {
                    if (json[qp] == '\\' && qp + 1 < json.size()) {
                        char n = json[qp + 1];
                        if (n == 'n') qr += '\n';
                        else if (n == '\\') qr += '\\';
                        else if (n == '\"') qr += '\"';
                        else { qr += n; }
                        qp += 2;
                    } else { qr += json[qp++]; }
                }
                // Show graphical QR window
                showQRGraphics(qr);
            }
        }
    }
    auto mp = json.find("\"message\":\"");
    if (mp == std::string::npos) mp = json.find("\"message\": \"");
    if (mp != std::string::npos) {
        mp = json.find("\"", mp + 9);
        if (mp != std::string::npos) {
            mp++;
            while (mp < json.size() && json[mp] != '\"') std::cout << json[mp++];
            std::cout << "\n";
        }
    }
    std::cout << "========================================\n";
    // Open payment page in browser
    std::string server = configGet("server");
    if (server.empty()) server = "https://api.deltacharts.in";
    std::string url = server + "/pay.html";
    std::string cmd = "start \"\" \"" + url + "\"";
    system(cmd.c_str());
    std::cout << "Payment page opened in browser: " << url << "\n";
    std::cout << "Close the QR window to exit.\n";
    // Don't return to caller - exit to prevent raw JSON printing
    exit(0);
}

// ── Graphical QR Window ────────────────────────────────
struct QRMatrix { int w, h; std::vector<bool> cells; };

LRESULT CALLBACK QRWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY: PostQuitMessage(0); return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            QRMatrix* m = (QRMatrix*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
            if (m && m->w > 0 && m->h > 0) {
                RECT rc; GetClientRect(hwnd, &rc);
                int cell = ((rc.right - rc.left) / m->w < (rc.bottom - rc.top) / m->h)
                            ? (rc.right - rc.left) / m->w : (rc.bottom - rc.top) / m->h;
                int sx = (rc.right - cell * m->w) / 2;
                int sy = (rc.bottom - cell * m->h) / 2;
                HBRUSH white = CreateSolidBrush(RGB(255,255,255));
                HBRUSH black = CreateSolidBrush(RGB(0,0,0));
                for (int y = 0; y < m->h; y++)
                    for (int x = 0; x < m->w; x++) {
                        RECT r = { sx + x * cell, sy + y * cell, sx + (x+1) * cell, sy + (y+1) * cell };
                        FillRect(hdc, &r, m->cells[y * m->w + x] ? black : white);
                    }
                DeleteObject(white); DeleteObject(black);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void showQRGraphics(const std::string& qrText) {
    std::vector<std::string> lines;
    std::istringstream ss(qrText);
    std::string line;
    while (std::getline(ss, line)) {
        auto e = line.find_last_not_of(" \t\r\n");
        if (e != std::string::npos) line = line.substr(0, e + 1);
        auto s = line.find_first_not_of(" \t\r\n");
        if (s != std::string::npos) line = line.substr(s);
        else line.clear();
        if (!line.empty()) lines.push_back(line);
    }
    if (lines.empty()) return;
    int h = (int)lines.size();
    // Count character pairs = module count per row
    int w = 0;
    for (size_t i = 0; i + 1 < lines[0].size(); i += 2) w++;
    if (w == 0 || h == 0) return;

    QRMatrix* m = new QRMatrix;
    m->w = w; m->h = h;
    m->cells.resize(w * h, false);
    for (int y = 0; y < h; y++) {
        int xi = 0;
        for (size_t i = 0; i + 1 < lines[y].size() && xi < w; i += 2) {
            m->cells[y * w + xi] = (lines[y][i] == '#' || lines[y][i + 1] == '#');
            xi++;
        }
    }

    const char CLS[] = "DeltaQR";
    WNDCLASS wc = {};
    wc.lpfnWndProc = QRWindowProc;
    wc.hInstance = GetModuleHandle(0);
    wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
    wc.lpszClassName = CLS;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, CLS, "Delta CLI - Scan QR to Pay",
        WS_OVERLAPPED & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 480, 480,
        0, 0, wc.hInstance, 0);
    if (!hwnd) { delete m; return; }
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)m);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, 0, 0, 0)) {
        TranslateMessage(&msg); DispatchMessage(&msg);
    }
    delete m;
}

void printHelp(const std::string& topic = "") {
    std::vector<std::string> lines;
    auto add = [&](const std::string& s) { lines.push_back(s); };
    add("Delta Exchange API CLI");
    add("");
    add("Commands:");
    add("  delta config <key> [value]           Get/set config");
    add("  delta <cmd> <args>                    Place orders / query data");
    add("");
    add("Orders:");
    add("  delta buy market <symbol> <size>         Buy market");
    add("  delta buy limit <symbol> <size> <price>  Buy limit");
    add("  delta sell market <symbol> <size>        Sell market");
    add("  delta sell limit <symbol> <size> <price> Sell limit");
    add("  delta buy stoplimit <symbol> <size> <price> <stop_price>");
    add("  delta buy stopmarket <symbol> <size> <stop_price>");
    add("  delta sell stoplimit <symbol> <size> <price> <stop_price>");
    add("  delta sell stopmarket <symbol> <size> <stop_price>");
    add("  delta bracket <symbol> <size> <side> <sl> <tp>");
    add("  delta bracket-attach <symbol> <sl> <tp>");
    add("  delta bracket-modify <symbol> [sl] [tp]");
    add("  delta bracket-remove <symbol>            Remove bracket from position");
    add("  delta modify <order_id> <symbol> <price>");
    add("  delta cancel <order_id> <symbol>       Cancel specific order");
    add("  delta cancel <symbol>                 Cancel all orders for symbol");
    add("  delta cancelall                          Cancel all open orders");
    add("  delta closeall                          Close all open positions");
    add("");
    add("Auth (all 4 required for free access):");
    add("  delta config email <addr>               Your email for tracking");
    add("  delta config apikey <key>                Your Delta Exchange API key");
    add("  delta config apisec <secret>             Your Delta Exchange API secret");
    add("  delta config baseurl <url>               Delta base URL");
    add("  delta config license <key>               Set license key");
    add("  delta activate                           Check license / account status");
    add("");
    add("Data:");
    add("  delta wallet                           Show USD balance");
    add("  delta positions                       Show open positions");
    add("  delta orders [symbol]                Show open orders (all if no symbol)");
    add("  delta optionchain [symbol] [date]      Option chain (default BTC, nearest expiry)");
    add("  delta price <symbol>                  Show bid/ask/mark price");
    add("  delta candles <symbol> <tf> <from> <to> [--csv <file>]  Merged mark+volume OHLC");
    add("                                    tf: 1m|5m|15m|1h|4h|1d, to is exclusive");
    add("  delta get <path>                       GET request (e.g. /api/wallet)");
    add("  delta post <path> <json>               POST request");
    add("  delta put <path> <json>                PUT request");
    add("  delta delete <path> [json]             DELETE request");
    add("  delta usage                            Show remaining free API calls");
    add("  delta leverage <symbol> [leverage]       Show or set product leverage");
    add("  delta pay <UTR>                            Submit payment UTR and reset usage");
    add("");
    add("Examples:");
    add("  delta config email user@example.com");
    add("  delta config apikey YOUR_DELTA_API_KEY");
    add("  delta config apisec YOUR_DELTA_API_SECRET");
    add("  delta config baseurl https://api.india.delta.exchange");
    add("  delta config license YOUR_LICENSE_KEY");
    add("  delta config server https://your-server.com");
    add("  delta buy market C-BTC-59000-100726 1");
    add("  delta buy limit C-BTC-59000-100726 1 5");
    add("  delta buy stoplimit C-BTC-59000-100726 1 5600 5550");
    add("  delta buy stopmarket C-BTC-59000-100726 1 5550");
    add("  delta get /api/wallet");
    add("  delta get /api/positions");
    add("  delta get /api/ticker?symbol=C-BTC-59000-100726");
    add("  delta leverage BTCUSD                Show BTCUSD leverage");
    add("  delta leverage BTCUSD 25             Set BTCUSD leverage to 25x");
    add("");
    add("JSON API endpoints (use with delta get/post or any HTTP client):");
    add("  GET  /api/activate                           Account status (license, blocked, day_pass)");
    add("  GET  /api/ticker                             All tickers");
    add("  GET  /api/ticker?symbol=X                    Single ticker by symbol");
    add("  GET  /api/products                           All products");
    add("  GET  /api/products/X                         Single product by symbol");
    add("  GET  /api/orderbook/<id>                     Order book by product id");
    add("  GET  /api/wallet                             USD wallet balance");
    add("  GET  /api/positions                          Open positions");
    add("  GET  /api/orders                             Open orders");
    add("  GET  /api/candles?symbol=X&tf=1m&from=T&to=T OHLC candles");
    add("  GET  /api/leverage/<id_or_symbol>             Product leverage");
    add("  GET  /api/license                            License info");
    add("  GET  /api/pay/status                         Payment status");
    add("  GET  /api/usage                              Remaining daily calls");
    add("  POST /api/register                           {email,apikey,apisec,baseurl,name}");
    add("  POST /api/orders/market                      {symbol,size}");
    add("  POST /api/orders/limit                       {symbol,size,price}");
    add("  POST /api/orders/stop-limit                  {symbol,size,price,stop_price}");
    add("  POST /api/orders/bracket                     {symbol,size,side,sl,tp}");
    add("  POST /api/orders/bracket/attach              {symbol,sl,tp}");
    add("  POST /api/leverage/<id_or_symbol>             {leverage}");
    add("  POST /api/pay/reset                          Reset usage counter");
    add("  POST /api/pay/utr                            {utr,email} Submit UTR");
    add("  POST /api/admin/verify                       {admin_key,email} Day pass");
    add("  POST /api/admin/block                        {admin_key,email} Block user");
    add("  POST /api/admin/unblock                      {admin_key,email} Unblock user");
    add("  PUT  /api/orders/bracket                     {symbol,sl,tp} Modify bracket");
    add("  PUT  /api/orders/<id>                        {price} Modify order");
    add("  DELETE /api/orders/<id>                      Cancel order");

    if (topic.empty()) {
        for (auto& l : lines) std::cout << l << "\n";
        return;
    }

    // Named sections
    std::string kw = topic;
    for (auto& c : kw) c = tolower(c);
    if (kw == "orders") {
        int sec = 0;
        for (auto& l : lines) { if (l == "Orders:") sec = 1; if (sec && l.empty()) break; if (sec) std::cout << l << "\n"; }
        std::cout << "\nTip: delta help shows all sections\n"; return;
    }
    if (kw == "config" || kw == "auth") {
        int sec = 0;
        for (auto& l : lines) { if (l.find("Auth") == 0) sec = 1; if (sec && l.empty()) break; if (sec) std::cout << l << "\n"; }
        std::cout << "\nTip: delta help shows all sections\n"; return;
    }
    if (kw == "data") {
        int sec = 0;
        for (auto& l : lines) { if (l == "Data:") sec = 1; if (sec && l.empty()) break; if (sec) std::cout << l << "\n"; }
        std::cout << "\nTip: delta help shows all sections\n"; return;
    }
    if (kw == "examples") {
        int sec = 0;
        for (auto& l : lines) { if (l == "Examples:") sec = 1; if (sec && l.empty()) break; if (sec) std::cout << l << "\n"; }
        std::cout << "\nTip: delta help shows all sections\n"; return;
    }
    if (kw == "api" || kw == "json") {
        int sec = 0;
        for (auto& l : lines) { if (l.find("JSON API") != std::string::npos) sec = 1; if (sec) std::cout << l << "\n"; }
        std::cout << "\nTip: delta help shows all sections\n"; return;
    }

    // Search: show matching lines with section header
    std::string prevSection;
    bool found = false;
    for (auto& l : lines) {
        std::string lk = l;
        for (auto& c : lk) c = tolower(c);
        if (lk.find(kw) != std::string::npos && !l.empty()) {
            if (!found) std::cout << "Search results for \"" << topic << "\":\n\n";
            found = true;
            std::cout << l << "\n";
        }
    }
    if (!found)
        std::cout << "No help matches for \"" << topic << "\". Try: orders, config, auth, data, examples, api\n";
    else
        std::cout << "\nTip: delta help shows all sections\n";
}

std::vector<std::string> cmdList = {
    "buy", "sell", "config", "wallet", "positions", "orders", "optionchain",
    "price", "candles", "bracket", "bracket-attach", "bracket-modify",
    "bracket-remove", "modify", "cancel", "cancelall", "closeall",
    "activate", "usage", "pay", "get", "post", "put", "delete",
    "help", "leverage", "quit", "exit"
};

std::string readLine(const std::string& prompt) {
    std::cout << prompt; std::cout.flush();
    std::string line;
    std::getline(std::cin, line);
    return line;
}

int runCmd(int argc, char* argv[]) {
    std::string cmd = argv[1];
    for (auto& c : cmd) c = tolower(c);

    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        std::string topic = (argc > 2) ? argv[2] : "";
        for (auto& c : topic) c = tolower(c);
        printHelp(topic);
        return 0;
    }

    if (cmd == "config") {
        if (argc < 3) {
            std::cout << "Config values:\n";
            for (auto& k : {"email", "apikey", "apisec", "baseurl", "server", "license", "expiry"}) {
                std::string v = configGet(k);
                std::cout << "  " << k << " = " << (v.empty() ? "(not set)" : v) << "\n";
            }
            return 0;
        }
        std::string key = argv[2];
        if (key == "file" && argc >= 4) {
            std::string fname = argv[3];
            std::ifstream f(fname);
            if (!f) { std::cerr << "Error: cannot open " << fname << "\n"; return 1; }
            std::string line;
            std::map<std::string, std::string> vals;
            while (std::getline(f, line)) {
                auto eq = line.find('=');
                if (eq != std::string::npos) {
                    std::string k = line.substr(0, eq), v = line.substr(eq + 1);
                    if (k == "emailid") { configSet("email", v); k = "email"; }
                    if (k == "apisecret") { configSet("apisec", v); k = "apisec"; }
                    vals[k] = v;
                    configSet(k, v);
                }
            }
            std::cout << "Loaded " << vals.size() << " values from " << fname << "\n";
            // Auto-register if no license but has all 4 params
            if (vals.count("email") && vals.count("apikey") && vals.count("apisec") && vals.count("baseurl") && !vals.count("license")) {
                std::string server = vals.count("server") ? vals["server"] : configGet("server");
                if (server.empty()) server = "https://api.deltacharts.in";
                std::string json = "{\"email\":\"" + vals["email"] + "\",\"apikey\":\"" + vals["apikey"]
                                 + "\",\"apisec\":\"" + vals["apisec"] + "\",\"baseurl\":\"" + vals["baseurl"] + "\"}";
                std::string h = "Content-Type: application/json\r\n";
                int code = 0;
                std::string resp = httpRequest("POST", server, "/api/register", h, json, &code);
                if (code == 200) {
                    auto pos = resp.find("\"license\"");
                    if (pos != std::string::npos) {
                        pos = resp.find("\"", pos + 9); pos++;
                        std::string license;
                        while (pos < resp.size() && resp[pos] != '\"') {
                            if (resp[pos] == '\\' && pos + 1 < resp.size()) { license += resp[pos + 1]; pos += 2; }
                            else { license += resp[pos++]; }
                        }
                        configSet("license", license);
                        vals["license"] = license;
                        std::cout << "License saved: " << license.substr(0, 24) << "...\n";
                        // Append license= to config file
                        std::ofstream out(fname, std::ios::app);
                        out << "license=" << license << "\n";
                        std::cout << "Appended license to " << fname << "\n";
                    }
                } else {
                    std::cerr << "Auto-register failed: " << resp << "\n";
                    return 1;
                }
            } else if (vals.count("license")) {
                std::cout << "License found. Ready to connect.\n";
            }
            return 0;
        }
        if (argc == 3) {
            std::cout << configGet(key) << "\n";
        } else {
            std::string val = argv[3];
            for (int i = 4; i < argc; i++) val += " " + std::string(argv[i]);
            configSet(key, val);
            std::cout << "Set " << key << "\n";
        }
        return 0;
    }

    if (cmd == "token") {
        std::string apikey = configGet("apikey");
        std::string apisecret = configGet("apisecret");
        std::string baseurl = configGet("baseurl");
        std::string server = configGet("server");
        if (server.empty()) server = "https://api.deltacharts.in";
        if (apikey.empty() || apisecret.empty()) {
            std::cerr << "Error: apikey and apisecret not set. Run 'delta config' first.\n";
            return 1;
        }
        std::string json = "{\"api_key\":\"" + apikey + "\",\"api_secret\":\"" + apisecret + "\"";
        if (!baseurl.empty()) json += ",\"base_url\":\"" + baseurl + "\"";
        json += "}";
        std::string hdrs = "Content-Type: application/json\r\n";
        int code = 0;
        std::string resp = httpRequest("POST", server, "/api/token", hdrs, json, &code);
        if (code != 200) {
            std::cerr << "Error (" << code << "): " << resp << "\n";
            return 1;
        }
        // Extract token from JSON response {"token":"..."}
        auto pos = resp.find("\"token\"");
        if (pos == std::string::npos) {
            std::cerr << "Error: token not found in response: " << resp << "\n";
            return 1;
        }
        pos = resp.find("\"", pos + 8);
        if (pos == std::string::npos) { std::cerr << "Error: bad json\n"; return 1; }
        pos++; // skip opening quote
        std::string token;
        while (pos < resp.size() && resp[pos] != '\"') {
            if (resp[pos] == '\\' && pos + 1 < resp.size()) {
                token += resp[pos + 1];
                pos += 2;
            } else {
                token += resp[pos++];
            }
        }
        configSet("token", token);
        std::cout << "Token saved: " << token.substr(0, 20) << "..." << "\n";
        return 0;
    }

    // ─── Shared HTTP request logic ──────────────────
    std::string httpPath, httpBody, httpMethod = cmd;
    for (auto& c : httpMethod) c = toupper(c);

    // ─── Simple order commands ─────────────────────────
    if (cmd == "buy" || cmd == "sell") {
        if (argc < 4) { std::cerr << "Usage: delta " << cmd << " market|limit|stoplimit <symbol_or_pid> <size> [price] [stop_price]\n"; return 1; }
        std::string side = cmd, type = argv[2], size = (argc > 4) ? argv[4] : "1";
        for (auto& c : type) c = tolower(c);
        std::string pid = resolveProductId(argv[3]);
        if (pid.empty()) return 1;
        httpMethod = "POST";
        if (type == "market") {
            httpPath = "/api/orders/market";
            httpBody = "{\"product_id\":" + pid + ",\"size\":" + size + ",\"side\":\"" + side + "\"}";
        } else if (type == "limit" && argc >= 6) {
            httpPath = "/api/orders/limit";
            httpBody = "{\"product_id\":" + pid + ",\"size\":" + size + ",\"side\":\"" + side + "\",\"limit_price\":" + argv[5] + "}";
        } else if (type == "stoplimit" && argc >= 7) {
            httpPath = "/api/orders/stop-limit";
            httpBody = "{\"product_id\":" + pid + ",\"size\":" + size + ",\"side\":\"" + side + "\",\"limit_price\":" + argv[5] + ",\"stop_price\":" + argv[6] + "}";
        } else if (type == "stopmarket" && argc >= 6) {
            httpPath = "/api/orders/stop-limit";
            httpBody = "{\"product_id\":" + pid + ",\"size\":" + size + ",\"side\":\"" + side + "\",\"order_type\":\"market_order\",\"stop_price\":" + argv[5] + "}";
        } else { std::cerr << "Usage: delta " << cmd << " market|limit|stoplimit|stopmarket <symbol_or_pid> <size> [price] [stop_price]\n"; return 1; }
        goto doHttp;
    }
    if (cmd == "closeall") {
        // Get positions
        std::string server = configGet("server");
if (server.empty()) server = "https://api.deltacharts.in";
std::string hdrs = authHeaders();
        int code = 0;
        std::string resp = httpRequest("GET", server, "/api/positions", hdrs, "", &code);
        if (code != 200) { std::cout << resp << "\n"; return 1; }
        if (resp.find("\"result\": []") != std::string::npos || resp.find("\"result\":[]") != std::string::npos) {
            std::cout << "No open positions.\n"; return 0;
        }
        // Extract each position's symbol and size
        std::vector<std::pair<std::string, std::string>> toClose;
        size_t searchFrom = 0;
        while (true) {
            auto symP = resp.find("\"product_symbol\"", searchFrom);
            if (symP == std::string::npos) break;
            int depth = 0; auto objStart = symP;
            while (objStart > 0) { if (resp[objStart] == '}') depth++; else if (resp[objStart] == '{') { depth--; if (depth < 0) break; } objStart--; }
            depth = 0; auto objEnd = symP;
            while (objEnd < resp.size()) { if (resp[objEnd] == '{') depth++; else if (resp[objEnd] == '}') { depth--; if (depth < 0) break; } objEnd++; }
            std::string block = resp.substr(objStart, objEnd - objStart + 1);
            std::string sym = extractJsonString(block, "product_symbol");
            std::string sz = extractJsonString(block, "size");
            if (!sym.empty() && !sz.empty() && sz != "0")
                toClose.push_back({sym, sz});
            searchFrom = objEnd + 1;
        }
        if (toClose.empty()) { std::cout << "No open positions.\n"; return 0; }
        std::cout << "Closing " << toClose.size() << " position(s):\n";
        for (size_t i = 0; i < toClose.size(); i++) {
            auto& p = toClose[i];
            std::string pid = resolveProductId(p.first);
            if (pid.empty()) { std::cerr << "  " << p.first << ": product lookup failed\n"; continue; }
            std::string body = "{\"product_id\":" + pid + ",\"size\":" + p.second + ",\"side\":\"sell\"}";
            std::string h = "Content-Type: application/json\r\n" + authHeaders();
            int c = 0;
            std::string r = httpRequest("POST", server, "/api/orders/market", h, body, &c);
            if (c == 200)
                std::cout << "  [OK] " << p.first << " x" << p.second << " closed\n";
            else
                std::cout << "  [ERR] " << p.first << ": " << r.substr(0, 80) << "\n";
        }
        return 0;
    }

    if (cmd == "cancelall") {
        std::string srv = configGet("server");
        if (srv.empty()) srv = "https://api.deltacharts.in";
        std::string hdrs = authHeaders();
        int code = 0;
        std::string resp = httpRequest("GET", srv, "/api/orders", hdrs, "", &code);
        if (code != 200) { std::cout << resp << "\n"; return 1; }
        if (resp.find("\"result\": []") != std::string::npos || resp.find("\"result\":[]") != std::string::npos) {
            std::cout << "No open orders.\n"; return 0;
        }
        struct OrderInfo { std::string id; std::string pid; std::string sym; };
        std::vector<OrderInfo> toCancel;
        size_t pos = 0;
        while (true) {
            auto ob = resp.find("\"product_id\"", pos);
            if (ob == std::string::npos) break;
            int dd = 0; auto st = ob;
            while (st > 0) { if (resp[st] == '}') dd++; else if (resp[st] == '{') { dd--; if (dd < 0) break; } st--; }
            dd = 0; auto en = ob;
            while (en < resp.size()) { if (resp[en] == '{') dd++; else if (resp[en] == '}') { dd--; if (dd < 0) break; } en++; }
            std::string blk = resp.substr(st, en - st + 1);
            // Get the last "id" in the block (top-level order id, not product.id)
            std::string id, pid = extractJsonString(blk, "product_id"), sym = extractJsonString(blk, "product_symbol");
            {
                auto lastId = blk.rfind("\"id\"");
                if (lastId != std::string::npos) {
                    auto col = blk.find(":", lastId + 4);
                    if (col != std::string::npos) {
                        col++; while (col < blk.size() && (blk[col]==' '||blk[col]=='\t')) col++;
                        if (col < blk.size() && blk[col] != '\"') {
                            size_t end = blk.find_first_of(",}\n\r", col);
                            id = blk.substr(col, end - col);
                        }
                    }
                }
            }
            if (id.size() >= 8 && !pid.empty())
                toCancel.push_back({id, pid, sym});
            pos = en + 1;
        }
        if (toCancel.empty()) { std::cout << "No open orders.\n"; return 0; }
        std::cout << "Cancelling " << toCancel.size() << " order(s):\n";
        for (size_t i = 0; i < toCancel.size(); i++) {
            std::string body = "{\"product_id\":" + toCancel[i].pid + "}";
            std::string h = "Content-Type: application/json\r\n" + authHeaders();
            int c = 0;
            std::string r = httpRequest("DELETE", srv, "/api/orders/" + toCancel[i].id, h, body, &c);
            if (c == 200)
                std::cout << "  [OK] " << toCancel[i].sym << " order#" << toCancel[i].id << " cancelled\n";
            else
                std::cout << "  [ERR] " << toCancel[i].sym << " order#" << toCancel[i].id << ": " << r.substr(0, 80) << "\n";
        }
        return 0;
    }

    if (cmd == "bracket" && argc >= 6) {
        std::string pid = resolveProductId(argv[2]);
        if (pid.empty()) return 1;
        httpMethod = "POST"; httpPath = "/api/orders/bracket";
        httpBody = "{\"product_id\":" + pid + ",\"size\":" + argv[3] + ",\"side\":\"" + argv[4] + "\",\"stop_loss_price\":" + argv[5] + ",\"take_profit_price\":" + argv[6] + "}";
        goto doHttp;
    }
    if (cmd == "bracket-remove" && argc >= 3) {
        std::string pid = resolveProductId(argv[2]);
        if (pid.empty()) return 1;
        std::string srv = configGet("server");
        if (srv.empty()) srv = "https://api.deltacharts.in";
        std::string hdrs = "Content-Type: application/json\r\n" + authHeaders();
        int code = 0;
        std::string resp = httpRequest("GET", srv, "/api/orders?product_id=" + pid, hdrs, "", &code);
        if (code != 200) { std::cout << resp << "\n"; return 1; }
        std::vector<std::string> oids;
        size_t p = 0;
        while (true) {
            auto br = resp.find("\"bracket_order\": true", p);
            if (br == std::string::npos) break;
            int dd = 0; auto st = br;
            while (st > 0) { if (resp[st] == '}') dd++; else if (resp[st] == '{') { dd--; if (dd < 0) break; } st--; }
            dd = 0; auto en = br;
            while (en < resp.size()) { if (resp[en] == '{') dd++; else if (resp[en] == '}') { dd--; if (dd < 0) break; } en++; }
            std::string blk = resp.substr(st, en - st + 1);
            size_t ip = 0;
            while (true) {
                ip = blk.find("\"id\":", ip);
                if (ip == std::string::npos) break;
                ip += 5; while (ip < blk.size() && (blk[ip]==' '||blk[ip]=='\n'||blk[ip]=='\t')) ip++;
                size_t np = ip; while (np < blk.size() && blk[np] >= '0' && blk[np] <= '9') np++;
                std::string val = blk.substr(ip, np - ip);
                if (val.size() >= 8) { oids.push_back(val); break; }
                ip = np;
            }
            p = br + 20;
        }
        if (oids.empty()) { std::cerr << "No bracket found for this product\n"; return 1; }
        for (auto& oid : oids) {
            httpRequest("DELETE", srv, "/api/orders/" + oid, hdrs, "{\"product_id\":" + pid + "}", &code);
            std::cout << "  Removed bracket order " << oid << "\n";
        }
        return 0;
    }

    if (cmd == "bracket-modify" && argc >= 4) {
        std::string pid = resolveProductId(argv[2]);
        if (pid.empty()) return 1;
        std::string sl = argv[3];
        std::string tp = (argc >= 5) ? argv[4] : "";
        std::string srv = configGet("server");
        if (srv.empty()) srv = "https://api.deltacharts.in";
        std::string hdrs = "Content-Type: application/json\r\n" + authHeaders();
        // Use PUT /api/orders/bracket to modify bracket via the stop-loss order ID
        // First, find bracket SL child order ID from open orders
        int code = 0;
        std::string resp = httpRequest("GET", srv, "/api/orders?product_id=" + pid, hdrs, "", &code);
        if (code != 200) { std::cout << resp << "\n"; return 1; }
        // Find all numeric "id" values followed by bracket_order:true nearby
        std::vector<std::string> oids;
        size_t p = 0;
        while (true) {
            auto br = resp.find("\"bracket_order\": true", p);
            if (br == std::string::npos) break;
            // Simple: find all numeric sequences between: "id": and "product"
            // Extract block for this order
            int dd = 0; auto st = br;
            while (st > 0) { if (resp[st] == '}') dd++; else if (resp[st] == '{') { dd--; if (dd < 0) break; } st--; }
            dd = 0; auto en = br;
            while (en < resp.size()) { if (resp[en] == '{') dd++; else if (resp[en] == '}') { dd--; if (dd < 0) break; } en++; }
            std::string blk = resp.substr(st, en - st + 1);
            // Find ALL "id":NNN in this block, pick the one with 10+ digits (order IDs are long)
            std::string best;
            size_t ip = 0;
            while (true) {
                ip = blk.find("\"id\":", ip);
                if (ip == std::string::npos) break;
                ip += 5; while (ip < blk.size() && (blk[ip]==' '||blk[ip]=='\n'||blk[ip]=='\t')) ip++;
                size_t np = ip; while (np < blk.size() && blk[np] >= '0' && blk[np] <= '9') np++;
                std::string val = blk.substr(ip, np - ip);
                if (val.size() >= 8) { best = val; break; } // order IDs are 10 digits
                ip = np;
            }
            if (!best.empty()) oids.push_back(best);
            p = br + 20;
        }
        if (oids.empty()) { std::cerr << "No bracket found for this product\n"; return 1; }
        // Cancel bracket child orders via DELETE
        for (auto& oid : oids) {
            httpRequest("DELETE", srv, "/api/orders/" + oid, hdrs, "{\"product_id\":" + pid + "}", &code);
            std::cout << "  Cancelled bracket order " << oid << "\n";
        }
        // Attach new bracket
        httpPath = "/api/orders/bracket/attach";
        httpBody = "{\"product_id\":" + pid + ",\"stop_loss_price\":" + sl;
        if (!tp.empty()) httpBody += ",\"take_profit_price\":" + tp;
        httpBody += "}";
        httpMethod = "POST";
        std::cout << "  Attaching new bracket SL=" << sl << " TP=" << tp << "\n";
        goto doHttp;
    }
    if (cmd == "bracket-attach" && argc >= 5) {
        std::string pid = resolveProductId(argv[2]);
        if (pid.empty()) return 1;
        httpMethod = "POST"; httpPath = "/api/orders/bracket/attach";
        httpBody = "{\"product_id\":" + pid + ",\"stop_loss_price\":" + argv[3] + ",\"take_profit_price\":" + argv[4] + "}";
        goto doHttp;
    }
    if (cmd == "modify" && argc >= 5) {
        std::string pid = resolveProductId(argv[3]);
        if (pid.empty()) return 1;
        httpMethod = "PUT"; httpPath = "/api/orders/" + std::string(argv[2]);
        httpBody = "{\"product_id\":" + pid + ",\"limit_price\":" + argv[4] + "}";
        goto doHttp;
    }
    if (cmd == "cancel") {
        if (argc >= 4) {
            std::string pid = resolveProductId(argv[3]);
            if (pid.empty()) return 1;
            httpMethod = "DELETE"; httpPath = "/api/orders/" + std::string(argv[2]);
            httpBody = "{\"product_id\":" + pid + "}";
            goto doHttp;
        }
        if (argc >= 3) {
            // cancel all orders for a symbol
            std::string pid = resolveProductId(argv[2]);
            if (pid.empty()) return 1;
            std::string srv = configGet("server");
            if (srv.empty()) srv = "https://api.deltacharts.in";
            std::string hdrs = authHeaders();
            int code = 0;
            std::string resp = httpRequest("GET", srv, "/api/orders?product_id=" + pid, hdrs, "", &code);
            if (code != 200) { std::cout << resp << "\n"; return 1; }
            if (resp.find("\"result\": []") != std::string::npos || resp.find("\"result\":[]") != std::string::npos) {
                std::cout << "No open orders for " << argv[2] << ".\n"; return 0;
            }
            struct OI { std::string id; std::string sym; };
            std::vector<OI> toCancel;
            size_t pos = 0;
            while (true) {
                auto ob = resp.find("\"product_id\"", pos);
                if (ob == std::string::npos) break;
                int dd = 0; auto st = ob;
                while (st > 0) { if (resp[st] == '}') dd++; else if (resp[st] == '{') { dd--; if (dd < 0) break; } st--; }
                dd = 0; auto en = ob;
                while (en < resp.size()) { if (resp[en] == '{') dd++; else if (resp[en] == '}') { dd--; if (dd < 0) break; } en++; }
                std::string blk = resp.substr(st, en - st + 1);
                std::string id, sym = extractJsonString(blk, "product_symbol");
                auto lastId = blk.rfind("\"id\"");
                if (lastId != std::string::npos) {
                    auto col = blk.find(":", lastId + 4);
                    if (col != std::string::npos) {
                        col++; while (col < blk.size() && (blk[col]==' '||blk[col]=='\t')) col++;
                        if (col < blk.size() && blk[col] != '\"') {
                            size_t end = blk.find_first_of(",}\n\r", col);
                            id = blk.substr(col, end - col);
                        }
                    }
                }
                if (id.size() >= 8) toCancel.push_back({id, sym});
                pos = en + 1;
            }
            if (toCancel.empty()) { std::cout << "No open orders for " << argv[2] << ".\n"; return 0; }
            std::cout << "Cancelling " << toCancel.size() << " order(s) for " << argv[2] << ":\n";
            for (size_t i = 0; i < toCancel.size(); i++) {
                std::string body = "{\"product_id\":" + pid + "}";
                std::string h = "Content-Type: application/json\r\n" + authHeaders();
                int c = 0;
                std::string r = httpRequest("DELETE", srv, "/api/orders/" + toCancel[i].id, h, body, &c);
                if (c == 200) std::cout << "  [OK] order#" << toCancel[i].id << "\n";
                else std::cout << "  [ERR] order#" << toCancel[i].id << ": " << r.substr(0, 80) << "\n";
            }
            return 0;
        }
        std::cerr << "Usage: delta cancel <order_id> <symbol>  or  delta cancel <symbol>\n";
        return 1;
    }

    if (cmd == "wallet") {
        std::string server = configGet("server");
if (server.empty()) server = "https://api.deltacharts.in";
std::string hdrs = authHeaders();
        int code = 0;
        std::string resp = httpRequest("GET", server, "/api/wallet", hdrs, "", &code);
        if (code != 200) { std::cout << resp << "\n"; return 1; }
        // find USD block by locating "asset_symbol":"USD" then extract the surrounding {...}
        auto usdPos = resp.find("\"asset_symbol\"");
        while (usdPos != std::string::npos) {
            auto colon = resp.find(":", usdPos + 14);
            if (colon == std::string::npos) break;
            colon++; while (colon < resp.size() && (resp[colon]==' '||resp[colon]=='\t'||resp[colon]=='\n')) colon++;
            if (colon < resp.size() && resp[colon] == '\"') {
                colon++; auto endq = resp.find("\"", colon);
                std::string foundSym = resp.substr(colon, endq - colon);
                if (foundSym == "USD") { usdPos = colon; break; }
            }
            usdPos = resp.find("\"asset_symbol\"", usdPos + 14);
        }
        if (usdPos == std::string::npos) { std::cout << resp << "\n"; return 1; }
        // find enclosing { } for this USD block
        int depth = 1; auto blockStart = usdPos;
        while (blockStart > 0 && depth > 0) {
            blockStart--;
            if (resp[blockStart] == '}') depth++;
            else if (resp[blockStart] == '{') depth--;
        }
        auto blockEnd = usdPos;
        depth = 1;
        while (blockEnd < resp.size() && depth > 0) {
            blockEnd++;
            if (resp[blockEnd] == '{') depth++;
            else if (resp[blockEnd] == '}') depth--;
        }
        std::string block = resp.substr(blockStart, blockEnd - blockStart + 1);
        std::string bal = extractJsonString(block, "balance");
        std::string avail = extractJsonString(block, "available_balance");
        std::cout << "USD Wallet: balance=" << trimNum(bal) << ", available=" << trimNum(avail) << "\n";
        return 0;
    }

    if (cmd == "positions") {
        std::string server = configGet("server");
if (server.empty()) server = "https://api.deltacharts.in";
std::string hdrs = authHeaders();
        int code = 0;
        std::string resp = httpRequest("GET", server, "/api/positions", hdrs, "", &code);
        if (code != 200) { std::cout << resp << "\n"; return 1; }
        if (resp.find("\"result\": []") != std::string::npos || resp.find("\"result\":[]") != std::string::npos) {
            std::cout << "No open positions.\n"; return 0;
        }
        // First pass: collect product IDs and position info
        std::vector<std::string> pids, syms, sizes, entries, pnls;
        size_t searchFrom = 0;
        while (true) {
            auto symP = resp.find("\"product_symbol\"", searchFrom);
            if (symP == std::string::npos) break;
            int depth = 0;
            auto objEnd = symP;
            bool foundEnd = false;
            while (objEnd < resp.size()) {
                if (resp[objEnd] == '{') depth++;
                else if (resp[objEnd] == '}') { depth--; if (depth < 0) { foundEnd = true; break; } }
                objEnd++;
            }
            if (!foundEnd) break;
            depth = 0;
            auto objStart = symP;
            while (objStart > 0) {
                if (resp[objStart] == '}') depth++;
                else if (resp[objStart] == '{') { depth--; if (depth < 0) { break; } }
                objStart--;
            }
            std::string block = resp.substr(objStart, objEnd - objStart + 1);
            pids.push_back(extractJsonString(block, "product_id"));
            syms.push_back(extractJsonString(block, "product_symbol"));
            sizes.push_back(extractJsonString(block, "size"));
            entries.push_back(extractJsonString(block, "entry_price"));
            pnls.push_back(extractJsonString(block, "unrealized_pnl"));
            searchFrom = objEnd + 1;
        }
        // Fetch bracket info for each product
        std::map<std::string, std::pair<std::string, std::string>> bracketInfo; // pid -> (sl, tp)
        for (auto& pid : pids) {
            std::string ordersResp = httpRequest("GET", server, "/api/orders?product_id=" + pid, hdrs, "", &code);
            if (code != 200) continue;
            size_t pos = 0;
            while (true) {
                auto br = ordersResp.find("\"bracket_order\": true", pos);
                if (br == std::string::npos) break;
                int bd = 0;
                auto st = br;
                while (st > 0) { if (ordersResp[st] == '}') bd++; else if (ordersResp[st] == '{') { bd--; if (bd < 0) break; } st--; }
                bd = 0;
                auto en = br;
                while (en < ordersResp.size()) { if (ordersResp[en] == '{') bd++; else if (ordersResp[en] == '}') { bd--; if (bd < 0) break; } en++; }
                std::string oblock = ordersResp.substr(st, en - st + 1);
                std::string ot = extractJsonString(oblock, "stop_order_type");
                std::string pr = extractJsonString(oblock, "stop_price");
                if (ot == "stop_loss_order") bracketInfo[pid].first = pr;
                else if (ot == "take_profit_order") bracketInfo[pid].second = pr;
                pos = br + 20;
            }
        }
        // Display
        for (size_t i = 0; i < syms.size(); i++) {
            std::cout << syms[i] << "  size=" << trimNum(sizes[i]) << "  entry=" << trimNum(entries[i]) << "  PnL=" << trimNum(pnls[i]);
            auto bi = bracketInfo.find(pids[i]);
            if (bi != bracketInfo.end()) {
                if (!bi->second.first.empty()) std::cout << "  SL=" << trimNum(bi->second.first);
                if (!bi->second.second.empty()) std::cout << "  TP=" << trimNum(bi->second.second);
            }
            std::cout << "\n";
        }
        if (syms.empty()) std::cout << "No open positions.\n";
        return 0;
    }

    if (cmd == "orders") {
        std::string srv = configGet("server");
        if (srv.empty()) srv = "https://api.deltacharts.in";
        std::string hdrs = authHeaders();
        int code = 0;
        std::string path;
        if (argc >= 3) {
            std::string pid = resolveProductId(argv[2]);
            if (pid.empty()) return 1;
            path = "/api/orders?product_id=" + pid;
        } else {
            path = "/api/orders";
        }
        std::string resp = httpRequest("GET", srv, path, hdrs, "", &code);
        if (code != 200) { std::cout << resp << "\n"; return 1; }
        if (resp.find("\"result\": []") != std::string::npos || resp.find("\"result\":[]") != std::string::npos) {
            std::cout << "No open orders.\n"; return 0;
        }
        size_t p = 0;
        int idx = 0;
        while (true) {
            auto ob = resp.find("\"product_id\"", p);
            if (ob == std::string::npos) break;
            int dd = 0; auto st = ob;
            while (st > 0) { if (resp[st] == '}') dd++; else if (resp[st] == '{') { dd--; if (dd < 0) break; } st--; }
            dd = 0; auto en = ob;
            while (en < resp.size()) { if (resp[en] == '{') dd++; else if (resp[en] == '}') { dd--; if (dd < 0) break; } en++; }
            std::string blk = resp.substr(st, en - st + 1);
            std::string id = extractJsonString(blk, "id");
            std::string sym = extractJsonString(blk, "product_symbol");
            std::string side = extractJsonString(blk, "side");
            std::string size = extractJsonString(blk, "size");
            std::string price = extractJsonString(blk, "limit_price");
            std::string otype = extractJsonString(blk, "order_type");
            std::string status = extractJsonString(blk, "state");
            std::string stopType = extractJsonString(blk, "stop_order_type");
            std::string stopPrice = extractJsonString(blk, "stop_price");
            std::string bracket = extractJsonString(blk, "bracket_order");
            if (bracket == "null") bracket = "";
            if (price.empty() || price == "null") price = stopPrice;
            if (idx == 0)
                printf("%-3s %-20s %-6s %-5s %-8s %-9s %-10s %s\n", "#", "Symbol", "Side", "Size", "Price", "Type", "Status", "Bracket");
            printf("%-3d %-20s %-6s %-5s %-8s %-9s %-10s", ++idx, sym.c_str(), side.c_str(),
                   trimNum(size).c_str(), trimNum(price).c_str(), otype.c_str(), status.c_str());
            if (bracket == "true") std::cout << " " << (stopType == "stop_loss_order" ? "SL" : stopType == "take_profit_order" ? "TP" : "BR");
            else std::cout << " -";
            std::cout << "\n";
            p = en + 1;
        }
        if (idx == 0) std::cout << "No open orders.\n";
        return 0;
    }

    if (cmd == "price" && argc >= 3) {
        std::string server = configGet("server");
if (server.empty()) server = "https://api.deltacharts.in";
std::string hdrs = authHeaders();
        int code = 0;
        std::string resp = httpRequest("GET", server, "/api/ticker", hdrs, "", &code);
        if (code != 200) { std::cout << resp << "\n"; return 1; }
        // Find the right product block by matching symbol (case-insensitive)
        std::string sym = toUpper(argv[2]);
        auto symP = resp.find("\"" + sym + "\"");
        if (symP == std::string::npos) { std::cout << "Symbol not found\n"; return 1; }
        // Enclosing { } for this product
        int depth = 0; auto objStart = symP;
        while (objStart > 0) {
            if (resp[objStart] == '}') depth++;
            else if (resp[objStart] == '{') { depth--; if (depth < 0) break; }
            objStart--;
        }
        depth = 0; auto objEnd = symP;
        while (objEnd < resp.size()) {
            if (resp[objEnd] == '{') depth++;
            else if (resp[objEnd] == '}') { depth--; if (depth < 0) break; }
            objEnd++;
        }
        std::string block = resp.substr(objStart, objEnd - objStart + 1);
        std::string bp = extractJsonString(block, "best_bid");
        std::string ap = extractJsonString(block, "best_ask");
        std::string mp = extractJsonString(block, "mark_price");
        if (bp == "null" || bp.empty()) bp = "-";
        if (ap == "null" || ap.empty()) ap = "-";
        std::cout << argv[2] << "  bid=" << trimNum(bp) << "  ask=" << trimNum(ap) << "  mark=" << trimNum(mp) << "\n";
        return 0;
    }

    if (cmd == "optionchain") {
        std::string underlying = "BTC";
        std::string targetDate; // DD-MM-YYYY
        int argIdx = 2;
        if (argc >= 3) {
            std::string a = argv[2];
            if (a.find('-') != std::string::npos) {
                targetDate = a;
                if (argc >= 4) underlying = argv[3];
            } else {
                underlying = a;
                if (argc >= 4) targetDate = argv[3];
            }
        }
        underlying = toUpper(underlying);

        std::string server = configGet("server");
if (server.empty()) server = "https://api.deltacharts.in";
std::string hdrs = authHeaders();
        int code = 0;
        std::string resp = httpRequest("GET", server, "/api/ticker", hdrs, "", &code);
        if (code != 200) { std::cout << resp << "\n"; return 1; }

        struct Opt { std::string type, symbol, strike, expiry, bid, ask, mark, vol, oi, delta, theta, vega; };
        std::vector<Opt> opts;
        double spotPrice = 0;
        size_t pos = 0;
        while (true) {
            std::string sk = "\"symbol\":";
            auto sp = resp.find(sk, pos);
            if (sp == std::string::npos) break;
            sp += sk.size();
            while (sp < resp.size() && (resp[sp] == ' ' || resp[sp] == '\t')) sp++;
            if (sp >= resp.size() || resp[sp] != '\"') { pos = sp + 1; continue; }
            sp++;
            auto se = resp.find("\"", sp);
            if (se == std::string::npos) break;
            std::string sym = resp.substr(sp, se - sp);
            pos = se + 1;

            if (sym.size() < 6 || sym[1] != '-') continue;
            // Check underlying prefix: symbol[0] is C/P, symbol[1] is '-', then underlying
            std::string prefix = std::string(1, sym[0]) + "-" + underlying + "-";
            if (sym.compare(0, prefix.size(), prefix) != 0) continue;

            int dd = 0; auto st = sp;
            while (st > 0) { if (resp[st] == '}') dd++; else if (resp[st] == '{') { dd--; if (dd < 0) break; } st--; }
            dd = 0; auto en = sp;
            while (en < resp.size()) { if (resp[en] == '{') dd++; else if (resp[en] == '}') { dd--; if (dd < 0) break; } en++; }
            std::string blk = resp.substr(st, en - st + 1);

            Opt o;
            o.type = (sym[0] == 'C') ? "call" : "put";
            o.symbol = sym;
            // Parse: C-UNDERLYING-STRIKE-DDMMYY
            size_t d1 = sym.find('-');
            size_t d2 = sym.find('-', d1 + 1);
            size_t d3 = sym.find('-', d2 + 1);
            if (d3 != std::string::npos) {
                o.strike = sym.substr(d2 + 1, d3 - d2 - 1);
                o.expiry = sym.substr(d3 + 1);
            }
            o.bid = extractJsonString(blk, "best_bid");
            o.ask = extractJsonString(blk, "best_ask");
            o.mark = extractJsonString(blk, "mark_price");
            o.vol = extractJsonString(blk, "oi_contracts");
            o.oi = extractJsonString(blk, "oi");
            if (o.bid == "null") o.bid = "";
            if (o.ask == "null") o.ask = "";
            if (o.mark == "null") o.mark = "";
            if (o.vol == "null") o.vol = "";
            if (o.oi == "null") o.oi = "";
            o.delta = extractJsonString(blk, "delta");
            o.theta = extractJsonString(blk, "theta");
            o.vega = extractJsonString(blk, "vega");
            if (o.delta == "null") o.delta = "";
            if (o.theta == "null") o.theta = "";
            if (o.vega == "null") o.vega = "";
            if (spotPrice == 0) {
                std::string sp = extractJsonString(blk, "spot_price");
                if (!sp.empty() && sp != "null") spotPrice = atof(sp.c_str());
            }
            opts.push_back(o);
        }

        if (opts.empty()) {
            std::cout << "No option products found for " << underlying << ".\n";
            return 0;
        }

        std::map<std::string, std::vector<Opt*>> byExp;
        for (auto& o : opts) byExp[o.expiry].push_back(&o);

        // Resolve target expiry
        std::string selExp;
        if (!targetDate.empty()) {
            int d, m, y; char c;
            std::istringstream iss(targetDate); iss >> d >> c >> m >> c >> y;
            char buf[7]; sprintf(buf, "%02d%02d%02d", d, m, y % 100);
            selExp = buf;
            if (byExp.find(selExp) == byExp.end()) {
                std::cout << "Expiry " << targetDate << " not found for " << underlying << ".\n";
                return 0;
            }
        } else {
            std::string cfgExp = configGet("expiry");
            if (!cfgExp.empty()) {
                int d, m, y; char c;
                std::istringstream iss(cfgExp); iss >> d >> c >> m >> c >> y;
                char buf[7]; sprintf(buf, "%02d%02d%02d", d, m, y % 100);
                selExp = buf;
            }
            if (selExp.empty() || byExp.find(selExp) == byExp.end()) {
                int bestCnt = 0;
                for (auto& e : byExp) {
                    int cnt = 0;
                    for (auto* o : e.second)
                        if (!o->bid.empty() || !o->ask.empty()) cnt++;
                    if (cnt > bestCnt) { bestCnt = cnt; selExp = e.first; }
                }
            }
        }

        auto& chain = byExp[selExp];
        std::map<double, Opt*> calls, puts;
        for (auto* o : chain) {
            double stk = atof(o->strike.c_str());
            if (o->type == "call") calls[stk] = o;
            else puts[stk] = o;
        }

        std::set<double> allStk;
        for (auto& c : calls) allStk.insert(c.first);
        for (auto& p : puts) allStk.insert(p.first);

        char expDisp[11];
        if (selExp.size() == 6)
            sprintf(expDisp, "%c%c-%c%c-20%c%c", selExp[0], selExp[1], selExp[2], selExp[3], selExp[4], selExp[5]);
        else
            sprintf(expDisp, "%s", selExp.c_str());

        char spotStr[16];
        sprintf(spotStr, "%.0f", spotPrice);
        std::cout << "\nOption Chain: " << underlying << "  Expiry: " << expDisp << "  Spot: " << spotStr << "\n";
        printf("%7s %7s %7s %7s %7s %7s %7s %7s %7s %7s %7s %7s %7s\n",
               "C DELTA", "C THETA", "C VEGA", "C OI", "C Bid", "C Ask", "Strike",
               "P Bid", "P Ask", "P OI", "P VEGA", "P THETA", "P DELTA");
        printf("%7s %7s %7s %7s %7s %7s %7s %7s %7s %7s %7s %7s %7s\n",
               "-------", "-------", "-------", "-------", "-------", "-------", "-------",
               "-------", "-------", "-------", "-------", "-------", "-------");

        // ATM threshold: 0.5% of spot
        double atmPct = 0.005;
        HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
        WORD defAttr = 7; // default gray on black
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(hCon, &csbi)) defAttr = csbi.wAttributes;

        for (double s : allStk) {
            auto ci = calls.find(s);
            auto pi = puts.find(s);
            char stkS[16];
            sprintf(stkS, s == (long long)s ? "%.0f" : "%.2f", s);

            WORD callColor = defAttr, putColor = defAttr;
            bool isAtm = false;
            if (spotPrice > 0) {
                double diff = (s - spotPrice) / spotPrice;
                double adiff = diff < 0 ? -diff : diff;
                isAtm = adiff <= atmPct;
                bool isBelow = diff < 0;
                // Call: ITM=strike<spot(Green), ATM(Yellow), OTM=strike>spot(Red)
                if (isAtm) callColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
                else if (isBelow) callColor = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
                else callColor = FOREGROUND_RED | FOREGROUND_INTENSITY;
                // Put: ITM=strike>spot(Green), ATM(Yellow), OTM=strike<spot(Red)
                if (isAtm) putColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
                else if (isBelow) putColor = FOREGROUND_RED | FOREGROUND_INTENSITY;
                else putColor = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
            }

            std::string cb = (ci != calls.end() && !ci->second->bid.empty()) ? trimNum(ci->second->bid, 2) : "-";
            std::string ca = (ci != calls.end() && !ci->second->ask.empty()) ? trimNum(ci->second->ask, 2) : "-";
            std::string cv = (ci != calls.end() && !ci->second->vol.empty()) ? ci->second->vol : "-";
            std::string cd = (ci != calls.end() && !ci->second->delta.empty()) ? trimNum(ci->second->delta, 2) : "-";
            std::string ct = (ci != calls.end() && !ci->second->theta.empty()) ? trimNum(ci->second->theta, 2) : "-";
            std::string cve = (ci != calls.end() && !ci->second->vega.empty()) ? trimNum(ci->second->vega, 2) : "-";
            std::string pb = (pi != puts.end() && !pi->second->bid.empty()) ? trimNum(pi->second->bid, 2) : "-";
            std::string pa = (pi != puts.end() && !pi->second->ask.empty()) ? trimNum(pi->second->ask, 2) : "-";
            std::string pv = (pi != puts.end() && !pi->second->vol.empty()) ? pi->second->vol : "-";
            std::string pd = (pi != puts.end() && !pi->second->delta.empty()) ? trimNum(pi->second->delta, 2) : "-";
            std::string pt = (pi != puts.end() && !pi->second->theta.empty()) ? trimNum(pi->second->theta, 2) : "-";
            std::string pve = (pi != puts.end() && !pi->second->vega.empty()) ? trimNum(pi->second->vega, 2) : "-";

            SetConsoleTextAttribute(hCon, callColor);
            printf("%7s %7s %7s %7s %7s %7s", cd.c_str(), ct.c_str(), cve.c_str(), cv.c_str(), cb.c_str(), ca.c_str());
            SetConsoleTextAttribute(hCon, isAtm ? (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY) : defAttr);
            printf(" %7s ", stkS);
            SetConsoleTextAttribute(hCon, putColor);
            printf("%7s %7s %7s %7s %7s %7s\n", pb.c_str(), pa.c_str(), pv.c_str(), pve.c_str(), pt.c_str(), pd.c_str());
        }
        SetConsoleTextAttribute(hCon, defAttr);
        return 0;
    }

    if (cmd == "candles" && argc >= 6) {
        // Candles is a public API - call Delta Exchange directly, bypass proxy
        std::string apiUrl = configGet("baseurl");
        if (apiUrl.empty()) apiUrl = "https://api.india.delta.exchange";
        auto toSec = [](const std::string& d) -> long long {
            int y, m, day; char c;
            std::istringstream iss(d); iss >> y >> c >> m >> c >> day;
            struct tm tm = {0}; tm.tm_year = y - 1900; tm.tm_mon = m - 1; tm.tm_mday = day;
            return (long long)mktime(&tm);
        };
        long long fromSec = toSec(argv[4]), toSecVal = toSec(argv[5]);
        long long chunkSec = 86400;

        struct Candle { std::string o, h, l, c, v; };
        auto parseToMap = [](const std::string& json, std::map<long long, Candle>& m, bool hasVol) {
            size_t p = 0;
            while (true) {
                auto tp = json.find("\"time\":", p);
                if (tp == std::string::npos) break;
                int d = 0; auto st = tp;
                while (st > 0) { if (json[st] == '}') d++; else if (json[st] == '{') { d--; if (d < 0) break; } st--; }
                d = 0; auto en = tp;
                while (en < json.size()) { if (json[en] == '{') d++; else if (json[en] == '}') { d--; if (d < 0) break; } en++; }
                std::string blk = json.substr(st, en - st + 1);
                long long ts = atoll(extractJsonString(blk, "time").c_str());
                Candle c;
                c.o = extractJsonString(blk, "open"); c.h = extractJsonString(blk, "high");
                c.l = extractJsonString(blk, "low");  c.c = extractJsonString(blk, "close");
                if (hasVol) { c.v = extractJsonString(blk, "volume"); if (c.v == "null") c.v = "0"; }
                if (m.find(ts) == m.end()) m[ts] = c;
                p = en + 1;
            }
        };

        std::string sym = argv[2];
        std::map<long long, Candle> rawMap, markMap;

        for (long long s = fromSec; s < toSecVal; s += chunkSec) {
            long long e = s + chunkSec;
            if (e > toSecVal) e = toSecVal;
            if (s >= (long long)time(nullptr)) continue;
            if (e > (long long)time(nullptr)) e = (long long)time(nullptr);
            auto fetchOne = [&](const std::string& sm) -> std::string {
                std::string q = "/v2/history/candles?symbol=" + sm + "&resolution=" + argv[3]
                              + "&start=" + std::to_string(s) + "&end=" + std::to_string(e);
                int code = 0;
                return httpRequest("GET", apiUrl, q, "", "", &code);
            };
            std::string r = fetchOne(sym);
            if (r.find("\"result\":[]") == std::string::npos && r.find("\"result\": null") == std::string::npos)
                parseToMap(r, rawMap, true);
            r = fetchOne("MARK:" + sym);
            if (r.find("\"result\":[]") == std::string::npos && r.find("\"result\": null") == std::string::npos)
                parseToMap(r, markMap, false);
        }

        bool csv = (argc >= 8 && std::string(argv[6]) == "--csv");
        std::string csvFile = csv ? argv[7] : "";
        std::string csvData;
        auto out = [&](const std::string& line) {
            std::cout << line << "\n";
            if (csv) csvData += line + "\n";
        };
        if (csv) out("Time,M-Open,M-High,M-Low,M-Close,Volume");
        else std::cout << "Time                M-Open     M-High     M-Low      M-Close    Volume\n";
        for (auto& kv : rawMap) {
            auto mi = markMap.find(kv.first);
            if (mi == markMap.end()) continue;
            char buf[64];
            time_t tss = (time_t)kv.first;
            struct tm* tmb = localtime(&tss);
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tmb);
            if (csv) {
                char line[256];
                snprintf(line, sizeof(line), "%s,%s,%s,%s,%s,%s", buf,
                         trimNum(mi->second.o).c_str(), trimNum(mi->second.h).c_str(),
                         trimNum(mi->second.l).c_str(), trimNum(mi->second.c).c_str(),
                         kv.second.v.c_str());
                out(line);
            } else {
                printf("%s  %9s  %9s  %9s  %9s  %s\n", buf,
                       trimNum(mi->second.o).c_str(), trimNum(mi->second.h).c_str(),
                       trimNum(mi->second.l).c_str(), trimNum(mi->second.c).c_str(),
                       kv.second.v.c_str());
            }
        }
        if (csv) {
            std::ofstream f(csvFile);
            if (f) { f << csvData; std::cout << "Saved to " << csvFile << "\n"; }
            else std::cerr << "Error writing " << csvFile << "\n";
        }
        return 0;
    }

    if (cmd == "usage") {
        std::string server = configGet("server");
        if (server.empty()) server = "https://api.deltacharts.in";
        std::string hdrs = authHeaders();
        int code = 0;
        std::string resp = httpRequest("GET", server, "/api/usage", hdrs, "", &code);
        std::cout << resp << "\n";
        return 0;
    }

    if (cmd == "leverage") {
        if (argc < 3) { std::cerr << "Usage: delta leverage <symbol> [leverage]\n"; return 1; }
        std::string sym = toUpper(argv[2]);
        std::string pid = resolveProductId(sym);
        if (pid.empty()) { std::cerr << "Error: could not resolve product ID for '" << sym << "'\n"; return 1; }
        std::string server = configGet("server");
        if (server.empty()) server = "https://api.deltacharts.in";
        std::string hdrs = authHeaders();
        int code = 0;
        if (argc > 3) {
            hdrs = "Content-Type: application/json\r\n" + hdrs;
            std::string lev = argv[3];
            std::string body = "{\"leverage\":" + lev + "}";
            std::string resp = httpRequest("POST", server, "/api/leverage/" + pid, hdrs, body, &code);
            if (code >= 400) { std::cout << resp << "\n"; return 1; }
            std::cout << "leverage set to " << lev << "x successfully\n";
        } else {
            std::string resp = httpRequest("GET", server, "/api/leverage/" + pid, hdrs, "", &code);
            if (code >= 400) { std::cout << resp << "\n"; return 1; }
            std::string lev = extractJsonString(resp, "leverage");
            if (lev.empty()) { std::cout << resp << "\n"; return 1; }
            std::cout << trimNum(lev, 0) << "x\n";
        }
        return 0;
    }

    if (cmd == "activate") {
        std::string server = configGet("server");
        if (server.empty()) server = "https://api.deltacharts.in";
        std::string hdrs = authHeaders();
        int code = 0;
        std::string resp = httpRequest("GET", server, "/api/license", hdrs, "", &code);
        std::cout << resp << "\n";
        return 0;
    }

    if (cmd == "pay") {
        if (argc < 3) { std::cerr << "Usage: delta pay <UTR_NO>\n"; return 1; }
        std::string server = configGet("server");
        if (server.empty()) server = "https://api.deltacharts.in";
        std::string hdrs = "Content-Type: application/json\r\n" + authHeaders();
        std::string utr = argv[2];
        std::string json = "{\"utr\":\"" + utr + "\"}";
        int code = 0;
        std::string resp = httpRequest("POST", server, "/api/pay/utr", hdrs, json, &code);
        std::cout << resp << "\n";
        return 0;
    }

    if (cmd == "get" || cmd == "post" || cmd == "put" || cmd == "delete") {
        if (argc < 3) { std::cerr << "Usage: delta " << cmd << " <path> [json_body]\n"; return 1; }
        httpPath = argv[2];
        httpBody = (argc > 3) ? argv[3] : "";
        // fall through to doHttp
    } else {
        std::cerr << "Unknown command: " << cmd << "\n";
        printHelp();
        return 1;
    }

    doHttp: {
        std::string server = configGet("server");
        if (server.empty()) server = "https://api.deltacharts.in";

        std::string hdrs;
        if (httpMethod == "POST" || httpMethod == "PUT" || httpMethod == "DELETE")
            hdrs = "Content-Type: application/json\r\n";
        hdrs += authHeaders();

        int code = 0;
        std::string resp = httpRequest(httpMethod, server, httpPath, hdrs, httpBody, &code);
        std::cout << resp << "\n";
        if (code >= 400)
            return 1;
        return 0;
    }

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc >= 2) return runCmd(argc, argv);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD defAttr = 7;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hOut, &csbi)) defAttr = csbi.wAttributes;
    std::cout << "DELTA> Interactive mode. quit/exit to stop.\n";
    while (true) {
        std::string line = readLine("DELTA> ");
        if (line.empty() && std::cin.eof()) break;
        if (line.empty()) continue;
        if (line == "quit" || line == "exit" || line == "q") break;
        std::vector<std::string> tokens;
        std::istringstream iss(line);
        std::string tok;
        while (iss >> tok) tokens.push_back(tok);
        if (tokens.empty()) continue;
        std::vector<char*> fakeArgv;
        fakeArgv.push_back(argv[0]);
        for (auto& t : tokens) fakeArgv.push_back(&t[0]);
        SetConsoleTextAttribute(hOut, defAttr);
        runCmd((int)fakeArgv.size(), fakeArgv.data());
    }
    SetConsoleTextAttribute(hOut, defAttr);
    return 0;
}


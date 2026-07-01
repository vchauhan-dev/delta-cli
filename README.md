# Delta Exchange API CLI

A Windows command-line tool for trading on [Delta Exchange India](https://www.delta.exchange) via REST API.

## Download

Download `delta.exe` from [Releases](https://github.com/vchauhan-dev/delta-cli/releases) or use the latest version from the repo.

## Setup

First set up your Delta Exchange credentials:

```
delta config email your@email.com
delta config apikey YOUR_DELTA_API_KEY
delta config apisec YOUR_DELTA_API_SECRET
delta config baseurl https://api.india.delta.exchange
delta config server https://api.deltacharts.in
```

> Contact the server owner to get a license key for full access.

## Usage

### Account

```
delta activate              Check account / license status
delta usage                 Check remaining free API calls
```

### Orders

```
delta buy market <symbol> <size>
delta buy limit <symbol> <size> <price>
delta sell market <symbol> <size>
delta sell limit <symbol> <size> <price>
delta buy stoplimit <symbol> <size> <price> <stop_price>
delta buy stopmarket <symbol> <size> <stop_price>
delta sell stoplimit <symbol> <size> <price> <stop_price>
delta sell stopmarket <symbol> <size> <stop_price>
```

### Bracket Orders

```
delta bracket <symbol> <size> <side> <sl> <tp>
delta bracket-attach <symbol> <sl> <tp>
delta bracket-modify <symbol> [sl] [tp]
delta bracket-remove <symbol>
```

### Modify / Cancel

```
delta modify <order_id> <symbol> <price>
delta cancel <order_id> <symbol>
delta cancelall
delta closeall
```

### Data

```
delta wallet                USD balance
delta positions             Open positions
delta orders [symbol]       Open orders
delta price <symbol>        Bid/ask/mark price
delta leverage <symbol>     Show leverage
delta leverage <symbol> <value>    Set leverage
delta optionchain [symbol] [date]  Option chain
delta candles <symbol> <tf> <from> <to>
  tf: 1m, 5m, 15m, 1h, 4h, 1d
```

### Raw API Calls

```
delta get <path>
delta post <path> <json>
delta put <path> <json>
delta delete <path> [json]
```

### Help

```
delta help                  Show all help
delta help orders           Help for orders only
delta help config           Help for config/auth
delta help data             Help for data commands
delta help api              JSON API endpoints
delta help <keyword>        Search help for keyword
```

## Interactive Mode

Run `delta` with no arguments for REPL mode:

```
DELTA> help
DELTA> buy market C-BTC-59000-100726 1
DELTA> quit
```

## Build from Source

Requires [w64devkit](https://github.com/skeeto/w64devkit) or MinGW-w64:

```
g++ -Os -s -o delta.exe delta_cli.cpp -lwinhttp -lgdi32 -static -std=c++11
```

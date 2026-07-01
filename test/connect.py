
import requests, json
BASE_URL="https://api.deltacharts.in"

r = requests.post(BASE_URL + '/api/token', json={
    'api_key': 'W7vSdcACrxsw29QprOOiytubM9ZQhG',
    'api_secret': 'm0Hy6q3H1u3pBslWP2qHyyRPjO3BOJSkOh3dvMKMbNBmClOiliCYp9fa65Jg',
    'base_url': 'https://cdn-ind.testnet.deltaex.org'
})

#print(r.json()['token'])
TOKEN=r.json()['token']


#r = requests.get(BASE_URL + '/api/wallet', headers={'X-Auth-Token': TOKEN})
#for w in r.json()['result']:
#    print(w['asset_symbol'], w['balance'])


#r = requests.get(BASE_URL + '/api/ticker?symbol=C-BTC-59000-100726', headers={'X-Auth-Token': TOKEN})
#print(r.json())

r = requests.get(BASE_URL + '/api/positions', headers={'X-Auth-Token': TOKEN})
print(r.json())
# Server deployment

The primary production endpoint is `wss://qt.ziyuexc.top/ws`. OpenResty proxies
it to the loopback WS-to-TCP gateway on port 18081; the gateway forwards to the
application on the private Docker network. The bundled Compose nginx provides
an optional TLS/TCP endpoint on port 9443.

## Prerequisites

- Docker Engine with Compose v2
- `qt.ziyuexc.top` pointing to the server
- HTTPS/WSS port 443 open
- A certificate for `qt.ziyuexc.top`
- The WS-to-TCP gateway listening on `127.0.0.1:18081`

## Start

```bash
cd deploy
cp .env.example .env
# Replace both passwords in .env before continuing.
sudo ./gen-certs.sh letsencrypt qt.ziyuexc.top you@example.com
docker compose pull
docker compose up -d
docker compose ps
curl --fail http://127.0.0.1:8080/health
curl --http1.1 --include --max-time 5 \
  -H 'Connection: Upgrade' \
  -H 'Upgrade: websocket' \
  -H 'Sec-WebSocket-Version: 13' \
  -H 'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==' \
  https://qt.ziyuexc.top/ws
```

For an initial private test, use `./gen-certs.sh selfsigned
qt.ziyuexc.top`. Clients must then set
`QTNETWORKCHAT_TLS_PINNED_SHA256` to the fingerprint printed by the script.

## Existing OpenResty

The Qt client supports both the project's TCP/TLS transport and WebSocket/WSS.
The deployed public route is `wss://qt.ziyuexc.top/ws`; OpenResty terminates TLS
and proxies to the WS-to-TCP gateway on `127.0.0.1:18081`, which forwards the
unchanged newline-delimited protocol to `qtnetworkchat-server:8888`.

Include `openresty/qtnetworkchat-wss-location.conf` from the HTTPS virtual host
for `qt.ziyuexc.top`. The gateway must already be listening on loopback port
18081; it must not be exposed directly to the public network.

The file `openresty/qtnetworkchat-stream.conf` remains available for clients
using the legacy TCP/TLS transport on dedicated port 9443.

The file `openresty/qtnetworkchat-stream.conf` is ready to include from the
`stream {}` context of an existing OpenResty configuration. Start the stack
with the OpenResty override so the bundled nginx does not claim port 9443,
then attach the existing OpenResty container to the application network:

```bash
docker compose -f compose.yaml -f compose.openresty.yaml up -d
docker network connect qtnetworkchat_qtnet openresty
docker exec openresty openresty -t
docker exec openresty openresty -s reload
```

Replace `openresty` with the actual container name. The Compose
service provides the network alias `qtnetworkchat-server`, so the stream
upstream remains private at `qtnetworkchat-server:8888`. Do not publish port
8888 on a public interface.

Released clients use port 443 and `/ws` with strict TLS and hostname
verification. Open TCP port 9443 only when the separate stream compatibility
route is required; never publish the backend port 8888.

Release `v1.1.0` and later includes the Qt WebSockets transport. OpenResty still
cannot translate frames by itself; the loopback gateway on port 18081 performs
that bridge.

## Build from source

```bash
cd deploy
docker compose -f compose.yaml -f compose.build.yaml up -d --build
```

## Update and rollback

Set `QTNETWORKCHAT_TAG` in `.env` to an immutable release tag such as `v1.1.0`.

```bash
docker compose pull
docker compose up -d
docker compose logs --tail=200 qqnt-server
```

To roll back, restore the previous tag in `.env` and run the same commands.
Database and attachment data remain in named volumes.

# Gobii computer relay protocol v1

ComputerCpp never exposes its local HTTP/MCP listener to Gobii. The desktop
application creates an authenticated outbound WebSocket using subprotocol
`gobii-computer-relay.v1`. JSON messages are UTF-8 objects and are limited to
512 KiB.

## Pairing

Create a pairing with `POST /api/computers/pairings/`. The request contains
`device_name`, `platform`, `architecture`, `client_version`,
`relay_protocol_version: 1`, and the advertised capabilities. The response
contains `pairing_id`, secret `device_code`, display `user_code`,
`verification_uri`, `verification_uri_complete`, `expires_in`, and `interval`.

The client opens only `verification_uri_complete` in the system browser and
polls `POST /api/computers/pairings/token/` with `pairing_id` and
`device_code`. Errors are `authorization_pending`, `slow_down`,
`access_denied`, or `expired_token`.

Success returns the device identity, rotating device refresh token,
short-lived relay access token and expiry, secure relay URL, and assigned
agent. Refresh uses `POST /api/computers/token/refresh/`; revoke uses
`POST /api/computers/revoke/`. Both authenticate with the device refresh
token. Pairing secrets and access tokens are never persisted.

### Local platform development

Configure a non-Release build with
`COMPUTER_CPP_GOBII_LOCAL_DEVELOPMENT=ON`, then set the platform endpoint:

```sh
cmake -S . -B build/gobii-local -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCOMPUTER_CPP_GOBII_LOCAL_DEVELOPMENT=ON \
  -DCOMPUTER_CPP_GOBII_DEV_INLINE_IMAGES=ON
cmake --build build/gobii-local
build/gobii-local/computer.cpp config set-gobii \
  --base-url http://127.0.0.1:8001
```

This mode permits insecure `http` pairing/browser URLs and `ws` relay URLs
only when their host is `127.0.0.1`, `localhost`, or `[::1]`. Remote insecure
hosts remain rejected. CMake rejects local-development mode in Release
builds. Inline images are controlled independently; enable the capped
development mode as shown when testing tools that return screenshots.

## Relay handshake and control

The WebSocket upgrade includes the relay bearer token, subprotocol, and
`computer.cpp/<version>` user agent. The first client message is:

```json
{
  "type": "hello",
  "protocol_version": 1,
  "device_id": "uuid",
  "client_version": "0.21.0",
  "platform": "macos",
  "architecture": "arm64",
  "permissions": {
    "screen_capture": true,
    "accessibility": true,
    "input": true
  },
  "paused": false,
  "apps": [{
    "name": "gobii-desktop",
    "display_name": "Gobii Desktop",
    "schema_sha256": "hex"
  }]
}
```

Gobii acknowledges with `{"type":"hello.ack","protocol_version":1}`. Either
side may send WebSocket ping frames. ComputerCpp pings every 25 seconds and
closes a connection with no traffic/pong for 60 seconds.

Gobii may send:

- `{"type":"pause"}` to persistently pause and close the connection.
- `{"type":"revoke"}` to remove the local secure credential and device link.
- `{"type":"update_required","required_version":"x.y.z"}` to prevent
  reconnect until the installed version changes.

## MCP requests

```json
{
  "type": "mcp.request",
  "request_id": "globally-unique-id",
  "app": "gobii-desktop",
  "deadline": "2026-07-30T20:00:00Z",
  "payload": {
    "jsonrpc": "2.0",
    "id": 1,
    "method": "tools/list",
    "params": {}
  }
}
```

The payload must be one JSON-RPC 2.0 request object. Batch requests, expired
deadlines, unknown apps, malformed identifiers, and oversized messages are
rejected before local forwarding.

Success:

```json
{
  "type": "mcp.response",
  "request_id": "globally-unique-id",
  "payload": {
    "jsonrpc": "2.0",
    "id": 1,
    "result": {}
  }
}
```

Failure:

```json
{
  "type": "mcp.response",
  "request_id": "globally-unique-id",
  "error": {
    "code": "permissions_required",
    "message": "Screen Recording permission is required.",
    "details": {}
  }
}
```

Defined errors are `offline`, `paused`, `busy`, `request_in_progress`,
`permissions_required`, `locked`, `unknown_app`, `invalid_request`,
`deadline_exceeded`, `payload_too_large`, `local_server_unavailable`,
`update_required`, `unsupported_runtime`, `artifact_upload_unavailable`, and
`internal_error`. A deadline or transport loss after a native action begins
may include `{"completion":"unknown"}`.

## Delivery and deduplication

ComputerCpp keeps 256 request records for at least 15 minutes. The first
receipt executes, a running duplicate returns `request_in_progress`, and a
completed duplicate resends its cached response. A request ID is never
executed twice. Socket loss does not cancel or replay an in-flight action.

## Images

Ordinary local MCP clients receive standard MCP image content. Relay delivery
must replace image bodies with authenticated Gobii artifact references.
The Gobii connection is available in supported production builds, but relay
responses containing images fail with `artifact_upload_unavailable` until the
authenticated upload contract is implemented. Development builds may retain
inline images only when explicitly enabled and when their encoded body is at
most 128 KiB. Release builds reject that development option.

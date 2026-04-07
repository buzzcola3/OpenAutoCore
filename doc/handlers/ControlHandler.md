# ControlHandler

**Channel:** CONTROL

Manages the entire session lifecycle:

- SSL/TLS handshake (multi-step via `ICryptor`)
- Version negotiation and service discovery
- Ping/keepalive on a dedicated thread (5-second interval, `std::thread` + `std::condition_variable`)
- Session teardown (signals `App` via callback)

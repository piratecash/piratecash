PIP-0001 Stage 1: Masternode Messenger (Corsa) integration
==========================================================

This release ships Stage 1 of [PIP-0001](pips/pip-0001.md). Operators that
run a PirateCash masternode now have to point Core at a local Corsa
messenger node, and Core verifies that node at startup and once per hour
afterwards. No consensus rules change; this is an operator-configuration
gate.

Stage 2 (network-visible availability checks) and Stage 3 (PoSe-enforced
proof of Corsa service) remain Draft and will land in later releases.

New configuration options
-------------------------

All options live under the `MASTERNODE` category and are only consulted
when `-masternodeblsprivkey` is set, i.e. when the daemon is started as a
masternode. They have no effect on a regular full node, light wallet, or
on `piratecash-cli`.

* `-corsarpcuser=<user>` — username for the local Corsa node RPC.
  **Required** to start a masternode.
* `-corsarpcpassword=<pw>` — password for the local Corsa node RPC.
  **Required** to start a masternode. Treated as sensitive: never written
  to the debug log, RPC error output, or GUI surfaces.
* `-corsarpcport=<port>` — TCP port of the local Corsa node RPC,
  e.g. `46464`. **Required** to start a masternode. The host is hardcoded
  to `127.0.0.1`: PirateCash Core never connects to a remote Corsa
  endpoint, so HTTP Basic credentials never leave the local machine.
* `-corsarpctimeout=<n>` — per-attempt timeout in seconds for the
  startup probe and the heartbeat monitor (default `10`).
* `-corsarpcattempts=<n>` — number of probe attempts before the daemon
  refuses to start as a masternode (default `5`).
* `-corsarpcretrydelay=<ms>` — pause in milliseconds between startup
  probe attempts (default `2000`).
* `-corsamonitorinterval=<sec>` — pause in seconds between heartbeat
  probes once the masternode is live (default `3600`).

Startup behaviour
-----------------

When `-masternodeblsprivkey` is set, `piratecashd` performs the following
sequence before flipping into masternode mode:

1. Validate that all three of `-corsarpcuser`, `-corsarpcpassword` and
   `-corsarpcport` are set and that the port is in `1..65535` (early
   fail in `AppInitParameterInteraction`).
2. POST `/rpc/v1/system/node_status` to the configured Corsa endpoint
   with HTTP Basic auth. Up to `-corsarpcattempts` attempts are made,
   pausing `-corsarpcretrydelay` between them, but **HTTP `401`/`403`
   from Corsa is treated as a hard, non-retryable failure**: the daemon
   exits immediately with an actionable hint. This is deliberate —
   Corsa locks loopback out for 15 minutes after 10 failed auth
   attempts in 5 minutes, so retrying a typo'd password would burn
   that budget and could brick the next restart. The probe is
   shutdown-aware: pressing Ctrl+C during a retry window aborts cleanly.
3. Validate that the returned JSON contains `identity`, `address`,
   `public_key`, `protocol_version` and `client_version`.
4. Compare `protocol_version` against the per-chain minimum,
   `Params().MinCorsaProtocolVersion()` (`13` on mainnet, testnet,
   devnet and regtest in this release).
5. **Negative auth probe.** Send the same request once more with
   deliberately invalid credentials and require the Corsa side to
   reject it with HTTP `401`/`403`. If the Corsa node returns `200`
   with bogus credentials it is running with auth disabled
   (`CORSA_RPC_USERNAME` / `CORSA_RPC_PASSWORD` unset), and the daemon
   refuses to start. This defends against a misconfigured Corsa node
   where the positive probe would otherwise succeed against an
   unauthenticated endpoint. The negative probe is a single shot — every
   failed auth counts against Corsa's per-IP rate limiter, so we do not
   retry on inconclusive results.

If any of these steps fails the daemon refuses to start as a masternode.
The exact reason is reported via `InitError(...)` and the operator gets
an actionable hint (missing arg, port out of range, HTTP 401/403,
`protocol_version` too low, Corsa auth disabled, etc.). Credentials
never appear in the error.

Runtime behaviour
-----------------

After a successful startup probe, an active masternode launches a single
background thread named `corsa-monitor`. The thread polls the same
`/rpc/v1/system/node_status` endpoint every `-corsamonitorinterval`
seconds and emits an INFO event:

```
Corsa node up: peers=N, uptime=Ns, client=0.42-alpha, protocol=13
```

A failed heartbeat does **not** stop the daemon at Stage 1 — it is
logged as `Corsa monitor: probe failed: <reason>` and the loop tries
again on the next tick. Stage 2/3 will tighten this into network-visible
checks and PoSe penalties; the on-disk integration surface
(`corsa::ProbeConfig`, `corsa::NodeStatus`) is designed to extend
without changing operator-facing flags.

Non-masternode clients (regular full nodes, wallets, `piratecash-cli`)
do not start the monitor and are unaffected by this change.

Compatibility
-------------

This is an operator-configuration change only. No block, transaction,
or consensus rules are affected. v19 may keep running on the existing
mainnet without any masternode action — the only effect is that
masternode operators must install Corsa locally before restarting their
node into v19.

Source references
-----------------

* `src/corsa/client.{h,cpp}` — startup probe and heartbeat monitor.
* `src/init.cpp` — argument registration, parameter-interaction gate,
  startup probe call, monitor lifecycle.
* `src/chainparams.{h,cpp}` — `MinCorsaProtocolVersion()` per network.
* `doc/pips/pip-0001.md` — full specification, including future stages.

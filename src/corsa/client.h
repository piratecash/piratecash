// Copyright (c) 2026 The PirateCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CORSA_CLIENT_H
#define BITCOIN_CORSA_CLIENT_H

#include <chrono>
#include <cstdint>
#include <string>

namespace corsa {

/**
 * Subset of the Corsa /rpc/v1/system/node_status response that PirateCash
 * Core needs at masternode startup (PIP-0001 stage 1) and during the
 * v20 liveness checks (stage 2). Fields not consumed yet are kept as-is
 * so we can extend without changing the wire shape.
 */
struct NodeStatus {
    std::string identity;
    std::string address;
    std::string public_key;
    std::string box_public_key;
    int protocol_version{0};
    int minimum_protocol_version{0};
    std::string client_version;
    int client_build{0};
    int connected_peers{0};
    std::string started_at;
    int64_t uptime_seconds{0};
    std::string current_time;
};

/**
 * Connection parameters for the local Corsa messenger node RPC. The
 * caller (init.cpp) hardcodes `host` to 127.0.0.1 and only the port and
 * credentials come from operator args (-corsarpcport / -corsarpcuser /
 * -corsarpcpassword). Credentials never get logged.
 */
struct ProbeConfig {
    std::string host;                 //!< already split via SplitHostPort, no port suffix
    uint16_t port{0};                 //!< already split via SplitHostPort, must be != 0
    std::string rpc_user;             //!< HTTP Basic auth user
    std::string rpc_password;         //!< HTTP Basic auth password (sensitive, do not log)
    int timeout_seconds{10};          //!< per-attempt connect/read timeout
    int max_attempts{5};              //!< total tries (>=1)
    std::chrono::milliseconds retry_delay{std::chrono::seconds{2}};
};

/**
 * Perform a single authenticated POST /rpc/v1/system/node_status against
 * a local Corsa node and parse the response. Retries with the configured
 * pause until the probe succeeds or max_attempts is exhausted.
 *
 * The function never logs credentials. On success returns true and fills
 * @p out. On failure returns false and writes a sanitised reason into
 * @p error (suitable for InitError() output).
 */
bool ProbeNodeStatus(const ProbeConfig& cfg, NodeStatus& out, std::string& error);

// ---------------------------------------------------------------------------
// Background monitor thread (PIP-0001 stage 1 runtime piece).
//
// Once a masternode has successfully started (initial probe passed), the
// Core daemon spawns a single background thread that periodically polls
// the same /rpc/v1/system/node_status endpoint, logs an INFO event with
// the current peer count and uptime, and keeps doing so until shutdown.
// Future stages (v20 liveness, v21 PoSe proof) extend the monitor with
// additional checks and signed artifacts; for now it is purely
// observational. Credentials are never written to the log.
// ---------------------------------------------------------------------------

/** Default cadence between monitor probes (1 hour). */
constexpr std::chrono::seconds kDefaultMonitorInterval{3600};

/**
 * Start the Corsa monitor thread.
 *
 * The thread executes a probe loop using @p cfg's connection parameters.
 * @p interval controls the pause between successful probes; if a probe
 * fails it is treated like a one-shot ProbeOnce() (no exponential
 * backoff in stage 1) and the next attempt happens after the same
 * interval. Calling StartMonitor() while a previous thread is still
 * running is a no-op.
 */
void StartMonitor(const ProbeConfig& cfg, std::chrono::seconds interval);

/** Wake up the sleeping monitor thread so it can observe shutdown. */
void InterruptMonitor();

/** Join the monitor thread. Safe to call even if it never started. */
void StopMonitor();

} // namespace corsa

#endif // BITCOIN_CORSA_CLIENT_H

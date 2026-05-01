// Copyright (c) 2026 The PirateCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <corsa/client.h>

#include <logging.h>
#include <shutdown.h>
#include <support/events.h>
#include <threadinterrupt.h>
#include <tinyformat.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <util/threadnames.h>
#include <util/time.h>

#include <event2/buffer.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include <algorithm>
#include <cstring>
#include <thread>

namespace corsa {

namespace {

// Path used by Stage 1 / Stage 2 of PIP-0001. The Corsa side also
// accepts /rpc/v1/exec with {"command":"getNodeStatus"} but the route
// form is the documented integration surface.
constexpr const char* kNodeStatusPath = "/rpc/v1/system/node_status";

/** libevent reply buffer (mirrors the helper in bitcoin-cli.cpp). */
struct HTTPReply {
    int status{0};
    int error{-1};
    std::string body;
};

const char* HttpErrorString(int code)
{
    switch (code) {
#if LIBEVENT_VERSION_NUMBER >= 0x02010300
    case EVREQ_HTTP_TIMEOUT:        return "timeout reached";
    case EVREQ_HTTP_EOF:            return "EOF reached";
    case EVREQ_HTTP_INVALID_HEADER: return "invalid header";
    case EVREQ_HTTP_BUFFER_ERROR:   return "buffer error";
    case EVREQ_HTTP_REQUEST_CANCEL: return "request canceled";
    case EVREQ_HTTP_DATA_TOO_LONG:  return "response too large";
#endif
    default:                        return "unknown";
    }
}

void HttpRequestDone(struct evhttp_request* req, void* ctx)
{
    auto* reply = static_cast<HTTPReply*>(ctx);
    if (req == nullptr) {
        // Connect error — error code already set in HttpErrorCb.
        reply->status = 0;
        return;
    }
    reply->status = evhttp_request_get_response_code(req);
    struct evbuffer* buf = evhttp_request_get_input_buffer(req);
    if (buf != nullptr) {
        size_t size = evbuffer_get_length(buf);
        const char* data = reinterpret_cast<const char*>(evbuffer_pullup(buf, size));
        if (data != nullptr) {
            reply->body.assign(data, size);
        }
        evbuffer_drain(buf, size);
    }
}

#if LIBEVENT_VERSION_NUMBER >= 0x02010300
void HttpErrorCb(enum evhttp_request_error err, void* ctx)
{
    auto* reply = static_cast<HTTPReply*>(ctx);
    reply->error = err;
}
#endif

/**
 * Validate the JSON object returned by /rpc/v1/system/node_status and
 * map it onto NodeStatus. Mandatory fields per docs/rpc/system.md:
 *   identity, address, public_key, protocol_version, client_version.
 * Anything else is best-effort.
 */
bool ParseNodeStatus(const std::string& body, NodeStatus& out, std::string& error)
{
    UniValue val;
    if (!val.read(body) || !val.isObject()) {
        error = "Corsa node_status: malformed JSON response";
        return false;
    }

    const UniValue& obj = val.get_obj();

    auto getStr = [&](const std::string& key, std::string& dst, bool required) {
        const UniValue& v = obj[key];
        if (v.isNull()) return !required;
        if (!v.isStr()) return false;
        dst = v.get_str();
        return true;
    };
    auto getInt = [&](const std::string& key, int& dst, bool required) {
        const UniValue& v = obj[key];
        if (v.isNull()) return !required;
        if (!v.isNum()) return false;
        dst = v.get_int();
        return true;
    };
    auto getInt64 = [&](const std::string& key, int64_t& dst, bool required) {
        const UniValue& v = obj[key];
        if (v.isNull()) return !required;
        if (!v.isNum()) return false;
        dst = v.get_int64();
        return true;
    };

    if (!getStr("identity", out.identity, /*required=*/true) || out.identity.empty()) {
        error = "Corsa node_status: missing or empty 'identity'";
        return false;
    }
    if (!getStr("address", out.address, /*required=*/true) || out.address.empty()) {
        error = "Corsa node_status: missing or empty 'address'";
        return false;
    }
    if (!getStr("public_key", out.public_key, /*required=*/true) || out.public_key.empty()) {
        error = "Corsa node_status: missing or empty 'public_key'";
        return false;
    }
    if (!getInt("protocol_version", out.protocol_version, /*required=*/true)) {
        error = "Corsa node_status: missing or invalid 'protocol_version'";
        return false;
    }
    if (!getStr("client_version", out.client_version, /*required=*/true)) {
        error = "Corsa node_status: missing or invalid 'client_version'";
        return false;
    }

    // Best-effort fields.
    getStr("box_public_key", out.box_public_key, /*required=*/false);
    getInt("minimum_protocol_version", out.minimum_protocol_version, /*required=*/false);
    getInt("client_build", out.client_build, /*required=*/false);
    getInt("connected_peers", out.connected_peers, /*required=*/false);
    getStr("started_at", out.started_at, /*required=*/false);
    getInt64("uptime_seconds", out.uptime_seconds, /*required=*/false);
    getStr("current_time", out.current_time, /*required=*/false);

    return true;
}

/** Result of a single probe attempt. */
struct ProbeResult {
    bool ok{false};            //!< true if reply was 200 + valid JSON parsed
    int http_status{0};        //!< 0 if connect/transport failed before headers
    NodeStatus status;         //!< populated only when ok==true
    std::string error;         //!< sanitised; never contains creds or body
};

/**
 * One probe round-trip with explicit credentials. Builds the HTTP request,
 * sends it, captures the HTTP status code (whether 200, 401, 403, …),
 * and parses the body only on a 200. Used both by the regular positive
 * probe (operator credentials) and the negative auth-enforcement probe
 * (deliberately wrong credentials).
 */
ProbeResult DoProbe(const ProbeConfig& cfg,
                    const std::string& rpc_user,
                    const std::string& rpc_password)
{
    ProbeResult result;

    raii_event_base base = obtain_event_base();
    raii_evhttp_connection evcon = obtain_evhttp_connection_base(base.get(), cfg.host, cfg.port);

    const int timeout = std::max(1, cfg.timeout_seconds);
    evhttp_connection_set_timeout(evcon.get(), timeout);

    HTTPReply reply;
    raii_evhttp_request req = obtain_evhttp_request(HttpRequestDone, &reply);
    if (req == nullptr) {
        result.error = "Corsa probe: failed to allocate HTTP request";
        return result;
    }
#if LIBEVENT_VERSION_NUMBER >= 0x02010300
    evhttp_request_set_error_cb(req.get(), HttpErrorCb);
#endif

    // Headers.
    struct evkeyvalq* output_headers = evhttp_request_get_output_headers(req.get());
    if (output_headers == nullptr) {
        result.error = "Corsa probe: cannot set request headers";
        return result;
    }
    const std::string host_header = strprintf("%s:%u", cfg.host, cfg.port);
    evhttp_add_header(output_headers, "Host", host_header.c_str());
    evhttp_add_header(output_headers, "Connection", "close");
    evhttp_add_header(output_headers, "Content-Type", "application/json");
    evhttp_add_header(output_headers, "Accept", "application/json");
    const std::string userpass = rpc_user + ":" + rpc_password;
    const std::string auth = std::string("Basic ") + EncodeBase64(userpass);
    evhttp_add_header(output_headers, "Authorization", auth.c_str());

    // Empty JSON body — getNodeStatus has no args.
    const std::string body = "{}";
    struct evbuffer* output_buffer = evhttp_request_get_output_buffer(req.get());
    if (output_buffer == nullptr) {
        result.error = "Corsa probe: cannot acquire request buffer";
        return result;
    }
    evbuffer_add(output_buffer, body.data(), body.size());

    int r = evhttp_make_request(evcon.get(), req.get(), EVHTTP_REQ_POST, kNodeStatusPath);
    req.release(); // ownership moved into evcon
    if (r != 0) {
        result.error = "Corsa probe: failed to dispatch HTTP request";
        return result;
    }

    event_base_dispatch(base.get());

    result.http_status = reply.status;

    if (reply.status == 0) {
        result.error = strprintf("Corsa probe: connection failed (%s)", HttpErrorString(reply.error));
        return result;
    }
    if (reply.status == 401 || reply.status == 403) {
        result.error = "Corsa probe: authentication rejected";
        return result;
    }
    if (reply.status != 200) {
        result.error = strprintf("Corsa probe: HTTP %d from %s", reply.status, kNodeStatusPath);
        return result;
    }
    if (reply.body.empty()) {
        result.error = "Corsa probe: empty response body";
        return result;
    }

    if (!ParseNodeStatus(reply.body, result.status, result.error)) {
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace

namespace {

/**
 * Run a single positive probe with explicit exception-safety wrapping
 * around DoProbe. Used inside the retry loop so that libevent / RAII
 * allocator throws never crash the daemon — they degrade to a normal
 * "failed attempt".
 */
ProbeResult ProbeOnceSafe(const ProbeConfig& cfg,
                          const std::string& rpc_user,
                          const std::string& rpc_password)
{
    try {
        return DoProbe(cfg, rpc_user, rpc_password);
    } catch (const std::exception& e) {
        ProbeResult r;
        r.error = strprintf("Corsa probe: %s", e.what());
        return r;
    } catch (...) {
        ProbeResult r;
        r.error = "Corsa probe: unknown exception";
        return r;
    }
}

bool SleepBetweenAttemptsInterruptible(std::chrono::milliseconds total,
                                       std::string& error)
{
    using namespace std::chrono;
    const auto slice = milliseconds{100};
    auto remaining = total;
    while (remaining.count() > 0) {
        if (ShutdownRequested()) {
            error = "Corsa probe: shutdown requested";
            return false;
        }
        auto step = remaining < slice ? remaining : slice;
        UninterruptibleSleep(step);
        remaining -= step;
    }
    return true;
}

} // namespace

bool ProbeNodeStatus(const ProbeConfig& cfg, NodeStatus& out, std::string& error)
{
    if (cfg.host.empty() || cfg.port == 0) {
        error = "Corsa probe: invalid host:port";
        return false;
    }
    if (cfg.rpc_user.empty() || cfg.rpc_password.empty()) {
        error = "Corsa probe: missing RPC credentials";
        return false;
    }

    const int attempts = std::max(1, cfg.max_attempts);
    std::string lastError;

    // ----- Positive probe (real credentials, with retries) -----
    //
    // Important: HTTP 401/403 from Corsa is a hard, non-retryable error.
    // Corsa's auth middleware (server.go) keeps a per-IP failure window
    // (authMaxAttempts=10 / authWindowDuration=5min /
    // authLockoutDuration=15min). Burning 5 retries on a typo'd password
    // would push loopback half-way to a 15-minute lockout in one restart
    // and could lock the operator out of the next restart entirely. We
    // only retry on transient/transport errors (connect failure, 5xx,
    // empty body, etc.).
    NodeStatus positive_status;
    bool positive_ok = false;

    for (int i = 1; i <= attempts; ++i) {
        if (ShutdownRequested()) {
            error = "Corsa probe: shutdown requested";
            return false;
        }

        ProbeResult res = ProbeOnceSafe(cfg, cfg.rpc_user, cfg.rpc_password);
        if (res.ok) {
            // Do not log credentials. Identity/address/public_key are public.
            LogPrintf("Corsa probe: ok (identity=%s, peers=%d, client=%s, attempt=%d/%d)\n",
                      res.status.identity, res.status.connected_peers,
                      res.status.client_version, i, attempts);
            positive_status = std::move(res.status);
            positive_ok = true;
            break;
        }

        // Fail-fast on auth rejection: do not eat Corsa's rate-limit budget.
        if (res.http_status == 401 || res.http_status == 403) {
            error = strprintf(
                "Corsa probe: authentication rejected (HTTP %d). "
                "Check -corsarpcuser / -corsarpcpassword against the local "
                "Corsa node's CORSA_RPC_USERNAME / CORSA_RPC_PASSWORD. "
                "Note: Corsa locks out the loopback IP for 15 minutes after "
                "10 failed auth attempts in 5 minutes — fix the credentials "
                "before retrying so you do not exhaust that budget "
                "(PIP-0001 stage 1).",
                res.http_status);
            return false;
        }

        LogPrintf("Corsa probe: attempt %d/%d failed: %s\n", i, attempts, res.error);
        lastError = std::move(res.error);
        if (i < attempts) {
            if (!SleepBetweenAttemptsInterruptible(
                    std::chrono::duration_cast<std::chrono::milliseconds>(cfg.retry_delay),
                    error)) {
                return false;
            }
        }
    }

    if (!positive_ok) {
        error = strprintf("Corsa probe: all %d attempts failed; last error: %s",
                          attempts, lastError);
        return false;
    }

    // ----- Negative probe (defence-in-depth: confirm Corsa actually
    //       enforces auth rather than accepting anonymous requests).
    //
    //       Corsa's docs say the RPC server is only started when
    //       CORSA_RPC_USERNAME / CORSA_RPC_PASSWORD are set, but custom
    //       Corsa builds, future changes, or alternate launchers could
    //       expose an unauthenticated /rpc/v1/system/node_status. We
    //       refuse to mark the masternode healthy unless deliberately
    //       wrong credentials get rejected.
    //
    //       The negative credentials are derived from the operator's
    //       real ones by appending a fixed sentinel suffix to the
    //       password. Since the resulting string is strictly longer
    //       than the real password it cannot equal it — there is no
    //       way for a misconfigured Corsa to accept these creds as
    //       valid. Use the operator's real username so the
    //       Authorization header parses identically; only the password
    //       comparison must fail.
    //
    //       This is intentionally a single attempt: every 401 we
    //       generate counts against Corsa's per-IP rate limiter
    //       (10 fails / 5 min → 15 min lockout). The positive probe
    //       already succeeded against the same socket microseconds
    //       earlier, so transport noise here is extremely unlikely; if
    //       the negative probe is somehow inconclusive we just refuse.
    constexpr const char* kNegativeProbeSuffix =
        "::piratecash-corsa-stage1-neg-probe-invalid";
    const std::string negative_password = cfg.rpc_password + kNegativeProbeSuffix;

    if (ShutdownRequested()) {
        error = "Corsa probe: shutdown requested";
        return false;
    }
    ProbeResult neg = ProbeOnceSafe(cfg, cfg.rpc_user, negative_password);

    if (neg.http_status == 401 || neg.http_status == 403) {
        // Auth is enforced — exactly what we want.
        LogPrintf("Corsa probe: auth enforcement verified (negative probe got HTTP %d)\n",
                  neg.http_status);
        out = std::move(positive_status);
        return true;
    }
    if (neg.ok || neg.http_status == 200) {
        // Bogus credentials accepted: Corsa is running without auth.
        error = "Corsa probe: local Corsa RPC accepts unauthenticated "
                "requests (got HTTP 200 with deliberately invalid "
                "credentials). Configure CORSA_RPC_USERNAME and "
                "CORSA_RPC_PASSWORD on the Corsa node before starting "
                "the masternode (PIP-0001 stage 1).";
        return false;
    }

    // Negative probe inconclusive (transport / 5xx / etc.). We cannot
    // prove auth is enforced and won't burn more rate-limit budget on
    // retries — refuse with whatever signal we got.
    error = strprintf(
        "Corsa probe: could not verify authentication enforcement "
        "(negative probe inconclusive: HTTP=%d, %s). Refusing to start "
        "the masternode (PIP-0001 stage 1).",
        neg.http_status, neg.error);
    return false;
}

// ---------------------------------------------------------------------------
// Background monitor thread.
//
// The interrupt object has static storage duration on purpose — its address
// is stable for the entire process lifetime, so MonitorLoop can read it
// concurrently with InterruptMonitor()/StopMonitor() without racing on the
// pointer itself. Internal synchronisation lives inside CThreadInterrupt.
// This mirrors the pattern used by mapport.cpp for its NAT-PMP/UPnP thread.
// ---------------------------------------------------------------------------

namespace {

CThreadInterrupt g_monitor_interrupt;
std::thread g_monitor_thread;

void MonitorLoop(ProbeConfig cfg, std::chrono::seconds interval)
{
    util::ThreadRename("corsa-monitor");
    LogPrintf("Corsa monitor: started (interval=%ds)\n",
              static_cast<int>(interval.count()));

    // Run until interrupted. The first poll happens immediately so the
    // operator gets an early INFO line confirming the loop is alive — the
    // startup probe already validated reachability and auth enforcement,
    // so this loop only does the cheap positive probe per heartbeat.
    while (!ShutdownRequested() && !(bool)g_monitor_interrupt) {
        ProbeResult res = ProbeOnceSafe(cfg, cfg.rpc_user, cfg.rpc_password);

        if (res.ok) {
            // INFO event consumed by operators. Stage 2/3 extensions can
            // add more fields (peer health, signed proof, drift, etc.).
            LogPrintf("Corsa node up: peers=%d, uptime=%ds, client=%s, protocol=%d\n",
                      res.status.connected_peers,
                      static_cast<int>(res.status.uptime_seconds),
                      res.status.client_version,
                      res.status.protocol_version);
        } else {
            // Stage 1 is observational only: do not punish or shut down on
            // a single failure, just surface it.
            LogPrintf("Corsa monitor: probe failed: %s\n", res.error);
        }

        // Interruptible sleep — Interrupt() / Stop() / shutdown wake us up.
        // CThreadInterrupt::sleep_for returns false on interrupt, true on
        // a clean timeout (matches the convention used in net.cpp).
        if (!g_monitor_interrupt.sleep_for(interval)) {
            break;
        }
    }

    LogPrintf("Corsa monitor: stopped\n");
}

} // namespace

void StartMonitor(const ProbeConfig& cfg, std::chrono::seconds interval)
{
    // Defensive guard: the monitor is only meaningful on an active masternode
    // that has already passed the PIP-0001 stage 1 startup probe. Refuse to
    // run with an obviously incomplete config — that way an accidental call
    // from a non-masternode code path or a misconfigured operator setup
    // cannot quietly spin up a useless polling loop.
    if (cfg.host.empty() || cfg.port == 0 ||
        cfg.rpc_user.empty() || cfg.rpc_password.empty()) {
        LogPrintf("Corsa monitor: not starting — incomplete probe config\n");
        return;
    }
    if (g_monitor_thread.joinable()) {
        // Already running.
        return;
    }
    if (ShutdownRequested()) {
        return;
    }
    if (interval.count() <= 0) {
        interval = kDefaultMonitorInterval;
    }

    // If a previous run was stopped and we ever re-enter (e.g. unit tests
    // or future hot-restart support), clear the latched interrupt flag so
    // the new loop iteration is not a no-op.
    g_monitor_interrupt.reset();
    g_monitor_thread = std::thread(MonitorLoop, cfg, interval);
}

void InterruptMonitor()
{
    if (g_monitor_thread.joinable()) {
        g_monitor_interrupt();
    }
}

void StopMonitor()
{
    if (g_monitor_thread.joinable()) {
        g_monitor_interrupt();
        g_monitor_thread.join();
        g_monitor_interrupt.reset();
    }
}

} // namespace corsa

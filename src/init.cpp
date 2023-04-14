// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2018-2023 The PirateCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "init.h"

#include "addrman.h"
#include "main.h"
#include "chainparams.h"
#include "scheduler.h"
#include "txdb.h"
#include "rpcserver.h"
#include "net.h"
#include "key.h"
#include "pubkey.h"
#include "util.h"
#include "utilmoneystr.h"
#include "torcontrol.h"
#include "ui_interface.h"
#include "checkpoints.h"
#include "darksend-relay.h"
#include "activemasternode.h"
#include "masternode-payments.h"
#include "masternode.h"
#include "masternodeman.h"
#include "masternodeconfig.h"
#include "spork.h"

#ifdef ENABLE_WALLET
#include "wallet/db.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#endif

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/function.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/thread.hpp>
#include <openssl/crypto.h>

#ifndef WIN32
#include <signal.h>
#endif


using namespace std;
using namespace boost;

static const bool DEFAULT_MASTERNODE  = false;


#ifdef ENABLE_WALLET
CWallet* pwalletMain = NULL;
int nWalletBackups = 10;
#endif
bool fFeeEstimatesInitialized = false;
bool fRestartRequested = false;  // true: restart false: shutdown
CClientUIInterface uiInterface;

// Used to pass flags to the Bind() function
enum BindFlags {
    BF_NONE         = 0,
    BF_EXPLICIT     = (1U << 0),
    BF_REPORT_ERROR = (1U << 1),
    BF_WHITELIST    = (1U << 2),
};

bool fConfChange;
unsigned int nNodeLifespan;
unsigned int nDerivationMethodIndex;
unsigned int nMinerSleep;
bool fUseFastIndex;
bool fOnlyTor = false;

#ifdef WIN32
// Win32 LevelDB doesn't use filedescriptors, and the ones used for
// accessing block files don't count towards the fd_set size limit
// anyway.
#define MIN_CORE_FILEDESCRIPTORS 0
#else
#define MIN_CORE_FILEDESCRIPTORS 150
#endif

//////////////////////////////////////////////////////////////////////////////
//
// Shutdown
//

//
// Thread management and startup/shutdown:
//
// The network-processing threads are all part of a thread group
// created by AppInit() or the Qt main() function.
//
// A clean exit happens when StartShutdown() or the SIGTERM
// signal handler sets fRequestShutdown, which triggers
// the DetectShutdownThread(), which interrupts the main thread group.
// DetectShutdownThread() then exits, which causes AppInit() to
// continue (it .joins the shutdown thread).
// Shutdown() is then
// called to clean up database connections, and stop other
// threads that should only be stopped after the main network-processing
// threads have exited.
//
// Note that if running -daemon the parent process returns from AppInit2
// before adding any threads to the threadGroup, so .join_all() returns
// immediately and the parent exits from main().
//
// Shutdown for Qt is very similar, only it uses a QTimer to detect
// fRequestShutdown getting set, and then does the normal Qt
// shutdown thing.
//

volatile bool fRequestShutdown = false;

void StartShutdown()
{
    fRequestShutdown = true;
}
bool ShutdownRequested()
{
    return fRequestShutdown || fRestartRequested;
}

/** Preparing steps before shutting down or restarting the wallet */
void PrepareShutdown(){
    fRestartRequested = true; // Needed when we restart the wallet
    LogPrintf("%s: In progress...\n", __func__);
    static CCriticalSection cs_Shutdown;
    TRY_LOCK(cs_Shutdown, lockShutdown);
    if (!lockShutdown)
        return;

    /// Note: Shutdown() must be able to handle cases in which AppInit2() failed part of the way,
    /// for example if the data directory was found to be locked.
    /// Be sure that anything that writes files or flushes caches only does this if the respective
    /// module was initialized.
    RenameThread("piratecash-shutoff");
    mempool.AddTransactionsUpdated(1);
    StopRPCThreads();
#ifdef ENABLE_WALLET
    ShutdownRPCMining();
    if (pwalletMain)
        bitdb.Flush(false);
#endif
    StopNode();
    UnregisterNodeSignals(GetNodeSignals());
    if (fFeeEstimatesInitialized)
    {
        //will be implemeneted later
        fFeeEstimatesInitialized = false;
    }
    DumpMasternodes();
    {
        LOCK(cs_main);
#ifdef ENABLE_WALLET
        if (pwalletMain)
            pwalletMain->SetBestChain(CBlockLocator(pindexBest));
#endif
    }
#ifdef ENABLE_WALLET
    if (pwalletMain)
        bitdb.Flush(true);
#endif
}

static boost::scoped_ptr<ECCVerifyHandle> globalVerifyHandle;

void Interrupt(boost::thread_group& threadGroup)
{
    InterruptTorControl();
    threadGroup.interrupt_all();
}

/**
* Shutdown is split into 2 parts:
* Part 1: shut down everything but the main wallet instance (done in PrepareShutdown() )
* Part 2: delete wallet instance
*
* In case of a restart PrepareShutdown() was already called before, but this method here gets
* called implicitly when the parent object is deleted. In this case we have to skip the
* PrepareShutdown() part because it was already executed and just delete the wallet instance.
*/
void Shutdown()
{
	fRequestShutdown = true; // Needed when we shutdown the wallet
    // true is workaround (need move it to the void BitcoinCore::restart(QStringList args) - this function isn't implemented ye)
    if(!fRestartRequested || true){ // most of shutdown is already done when we're restarting the wallet
        PrepareShutdown();
    }
    // Shutdown part 2: Stop TOR thread and delete wallet instance
    StopTorControl();
#ifndef WIN32
    boost::filesystem::remove(GetPidFile());
#endif
    UnregisterAllWallets();
#ifdef ENABLE_WALLET
    delete pwalletMain;
    pwalletMain = NULL;
#endif
    globalVerifyHandle.reset();
    ECC_Stop();
    LogPrintf("Shutdown : done\n");
}

//
// Signal handlers are very limited in what they are allowed to do, so:
//
void HandleSIGTERM(int)
{
    fRequestShutdown = true;
}

void HandleSIGHUP(int)
{
    fReopenDebugLog = true;
}

bool static InitError(const std::string &str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_ERROR);
    return false;
}

bool static InitWarning(const std::string &str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_WARNING);
    return true;
}

bool static Bind(const CService &addr, unsigned int flags) {
    if (!(flags & BF_EXPLICIT) && IsLimited(addr))
        return false;
    std::string strError;
    if (!BindListenPort(addr, strError, flags & BF_WHITELIST) != 0) {
        if (flags & BF_REPORT_ERROR)
            return InitError(strError);
        return false;
    }
    return true;
}

void OnRPCStopped()
 {
     cvBlockChange.notify_all();
     LogPrint("rpc", "RPC stopped.\n");
 }

 void OnRPCPreCommand(const CRPCCommand& cmd)
 {
 #ifdef ENABLE_WALLET
     if (cmd.reqWallet && !pwalletMain)
         throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found (disabled)");
 #endif

     // Observe safe mode
     string strWarning = GetWarnings("rpc");
     if (strWarning != "" && !GetBoolArg("-disablesafemode", false) &&
         !cmd.okSafeMode)
         throw JSONRPCError(RPC_FORBIDDEN_BY_SAFE_MODE, string("Safe mode: ") + strWarning);
 }

std::string HelpMessage()
{
    string strUsage = _("Options:") + "\n";
    strUsage += "  -?                     " + _("This help message") + "\n";
    strUsage += "  -conf=<file>           " + _("Specify configuration file (default: piratecash.conf)") + "\n";
    strUsage += "  -pid=<file>            " + _("Specify pid file (default: piratecashd.pid)") + "\n";
    strUsage += "  -datadir=<dir>         " + _("Specify data directory") + "\n";
    strUsage += "  -wallet=<dir>          " + _("Specify wallet file (within data directory)") + "\n";
    strUsage += "  -dbcache=<n>           " + _("Set database cache size in megabytes (default: 10)") + "\n";
    strUsage += "  -dbwalletcache=<n>     " + _("Set wallet database cache size in megabytes (default: 1)") + "\n";
    strUsage += "  -dblogsize=<n>         " + _("Set database disk log size in megabytes (default: 100)") + "\n";
    strUsage += "  -timeout=<n>           " + strprintf(_("Specify connection timeout in milliseconds (minimum: 1, default: %d)"), DEFAULT_CONNECT_TIMEOUT) + "\n";
    strUsage += "  -torcontrol=<ip>:<port>" + strprintf(_("Tor control port to use if onion listening enabled (default: %s)"), DEFAULT_TOR_CONTROL);
    strUsage += "  -torpassword=<pass>    " + _("Tor control port password (default: empty)");
    strUsage += "  -proxy=<ip:port>       " + _("Connect through SOCKS5 proxy") + "\n";
    strUsage += "  -onion=<ip:port>       " + _("Use proxy to reach tor hidden services (default: same as -proxy)") + "\n";
    strUsage += "  -dns                   " + _("Allow DNS lookups for -addnode, -seednode and -connect") + "\n";
    strUsage += "  -port=<port>           " + _("Listen for connections on <port> (default: 23232)") + "\n";
    strUsage += "  -maxconnections=<n>    " + _("Maintain at most <n> connections to peers (default: 125)") + "\n";
#ifndef WIN32
    strUsage += "  -pid=<file>            " + _("Specify pid file (default: piratecashd.pid)") + "\n";
#endif
    strUsage += "  -addnode=<ip>          " + _("Add a node to connect to and attempt to keep the connection open") + "\n";
    strUsage += "  -connect=<ip>          " + _("Connect only to the specified node(s)") + "\n";
    strUsage += "  -seednode=<ip>         " + _("Connect to a node to retrieve peer addresses, and disconnect") + "\n";
    strUsage += "  -externalip=<ip>       " + _("Specify your own public address") + "\n";
    strUsage += "  -onlynet=<net>         " + _("Only connect to nodes in network <net> (IPv4, IPv6 or Onion)") + "\n";
    strUsage += "  -discover              " + _("Discover own IP address (default: 1 when listening and no -externalip)") + "\n";
    strUsage += "  -irc                   " + _("Find peers using internet relay chat (default: 0)") + "\n";
    strUsage += "  -listen                " + _("Accept connections from outside (default: 1 if no -proxy or -connect)") + "\n";
    strUsage += "  -listenonion           " + strprintf(_("Automatically create Tor hidden service (default: %d)"), DEFAULT_LISTEN_ONION);
    strUsage += "  -bind=<addr>           " + _("Bind to given address and always listen on it. Use [host]:port notation for IPv6") + "\n";
    strUsage += "  -dnsseed               " + _("Query for peer addresses via DNS lookup, if low on addresses (default: 1 unless -connect)") + "\n";
    strUsage += "  -forcednsseed          " + _("Always query for peer addresses via DNS lookup (default: 0)") + "\n";
    strUsage += "  -synctime              " + _("Sync time with other nodes. Disable if time on your system is precise e.g. syncing with NTP (default: 1)") + "\n";
    strUsage += "  -banscore=<n>          " + _("Threshold for disconnecting misbehaving peers (default: 100)") + "\n";
    strUsage += "  -bantime=<n>           " + _("Number of seconds to keep misbehaving peers from reconnecting (default: 86400)") + "\n";
    strUsage += "  -maxreceivebuffer=<n>  " + _("Maximum per-connection receive buffer, <n>*1000 bytes (default: 5000)") + "\n";
    strUsage += "  -maxsendbuffer=<n>     " + _("Maximum per-connection send buffer, <n>*1000 bytes (default: 1000)") + "\n";
#ifdef USE_UPNP
#if USE_UPNP
    strUsage += "  -upnp                  " + _("Use UPnP to map the listening port (default: 1 when listening)") + "\n";
#else
    strUsage += "  -upnp                  " + _("Use UPnP to map the listening port (default: 0)") + "\n";
#endif
#endif
    strUsage += "  -whitebind=<addr>      " + _("Bind to given address and whitelist peers connecting to it. Use [host]:port notation for IPv6") + "\n";
    strUsage += "  -whitelist=<netmask>   " + _("Whitelist peers connecting from the given netmask or ip. Can be specified multiple times.") + "\n";
    strUsage += "  -whiteconnections=<n>", strprintf(_("Reserve this many inbound connections for whitelisted peers (default: %d)"), 0);
    strUsage += "  -paytxfee=<amt>        " + _("Fee per KB to add to transactions you send") + "\n";
    strUsage += "  -mininput=<amt>        " + _("When creating transactions, ignore inputs with value less than this (default: 0.01)") + "\n";
    if (fHaveGUI)
        strUsage += "  -server                " + _("Accept command line and JSON-RPC commands") + "\n";
#if !defined(WIN32)
    if (fHaveGUI)
        strUsage += "  -daemon                " + _("Run in the background as a daemon and accept commands") + "\n";
#endif
    strUsage += "  -testnet               " + _("Use the test network") + "\n";
    strUsage += "  -debug=<category>      " + _("Output debugging information (default: 0, supplying <category> is optional)") + "\n";
    strUsage +=                               _("If <category> is not supplied, output all debugging information.") + "\n";
    strUsage +=                               _("<category> can be:");
    strUsage +=                                 " addrman, alert, db, lock, rand, rpc, selectcoins, mempool, net,"; // Don't translate these and qt below
    strUsage +=                                 " coinage, coinstake, creation, stakemodifier, tor";
    if (fHaveGUI){
        strUsage += ", qt.\n";
    }else{
        strUsage += ".\n";
    }
    strUsage += "  -logips                " + _("Include IP addresses in debug output (default: 0)") + "\n";
    strUsage += "  -logtimestamps         " + _("Prepend debug output with timestamp") + "\n";
    strUsage += "  -shrinkdebugfile       " + _("Shrink debug.log file on client startup (default: 1 when no -debug)") + "\n";
    strUsage += "  -printtoconsole        " + _("Send trace/debug info to console instead of debug.log file") + "\n";
    strUsage += "  -regtest               " + _("Enter regression test mode, which uses a special chain in which blocks can be "
                                                "solved instantly. This is intended for regression testing tools and app development.") + "\n";
    strUsage += "  -rpcuser=<user>        " + _("Username for JSON-RPC connections") + "\n";
    strUsage += "  -rpcpassword=<pw>      " + _("Password for JSON-RPC connections") + "\n";
    strUsage += "  -rpcport=<port>        " + _("Listen for JSON-RPC connections on <port> (default: 3535)") + "\n";
    strUsage += "  -rpcallowip=<ip>       " + _("Allow JSON-RPC connections from specified IP address") + "\n";
    if (!fHaveGUI){
        strUsage += "  -rpcconnect=<ip>       " + _("Send commands to node running on <ip> (default: 127.0.0.1)") + "\n";
        strUsage += "  -rpcwait               " + _("Wait for RPC server to start") + "\n";
    }
    strUsage += "  -rpcthreads=<n>        " + _("Set the number of threads to service RPC calls (default: 4)") + "\n";
    strUsage += "  -blocknotify=<cmd>     " + _("Execute command when the best block changes (%s in cmd is replaced by block hash)") + "\n";
    strUsage += "  -walletnotify=<cmd>    " + _("Execute command when a wallet transaction changes (%s in cmd is replaced by TxID)") + "\n";
    strUsage += "  -confchange            " + _("Require a confirmations for change (default: 0)") + "\n";
    strUsage += "  -alertnotify=<cmd>     " + _("Execute command when a relevant alert is received (%s in cmd is replaced by message)") + "\n";
    strUsage += "  -upgradewallet         " + _("Upgrade wallet to latest format") + "\n";
    strUsage += "  -createwalletbackups=<n> " + _("Number of automatic wallet backups (default: 10)") + "\n";
    strUsage += "  -keypool=<n>           " + strprintf(_("Set key pool size to <n> (default: %u)"), DEFAULT_KEYPOOL_SIZE) + "\n";
    strUsage += "  -rescan                " + _("Rescan the block chain for missing wallet transactions") + "\n";
    strUsage += "  -salvagewallet         " + _("Attempt to recover private keys from a corrupt wallet.dat") + "\n";
    strUsage += "  -checkblocks=<n>       " + _("How many blocks to check at startup (default: 500, 0 = all)") + "\n";
    strUsage += "  -checklevel=<n>        " + _("How thorough the block verification is (0-6, default: 1)") + "\n";
    strUsage += "  -loadblock=<file>      " + _("Imports blocks from external blk000?.dat file") + "\n";
    strUsage += "  -maxorphanblocks=<n>   " + strprintf(_("Keep at most <n> unconnectable blocks in memory (default: %u)"), DEFAULT_MAX_ORPHAN_BLOCKS) + "\n";

    strUsage += "\n" + _("Block creation options:") + "\n";
    strUsage += "  -blockminsize=<n>      "   + _("Set minimum block size in bytes (default: 0)") + "\n";
    strUsage += "  -blockmaxsize=<n>      "   + _("Set maximum block size in bytes (default: 250000)") + "\n";
    strUsage += "  -blockprioritysize=<n> "   + _("Set maximum size of high-priority/low-fee transactions in bytes (default: 27000)") + "\n";

    strUsage += "\n" + _("SSL options: (see the Bitcoin Wiki for SSL setup instructions)") + "\n";
    strUsage += "  -rpcssl                                  " + _("Use OpenSSL (https) for JSON-RPC connections") + "\n";
    strUsage += "  -rpcsslcertificatechainfile=<file.cert>  " + _("Server certificate file (default: server.cert)") + "\n";
    strUsage += "  -rpcsslprivatekeyfile=<file.pem>         " + _("Server private key (default: server.pem)") + "\n";
    strUsage += "  -rpcsslciphers=<ciphers>                 " + _("Acceptable ciphers (default: TLSv1.2+HIGH:TLSv1+HIGH:!SSLv3:!SSLv2:!aNULL:!eNULL:!3DES:@STRENGTH)") + "\n";
    strUsage += "  -litemode=<n>          " + _("Disable all Darksend and Stealth Messaging related functionality (0-1, default: 0)") + "\n";
strUsage += "\n" + _("Masternode options:") + "\n";
    strUsage += "  -masternode=<n>            " + _("Enable the client to act as a masternode (0-1, default: 0)") + "\n";
    strUsage += "  -mnconf=<file>             " + _("Specify masternode configuration file (default: masternode.conf)") + "\n";
    strUsage += "  -mnconflock=<n>            " + _("Lock masternodes from masternode configuration file (default: 1)") + "\n";
    strUsage += "  -masternodeprivkey=<n>     " + _("Set the masternode private key") + "\n";
    strUsage += "  -masternodeaddr=<n>        " + _("Set external address:port to get to this masternode (example: address:port)") + "\n";
    strUsage += "  -masternodeminprotocol=<n> " + strprintf(_("Ignore masternodes less than version (example: 70050; default: %u)"), masternodePayments.GetMinMasternodePaymentsProto()) + "\n";

    strUsage += "\n" + _("Darksend options:") + "\n";
    strUsage += "  -enabledarksend=<n>          " + _("Enable use of automated darksend for funds stored in this wallet (0-1, default: 0)") + "\n";
    strUsage += "  -darksendrounds=<n>          " + _("Use N separate masternodes to anonymize funds  (2-8, default: 2)") + "\n";
    strUsage += "  -anonymizepiratecashamount=<n> " + _("Keep N Piratecash anonymized (default: 0)") + "\n";
    strUsage += "  -liquidityprovider=<n>       " + _("Provide liquidity to Darksend by infrequently mixing coins on a continual basis (0-100, default: 0, 1=very frequent, high fees, 100=very infrequent, low fees)") + "\n";

    strUsage += "\n" + _("InstantX options:") + "\n";
    strUsage += "  -enableinstantx=<n>    " + _("Enable instantx, show confirmations for locked transactions (bool, default: true)") + "\n";
    strUsage += "  -instantxdepth=<n>     " + strprintf(_("Show N confirmations for a successfully locked transaction (0-9999, default: %u)"), nInstantXDepth) + "\n"; 
    strUsage += "\n" + _("Staking options:") +  "  -stakethreshold=<n> " + _("This will set the output size of your stakes to never be below this number (default: 250)") + "\n" +
    strUsage += "  -inputstakeprotect=<n> " + _("Don't use masternode collateral and denominated amounts for staking (0-1, default: 1)") + "\n";

    return strUsage;
}

/** Sanity checks
 *  Ensure that Bitcoin is running in a usable environment with all
 *  necessary library support.
 */
bool InitSanityCheck(void)
{
    if(!ECC_InitSanityCheck()) {
        InitError("OpenSSL appears to lack support for elliptic curve cryptography. For more "
                  "information, visit https://en.bitcoin.it/wiki/OpenSSL_and_EC_Libraries");
        return false;
    }

    // TODO: remaining sanity checks, see #4081

    return true;
}

std::string LicenseInfo()
{
    return FormatParagraph(strprintf(_("Copyright (C) 2009-%i The Bitcoin Core developers"), COPYRIGHT_YEAR)) + "\n" +
           "\n" +
           FormatParagraph(strprintf(_("Copyright (C) 2014-%i The Dash Core developers"), COPYRIGHT_YEAR)) + "\n" +
           "\n" +
           FormatParagraph(strprintf(_("Copyright (C) 2014-%i The PIVX developers"), COPYRIGHT_YEAR)) + "\n" +
           "\n" +
           FormatParagraph(strprintf(_("Copyright (C) 2012-%i The NovaCoin developers"), COPYRIGHT_YEAR)) + "\n" +
           "\n" +
           FormatParagraph(strprintf(_("Copyright (C) 2018-%i The PirateCash developers"), COPYRIGHT_YEAR)) + "\n" +
           "\n" +
           FormatParagraph(_("This is experimental software.")) + "\n" +
           "\n" +
           FormatParagraph(_("Distributed under the MIT/X11 software license, see the accompanying file COPYING or <http://www.opensource.org/licenses/mit-license.php>.")) + "\n" +
           "\n" +
           FormatParagraph(_("This product includes software developed by the OpenSSL Project for use in the OpenSSL Toolkit <https://www.openssl.org/> and cryptographic software written by Eric Young and UPnP software written by Thomas Bernard.")) +
           "\n";
}

struct CImportingNow
{
    CImportingNow() {
        assert(fImporting == false);
        fImporting = true;
    }

    ~CImportingNow() {
        assert(fImporting == true);
        fImporting = false;
    }
};

void ThreadImport(std::vector<boost::filesystem::path> vImportFiles)
{
    RenameThread("piratecash-loadblk");

    CImportingNow imp;

    // -loadblock=
    BOOST_FOREACH(boost::filesystem::path &path, vImportFiles) {
        FILE *file = fopen(path.string().c_str(), "rb");
        if (file)
            LoadExternalBlockFile(file);
    }

    // hardcoded $DATADIR/bootstrap.dat
    filesystem::path pathBootstrap = GetDataDir() / "bootstrap.dat";
    if (filesystem::exists(pathBootstrap)) {
        FILE *file = fopen(pathBootstrap.string().c_str(), "rb");
        if (file) {
            filesystem::path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
            LoadExternalBlockFile(file);
            RenameOver(pathBootstrap, pathBootstrapOld);
        }
    }
}

/** Initialize bitcoin.
 *  @pre Parameters should be parsed and config file should be read.
 */
bool AppInit2(boost::thread_group& threadGroup, CScheduler& scheduler)
{
    // ********************************************************* Step 1: setup
#ifdef _MSC_VER
    // Turn off Microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFileA("NUL", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0));
#endif
#if _MSC_VER >= 1400
    // Disable confusing "helpful" text message on abort, Ctrl-C
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifdef WIN32
    // Enable Data Execution Prevention (DEP)
    // Minimum supported OS versions: WinXP SP3, WinVista >= SP1, Win Server 2008
    // A failure is non-critical and needs no further attention!
#ifndef PROCESS_DEP_ENABLE
    // We define this here, because GCCs winbase.h limits this to _WIN32_WINNT >= 0x0601 (Windows 7),
    // which is not correct. Can be removed, when GCCs winbase.h is fixed!
#define PROCESS_DEP_ENABLE 0x00000001
#endif
    typedef BOOL (WINAPI *PSETPROCDEPPOL)(DWORD);
    PSETPROCDEPPOL setProcDEPPol = (PSETPROCDEPPOL)GetProcAddress(GetModuleHandleA("Kernel32.dll"), "SetProcessDEPPolicy");
    if (setProcDEPPol != NULL) setProcDEPPol(PROCESS_DEP_ENABLE);
#endif
#ifndef WIN32
    umask(077);

    // Clean shutdown on SIGTERM
    struct sigaction sa;
    sa.sa_handler = HandleSIGTERM;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    // Reopen debug.log on SIGHUP
    struct sigaction sa_hup;
    sa_hup.sa_handler = HandleSIGHUP;
    sigemptyset(&sa_hup.sa_mask);
    sa_hup.sa_flags = 0;
    sigaction(SIGHUP, &sa_hup, NULL);

    // Ignore SIGPIPE, otherwise it will bring the daemon down if the client closes unexpectedly
    signal(SIGPIPE, SIG_IGN);
#endif

    // ********************************************************* Step 2: parameter interactions

    nNodeLifespan = GetArg("-addrlifespan", 7);
    fUseFastIndex = GetBoolArg("-fastindex", true);
    nMinerSleep = GetArg("-minersleep", 500);

    nDerivationMethodIndex = 0;

    if (!SelectParamsFromCommandLine()) {
        return InitError("Invalid combination of -testnet and -regtest.");
    }

    if (TestNet())
    {
        SPEC_TARGET_FIX = 104000;
        SoftSetBoolArg("-irc", true);
    }

    if (mapArgs.count("-bind") || mapArgs.count("-whitebind")) {
        // when specifying an explicit binding address, you want to listen on it
        // even when -connect or -proxy is specified
        if (SoftSetBoolArg("-listen", true))
            LogPrintf("AppInit2 : parameter interaction: -bind or -whitebind set -> setting -listen=1\n");
    }

    // Process masternode config
    masternodeConfig.read(GetMasternodeConfigFile());

    if (mapArgs.count("-connect") && mapMultiArgs["-connect"].size() > 0) {
        // when only connecting to trusted nodes, do not seed via DNS, or listen by default
        if (SoftSetBoolArg("-dnsseed", false))
            LogPrintf("AppInit2 : parameter interaction: -connect set -> setting -dnsseed=0\n");
        if (SoftSetBoolArg("-listen", false))
            LogPrintf("AppInit2 : parameter interaction: -connect set -> setting -listen=0\n");
    }

    if (mapArgs.count("-proxy")) {
        // to protect privacy, do not listen by default if a default proxy server is specified
        if (SoftSetBoolArg("-listen", false))
            LogPrintf("AppInit2 : parameter interaction: -proxy set -> setting -listen=0\n");
        // to protect privacy, do not use UPNP when a proxy is set. The user may still specify -listen=1
        // to listen locally, so don't rely on this happening through -listen below.
        if (SoftSetBoolArg("-upnp", false))
            LogPrintf("AppInit2 : parameter interaction: -proxy set -> setting -upnp=0\n");
        // to protect privacy, do not discover addresses by default
        if (SoftSetBoolArg("-discover", false))
            LogPrintf("AppInit2 : parameter interaction: -proxy set -> setting -discover=0\n");
        if (SoftSetBoolArg("-listenonion", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -listenonion=0\n", __func__);
    }

    if (!GetBoolArg("-listen", true)) {
        // do not map ports or try to retrieve public IP when not listening (pointless)
        if (SoftSetBoolArg("-upnp", false))
            LogPrintf("AppInit2 : parameter interaction: -listen=0 -> setting -upnp=0\n");
        if (SoftSetBoolArg("-discover", false))
            LogPrintf("AppInit2 : parameter interaction: -listen=0 -> setting -discover=0\n");
    }

    if (mapArgs.count("-externalip")) {
        // if an explicit public IP is specified, do not try to find others
        if (SoftSetBoolArg("-discover", false))
            LogPrintf("AppInit2 : parameter interaction: -externalip set -> setting -discover=0\n");
    }

    if (GetBoolArg("-salvagewallet", false)) {
        // Rewrite just private keys: rescan to find transactions
        if (SoftSetBoolArg("-rescan", true))
            LogPrintf("AppInit2 : parameter interaction: -salvagewallet=1 -> setting -rescan=1\n");
    }

    // Make sure enough file descriptors are available
    int nBind = std::max((int)mapArgs.count("-bind") + (int)mapArgs.count("-whitebind"), 1);
    int nUserMaxConnections = GetArg("-maxconnections", 125);
    nMaxConnections = std::max(nUserMaxConnections, 0);
    int nUserWhiteConnections = GetArg("-whiteconnections", 0);
    nWhiteConnections = std::max(nUserWhiteConnections, 0);

    if ((mapArgs.count("-whitelist")) || (mapArgs.count("-whitebind"))) {
        if (!(mapArgs.count("-maxconnections"))) {
            // User is using whitelist feature,
            // but did not specify -maxconnections parameter.
            // Silently increase the default to compensate,
            // so that the whitelist connection reservation feature
            // does not inadvertently reduce the default
            // inbound connection capacity of the network.
            nMaxConnections += nWhiteConnections;
        }
    } else {
        // User not using whitelist feature.
        // Silently disable connection reservation,
        // for the same reason as above.
        nWhiteConnections = 0;
    }

    // Trim requested connection counts, to fit into system limitations
    nMaxConnections = std::max(std::min(nMaxConnections, (int)(FD_SETSIZE - nBind - MIN_CORE_FILEDESCRIPTORS)), 0);
    int nFD = RaiseFileDescriptorLimit(nMaxConnections + MIN_CORE_FILEDESCRIPTORS);
    if (nFD < MIN_CORE_FILEDESCRIPTORS)
        return InitError(_("Not enough file descriptors available."));
    nMaxConnections = std::min(nFD - MIN_CORE_FILEDESCRIPTORS, nMaxConnections);

    if (nMaxConnections < nUserMaxConnections)
        InitWarning(strprintf(_("Reducing -maxconnections from %d to %d, because of system limitations."), nUserMaxConnections, nMaxConnections));

    // Connection capacity is prioritized in this order:
    // outbound connections (hardcoded to 8),
    // then whitelisted connections,
    // then non-whitelisted connections get whatever's left (if any).
    if ((nWhiteConnections > 0) && (nWhiteConnections >= (nMaxConnections - 8)))
        InitWarning(strprintf(_("All non-whitelisted incoming connections will be dropped, because -whiteconnections is %d and -maxconnections is only %d."), nWhiteConnections, nMaxConnections));

    // ********************************************************* Step 3: parameter-to-internal-flags

    fDebug = !mapMultiArgs["-debug"].empty();
    // Special-case: if -debug=0/-nodebug is set, turn off debugging messages
    const vector<string>& categories = mapMultiArgs["-debug"];
    if (GetBoolArg("-nodebug", false) || find(categories.begin(), categories.end(), string("0")) != categories.end())
        fDebug = false;

    if(fDebug)
    {
	fDebugSmsg = true;
    } else
    {
        fDebugSmsg = GetBoolArg("-debugsmsg", false);
    }

    // Exit early if -masternode=1 and -listen=0
    if (GetBoolArg("-masternode", DEFAULT_MASTERNODE) && !GetBoolArg("-listen", DEFAULT_LISTEN))
        return InitError(strprintf(_("Error: %s must be true if %s is set."), "-listen", "-masternode"));

    // Check for -debugnet (deprecated)
    if (GetBoolArg("-debugnet", false))
        InitWarning(_("Warning: Deprecated argument -debugnet ignored, use -debug=net"));
    // Check for -tor - as this is a privacy risk to continue, exit here
    if (GetBoolArg("-tor", false))
        return InitError(_("Error: Unsupported argument -tor found, use -onion."));
    // Check for -socks - as this is a privacy risk to continue, exit here
    if (mapArgs.count("-socks"))
        return InitError(_("Error: Unsupported argument -socks found. Setting SOCKS version isn't possible anymore, only SOCKS5 proxies are supported."));
    if (fDaemon)
        fServer = true;
    else
    	fServer = GetBoolArg("-server", false);
    if (!fHaveGUI) 
       fServer = true;
    fPrintToConsole = GetBoolArg("-printtoconsole", false);
    fLogTimestamps = GetBoolArg("-logtimestamps", true);
    fLogIPs = GetBoolArg("-logips", false);
#ifdef ENABLE_WALLET
    bool fDisableWallet = GetBoolArg("-disablewallet", false);
#endif

    nConnectTimeout = GetArg("-timeout", DEFAULT_CONNECT_TIMEOUT);
    if (nConnectTimeout <= 0)
        nConnectTimeout = DEFAULT_CONNECT_TIMEOUT;

#ifdef ENABLE_WALLET
    if (mapArgs.count("-paytxfee"))
    {
        if (!ParseMoney(mapArgs["-paytxfee"], nTransactionFee))
            return InitError(strprintf(_("Invalid amount for -paytxfee=<amount>: '%s'"), mapArgs["-paytxfee"]));
        if (nTransactionFee > 0.25 * COIN)
            InitWarning(_("Warning: -paytxfee is set very high! This is the transaction fee you will pay if you send a transaction."));
    }
#endif

    fConfChange = GetBoolArg("-confchange", false);

#ifdef ENABLE_WALLET
    if (mapArgs.count("-mininput"))
    {
        if (!ParseMoney(mapArgs["-mininput"], nMinimumInputValue))
            return InitError(strprintf(_("Invalid amount for -mininput=<amount>: '%s'"), mapArgs["-mininput"]));
    }
#endif

    // ********************************************************* Step 4: application initialization: dir lock, daemonize, pidfile, debug log

    // Initialize elliptic curve code
    ECC_Start();
    globalVerifyHandle.reset(new ECCVerifyHandle());

    // Sanity check
    if (!InitSanityCheck())
        return InitError(_("Initialization sanity check failed. Piratecash is shutting down."));

    std::string strDataDir = GetDataDir().string();
#ifdef ENABLE_WALLET
    std::string strWalletFileName = GetArg("-wallet", "wallet.dat");

    // strWalletFileName must be a plain filename without a directory
    if (strWalletFileName != boost::filesystem::basename(strWalletFileName) + boost::filesystem::extension(strWalletFileName))
        return InitError(strprintf(_("Wallet %s resides outside data directory %s."), strWalletFileName, strDataDir));
#endif
    // Make sure only a single Bitcoin process is using the data directory.
    boost::filesystem::path pathLockFile = GetDataDir() / ".lock";
    FILE* file = fopen(pathLockFile.string().c_str(), "a"); // empty lock file; created if it doesn't exist.
    if (file) fclose(file);
    static boost::interprocess::file_lock lock(pathLockFile.string().c_str());
    if (!lock.try_lock())
        return InitError(strprintf(_("Cannot obtain a lock on data directory %s. Piratecash is probably already running."), strDataDir));
#ifndef WIN32
    CreatePidFile(GetPidFile(), getpid());
#endif
    if (GetBoolArg("-shrinkdebugfile", !fDebug))
        ShrinkDebugFile();
    LogPrintf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
    LogPrintf("Piratecash version %s (%s)\n", FormatFullVersion(), CLIENT_DATE);
    LogPrintf("Using OpenSSL version %s\n", SSLeay_version(SSLEAY_VERSION));
    if (!fLogTimestamps)
        LogPrintf("Startup time: %s\n", DateTimeStrFormat("%x %H:%M:%S", GetTime()));
    LogPrintf("Default data directory %s\n", GetDefaultDataDir().string());
    LogPrintf("Used data directory %s\n", strDataDir);
    if (nWhiteConnections > 0)
        LogPrintf("Reserving %i of these connections for whitelisted inbound peers\n", nWhiteConnections);
    std::ostringstream strErrors;

    // Start the lightweight task scheduler thread
    CScheduler::Function serviceLoop = boost::bind(&CScheduler::serviceQueue, &scheduler);
    threadGroup.create_thread(boost::bind(&TraceThread<CScheduler::Function>, "scheduler", serviceLoop));

    if (mapArgs.count("-masternodepaymentskey")) // masternode payments priv key
    {
        if (!masternodePayments.SetPrivKey(GetArg("-masternodepaymentskey", "")))
            return InitError(_("Unable to sign masternode payment winner, wrong key?"));
        if (!sporkManager.SetPrivKey(GetArg("-masternodepaymentskey", "")))
            return InitError(_("Unable to sign spork message, wrong key?"));
    }

    //ignore masternodes below protocol version
    nMasternodeMinProtocol = GetArg("-masternodeminprotocol", masternodePayments.GetMinMasternodePaymentsProto());

    if (fDaemon)
        fprintf(stdout, "Piratecash server starting\n"); 

    int64_t nStart;

    // ********************************************************* Step 5: Backup wallet and verify wallet database integrity
#ifdef ENABLE_WALLET
    if (!fDisableWallet) {

        filesystem::path backupDir = GetDataDir() / "backups";
        if (!filesystem::exists(backupDir))
        {
            // Always create backup folder to not confuse the operating system's file browser
            filesystem::create_directories(backupDir);
        }
        nWalletBackups = GetArg("-createwalletbackups", 10);
        nWalletBackups = std::max(0, std::min(10, nWalletBackups));
        if(nWalletBackups > 0)
        {
            if (filesystem::exists(backupDir))
            {
                // Create backup of the wallet
                std::string dateTimeStr = DateTimeStrFormat(".%Y-%m-%d-%H.%M", GetTime());
                std::string backupPathStr = backupDir.string();
                backupPathStr += "/" + strWalletFileName;
                std::string sourcePathStr = GetDataDir().string();
                sourcePathStr += "/" + strWalletFileName;
                boost::filesystem::path sourceFile = sourcePathStr;
                boost::filesystem::path backupFile = backupPathStr + dateTimeStr;
                sourceFile.make_preferred();
                backupFile.make_preferred();
                try {
                    boost::filesystem::copy_file(sourceFile, backupFile);
                    LogPrintf("Creating backup of %s -> %s\n", sourceFile, backupFile);
                } catch(boost::filesystem::filesystem_error &error) {
                    LogPrintf("Failed to create backup %s\n", error.what());
                }
                // Keep only the last 10 backups, including the new one of course
                typedef std::multimap<std::time_t, boost::filesystem::path> folder_set_t;
                folder_set_t folder_set;
                boost::filesystem::directory_iterator end_iter;
                boost::filesystem::path backupFolder = backupDir.string();
                backupFolder.make_preferred();
                // Build map of backup files for current(!) wallet sorted by last write time
                boost::filesystem::path currentFile;
                for (boost::filesystem::directory_iterator dir_iter(backupFolder); dir_iter != end_iter; ++dir_iter)
                {
                    // Only check regular files
                    if ( boost::filesystem::is_regular_file(dir_iter->status()))
                    {
                        currentFile = dir_iter->path().filename();
                        // Only add the backups for the current wallet, e.g. wallet.dat.*
                        if(currentFile.string().find(strWalletFileName) != string::npos)
                        {
                            folder_set.insert(folder_set_t::value_type(boost::filesystem::last_write_time(dir_iter->path()), *dir_iter));
                        }
                    }
                }
                // Loop backward through backup files and keep the N newest ones (1 <= N <= 10)
                int counter = 0;
                BOOST_REVERSE_FOREACH(PAIRTYPE(const std::time_t, boost::filesystem::path) file, folder_set)
                {
                    counter++;
                    if (counter > nWalletBackups)
                    {
                        // More than nWalletBackups backups: delete oldest one(s)
                        try {
                            boost::filesystem::remove(file.second);
                            LogPrintf("Old backup deleted: %s\n", file.second);
                        } catch(boost::filesystem::filesystem_error &error) {
                            LogPrintf("Failed to delete backup %s\n", error.what());
                        }
                    }
                }
            }
        }


        uiInterface.InitMessage(_("Verifying database integrity..."));

        if (!bitdb.Open(GetDataDir()))
        {
            // try moving the database env out of the way
            boost::filesystem::path pathDatabase = GetDataDir() / "database";
            boost::filesystem::path pathDatabaseBak = GetDataDir() / strprintf("database.%d.bak", GetTime());
            try {
                boost::filesystem::rename(pathDatabase, pathDatabaseBak);
                LogPrintf("Moved old %s to %s. Retrying.\n", pathDatabase.string(), pathDatabaseBak.string());
            } catch(boost::filesystem::filesystem_error &error) {
                 // failure is ok (well, not really, but it's not worse than what we started with)
            }

            // try again
            if (!bitdb.Open(GetDataDir())) {
                // if it still fails, it probably means we can't even create the database env
                string msg = strprintf(_("Error initializing wallet database environment %s!"), strDataDir);
                return InitError(msg);
            }
        }

        int nBind = std::max((int)mapArgs.count("-bind") + (int)mapArgs.count("-whitebind"), 1);

        if (GetBoolArg("-salvagewallet", false))
        {
            // Recover readable keypairs:
            if (!CWalletDB::Recover(bitdb, strWalletFileName, true))
                return false;
        }

        if (filesystem::exists(GetDataDir() / strWalletFileName))
        {
            CDBEnv::VerifyResult r = bitdb.Verify(strWalletFileName, CWalletDB::Recover);
            if (r == CDBEnv::RECOVER_OK)
            {
                string msg = strprintf(_("Warning: wallet.dat corrupt, data salvaged!"
                                         " Original wallet.dat saved as wallet.{timestamp}.bak in %s; if"
                                         " your balance or transactions are incorrect you should"
                                         " restore from a backup."), strDataDir);
                InitWarning(msg);
            }
            if (r == CDBEnv::RECOVER_FAIL)
                return InitError(_("wallet.dat corrupt, salvage failed"));
        }

    } // (!fDisableWallet)
#endif // ENABLE_WALLET
    // ********************************************************* Step 6: network initialization

    RegisterNodeSignals(GetNodeSignals());

    // format user agent, check total size
    strSubVersion = FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, mapMultiArgs.count("-uacomment") ? mapMultiArgs["-uacomment"] : std::vector<string>());
    if (strSubVersion.size() > MAX_SUBVERSION_LENGTH) {
        return InitError(strprintf("Total length of network version string %i exceeds maximum of %i characters. Reduce the number and/or size of uacomments.",
            strSubVersion.size(), MAX_SUBVERSION_LENGTH));
    }
    
    if (mapArgs.count("-onlynet")) {
        std::set<enum Network> nets;
        BOOST_FOREACH(std::string snet, mapMultiArgs["-onlynet"]) {
            enum Network net = ParseNetwork(snet);
	    if(net == NET_TOR)
		fOnlyTor = true;

            if (net == NET_UNROUTABLE)
                return InitError(strprintf(_("Unknown network specified in -onlynet: '%s'"), snet));
            nets.insert(net);
        }
        for (int n = 0; n < NET_MAX; n++) {
            enum Network net = (enum Network)n;
            if (!nets.count(net))
                SetLimited(net);
        }
    } else {
        SetReachable(NET_IPV4);
        SetReachable(NET_IPV6);
    }

    if (mapArgs.count("-whitelist")) {
        BOOST_FOREACH(const std::string& net, mapMultiArgs["-whitelist"]) {
            CSubNet subnet(net);
            if (!subnet.IsValid())
                return InitError(strprintf(_("Invalid netmask specified in -whitelist: '%s'"), net));
            CNode::AddWhitelistedRange(subnet);
        }
    }

    CService addrProxy;
    bool fProxy = false;
    if (mapArgs.count("-proxy")) {
        addrProxy = CService(mapArgs["-proxy"], 9050);
        if (!addrProxy.IsValid())
            return InitError(strprintf(_("Invalid -proxy address: '%s'"), mapArgs["-proxy"]));

        if (!IsLimited(NET_IPV4))
            SetProxy(NET_IPV4, addrProxy);
        if (!IsLimited(NET_IPV6))
            SetProxy(NET_IPV6, addrProxy);
        SetNameProxy(addrProxy);
        fProxy = true;
    }

    // -onion can override normal proxy, -noonion disables tor entirely
    if (!(mapArgs.count("-onion") && mapArgs["-onion"] == "0") &&
            (fProxy || mapArgs.count("-onion"))) {
        CService addrOnion;
        if (!mapArgs.count("-onion"))
            addrOnion = addrProxy;
        else
            addrOnion = CService(mapArgs["-onion"], 9050);
        if (!addrOnion.IsValid())
            return InitError(strprintf(_("Invalid -onion address: '%s'"), mapArgs["-onion"]));
        SetProxy(NET_TOR, addrOnion);
        SetReachable(NET_TOR);
    }

    // see Step 2: parameter interactions for more information about these
    fListen = GetBoolArg("-listen", true);
    fDiscover = GetBoolArg("-discover", true);
    fNameLookup = GetBoolArg("-dns", true);

    bool fBound = false;
    if (fListen)
    {
        if (mapArgs.count("-bind") || mapArgs.count("-whitebind")) {
            BOOST_FOREACH(std::string strBind, mapMultiArgs["-bind"]) {
                CService addrBind;
                if (!Lookup(strBind.c_str(), addrBind, GetListenPort(), false))
                    return InitError(strprintf(_("Cannot resolve -bind address: '%s'"), strBind));
                fBound |= Bind(addrBind, (BF_EXPLICIT | BF_REPORT_ERROR));
            }
            BOOST_FOREACH(std::string strBind, mapMultiArgs["-whitebind"]) {
                CService addrBind;
                if (!Lookup(strBind.c_str(), addrBind, 0, false))
                    return InitError(strprintf(_("Cannot resolve -whitebind address: '%s'"), strBind));
                if (addrBind.GetPort() == 0)
                    return InitError(strprintf(_("Need to specify a port with -whitebind: '%s'"), strBind));
                fBound |= Bind(addrBind, (BF_EXPLICIT | BF_REPORT_ERROR | BF_WHITELIST));
            }
        }
        else {
            struct in_addr inaddr_any;
            inaddr_any.s_addr = INADDR_ANY;
            if (!IsLimited(NET_IPV6))
                fBound |= Bind(CService(in6addr_any, GetListenPort()), BF_NONE);
            if (!IsLimited(NET_IPV4))
                fBound |= Bind(CService(inaddr_any, GetListenPort()), !fBound ? BF_REPORT_ERROR : BF_NONE);
        }
        if (!fBound)
            return InitError(_("Failed to listen on any port. Use -listen=0 if you want this."));
    }

    if (mapArgs.count("-externalip")) {
        BOOST_FOREACH(string strAddr, mapMultiArgs["-externalip"]) {
            CService addrLocal(strAddr, GetListenPort(), fNameLookup);
            if (!addrLocal.IsValid())
                return InitError(strprintf(_("Cannot resolve -externalip address: '%s'"), strAddr));
            AddLocal(CService(strAddr, GetListenPort(), fNameLookup), LOCAL_MANUAL);
        }
    }

#ifdef ENABLE_WALLET
    if (mapArgs.count("-reservebalance")) // ppcoin: reserve balance amount
    {
        if (!ParseMoney(mapArgs["-reservebalance"], nReserveBalance))
        {
            InitError(_("Invalid amount for -reservebalance=<amount>"));
            return false;
        }
    }
#endif

    BOOST_FOREACH(string strDest, mapMultiArgs["-seednode"])
        AddOneShot(strDest);

    // ********************************************************* Step 7: load blockchain

    if (GetBoolArg("-loadblockindextest", false))
    {
        CTxDB txdb("r");
        txdb.LoadBlockIndex();
        PrintBlockTree();
        return false;
    }

    uiInterface.InitMessage(_("Loading block index..."));

    nStart = GetTimeMillis();
    if (!LoadBlockIndex())
        return InitError(_("Error loading block database"));


    // as LoadBlockIndex can take several minutes, it's possible the user
    // requested to kill bitcoin-qt during the last operation. If so, exit.
    // As the program has not fully started yet, Shutdown() is possibly overkill.
    if (fRequestShutdown)
    {
        LogPrintf("Shutdown requested. Exiting.\n");
        return false;
    }
    LogPrintf(" block index %15dms\n", GetTimeMillis() - nStart);

    if (GetBoolArg("-printblockindex", false) || GetBoolArg("-printblocktree", false))
    {
        PrintBlockTree();
        return false;
    }

    if (mapArgs.count("-printblock"))
    {
        string strMatch = mapArgs["-printblock"];
        int nFound = 0;
        for (BlockMap::iterator mi = mapBlockIndex.begin(); mi != mapBlockIndex.end(); ++mi)
        {
            uint256 hash = (*mi).first;
            if (strncmp(hash.ToString().c_str(), strMatch.c_str(), strMatch.size()) == 0)
            {
                CBlockIndex* pindex = (*mi).second;
                CBlock block;
                block.ReadFromDisk(pindex);
                block.BuildMerkleTree();
                LogPrintf("%s\n", block.ToString());
                nFound++;
            }
        }
        if (nFound == 0)
            LogPrintf("No blocks matching %s were found\n", strMatch);
        return false;
    }

    fFeeEstimatesInitialized = true;

    // ********************************************************* Step 8: load wallet
#ifdef ENABLE_WALLET
    if (fDisableWallet) {
        pwalletMain = NULL;
        LogPrintf("Wallet disabled!\n");
    } else {
        uiInterface.InitMessage(_("Loading wallet..."));

        nStart = GetTimeMillis();
        bool fFirstRun = true;
        pwalletMain = new CWallet(strWalletFileName);
        DBErrors nLoadWalletRet = pwalletMain->LoadWallet(fFirstRun);
        if (nLoadWalletRet != DB_LOAD_OK)
        {
            if (nLoadWalletRet == DB_CORRUPT)
                strErrors << _("Error loading wallet.dat: Wallet corrupted") << "\n";
            else if (nLoadWalletRet == DB_NONCRITICAL_ERROR)
            {
                string msg(_("Warning: error reading wallet.dat! All keys read correctly, but transaction data"
                             " or address book entries might be missing or incorrect."));
                InitWarning(msg);
            }
            else if (nLoadWalletRet == DB_TOO_NEW)
                strErrors << _("Error loading wallet.dat: Wallet requires newer version of Piratecash") << "\n";
            else if (nLoadWalletRet == DB_NEED_REWRITE)
            {
                strErrors << _("Wallet needed to be rewritten: restart Piratecash to complete") << "\n";
                LogPrintf("%s", strErrors.str());
                return InitError(strErrors.str());
            }
            else
                strErrors << _("Error loading wallet.dat") << "\n";
        }

        if (GetBoolArg("-upgradewallet", fFirstRun))
        {
            int nMaxVersion = GetArg("-upgradewallet", 0);
            if (nMaxVersion == 0) // the -upgradewallet without argument case
            {
                LogPrintf("Performing wallet upgrade to %i\n", FEATURE_LATEST);
                nMaxVersion = CLIENT_VERSION;
                pwalletMain->SetMinVersion(FEATURE_LATEST); // permanently upgrade the wallet immediately
            }
            else
                LogPrintf("Allowing wallet upgrade up to %i\n", nMaxVersion);
            if (nMaxVersion < pwalletMain->GetVersion())
                strErrors << _("Cannot downgrade wallet") << "\n";
            pwalletMain->SetMaxVersion(nMaxVersion);
        }

        if (fFirstRun)
        {
            // Create new keyUser and set as default key
            RandAddSeedPerfmon();

            CPubKey newDefaultKey;
            if (pwalletMain->GetKeyFromPool(newDefaultKey)) {
                pwalletMain->SetDefaultKey(newDefaultKey);
                if (!pwalletMain->SetAddressBookName(pwalletMain->vchDefaultKey.GetID(), ""))
                    strErrors << _("Cannot write default address") << "\n";
            }

            pwalletMain->SetBestChain(CBlockLocator(pindexBest));
        }

        LogPrintf("%s", strErrors.str());
        LogPrintf(" wallet      %15dms\n", GetTimeMillis() - nStart);

        RegisterWallet(pwalletMain);

        CBlockIndex *pindexRescan = pindexBest;
        if (GetBoolArg("-rescan", false))
            pindexRescan = pindexGenesisBlock;
        else
        {
            CWalletDB walletdb(strWalletFileName);
            CBlockLocator locator;
            if (walletdb.ReadBestBlock(locator))
                pindexRescan = locator.GetBlockIndex();
            else
                pindexRescan = pindexGenesisBlock;
        }
        if (pindexBest != pindexRescan && pindexBest && pindexRescan && pindexBest->nHeight > pindexRescan->nHeight)
        {
            uiInterface.InitMessage(_("Rescanning..."));
            LogPrintf("Rescanning last %i blocks (from block %i)...\n", pindexBest->nHeight - pindexRescan->nHeight, pindexRescan->nHeight);
            nStart = GetTimeMillis();
            pwalletMain->ScanForWalletTransactions(pindexRescan, true);
            LogPrintf(" rescan      %15dms\n", GetTimeMillis() - nStart);
            pwalletMain->SetBestChain(CBlockLocator(pindexBest));
            nWalletDBUpdated++;
        }
    } // (!fDisableWallet)
#else // ENABLE_WALLET
    LogPrintf("No wallet compiled in!\n");
#endif // !ENABLE_WALLET
    // ********************************************************* Step 9: import blocks

    std::vector<boost::filesystem::path> vImportFiles;
    if (mapArgs.count("-loadblock"))
    {
        BOOST_FOREACH(string strFile, mapMultiArgs["-loadblock"])
            vImportFiles.push_back(strFile);
    }
    threadGroup.create_thread(boost::bind(&ThreadImport, vImportFiles));

    // ********************************************************* Step 10: start node

    if (!CheckDiskSpace())
        return false;

    if (!strErrors.str().empty())
        return InitError(strErrors.str());

    uiInterface.InitMessage(_("Loading masternode cache..."));

    CMasternodeDB mndb;
    CMasternodeDB::ReadResult readResult = mndb.Read(mnodeman);
    if (readResult == CMasternodeDB::FileError)
        LogPrintf("Missing masternode cache file - mncache.dat, will try to recreate\n");
    else if (readResult != CMasternodeDB::Ok)
    {
        LogPrintf("Error reading mncache.dat: ");
        if(readResult == CMasternodeDB::IncorrectFormat)
            LogPrintf("magic is ok but data has invalid format, will try to recreate\n");
        else
            LogPrintf("file format is unknown or invalid, please fix it manually\n");
    }else
        mnodeman.CheckAndRemove(); // clean out expired

    LogPrintf("Loaded info from masternodes.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrintf("  %s\n", mnodeman.ToString());


    fMasterNode = GetBoolArg("-masternode", false);
    if(fMasterNode) {
        LogPrintf("IS DARKSEND MASTER NODE\n");
        strMasterNodeAddr = GetArg("-masternodeaddr", "");

        if (strMasterNodeAddr.empty()) {
               return InitError("ERROR: Empty masternodeaddr");
           }

        LogPrintf(" addr %s\n", strMasterNodeAddr.c_str());

        if(!strMasterNodeAddr.empty()){
            CService addrTest = CService(strMasterNodeAddr, fNameLookup);
            if (!addrTest.IsValid()) {
                return InitError("Invalid -masternodeaddr address: " + strMasterNodeAddr);
            }
        }

        // Address parsing.
        const CChainParams& params = Params();
        int nPort = 0;
        int nDefaultPort = params.GetDefaultPort();
        std::string strHost;
        SplitHostPort(strMasterNodeAddr, nPort, strHost);

        // Allow for the port number to be omitted here and just double check
        // that if a port is supplied, it matches the required default port.
        if (nPort == 0) nPort = nDefaultPort;
        if (nPort != nDefaultPort) {
            return InitError(strprintf(_("Invalid -masternodeaddr port %d, only %d is supported."),nPort, nDefaultPort));
        }

        // Peer port needs to match the masternode public one.
        if (nPort != GetListenPort()) {
            return InitError(strprintf(_("Invalid -masternodeaddr port %d, isn't the same as the peer port %d"),
                                          nPort, GetListenPort()));
            }

        strMasterNodePrivKey = GetArg("-masternodeprivkey", "");
        if(!strMasterNodePrivKey.empty()){
            std::string errorMessage;

            CKey key;
            CPubKey pubkey;

            if(!darkSendSigner.SetKey(strMasterNodePrivKey, errorMessage, key, pubkey))
            {
                return InitError(_("Invalid masternodeprivkey. Please see documenation."));
            }

            activeMasternode.pubKeyMasternode = pubkey;

        } else {
            return InitError(_("You must specify a masternodeprivkey in the configuration. Please see documentation for help."));
        }

        activeMasternode.ManageStatus();
    }

    if(GetBoolArg("-mnconflock", false)) {
        LogPrintf("Locking Masternodes:\n");
        uint256 mnTxHash;
        BOOST_FOREACH(CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
            LogPrintf("  %s %s\n", mne.getTxHash(), mne.getOutputIndex());
            mnTxHash.SetHex(mne.getTxHash());
            COutPoint outpoint = COutPoint(mnTxHash, boost::lexical_cast<unsigned int>(mne.getOutputIndex()));
            pwalletMain->LockCoin(outpoint);
        }
    }

    inputStakeProtect = GetBoolArg("-inputstakeprotect", true);
    fEnableDarksend = GetBoolArg("-enabledarksend", false);

    nDarksendRounds = GetArg("-darksendrounds", 2);
    if(nDarksendRounds > 16) nDarksendRounds = 16;
    if(nDarksendRounds < 1) nDarksendRounds = 1;

    nLiquidityProvider = GetArg("-liquidityprovider", 0); //0-100
    if(nLiquidityProvider != 0) {
        darkSendPool.SetMinBlockSpacing(std::min(nLiquidityProvider,100)*15);
        fEnableDarksend = true;
        nDarksendRounds = 99999;
    }

    nAnonymizeDarkcoinAmount = GetArg("-anonymizepiratecashamount", 0);
    if(nAnonymizeDarkcoinAmount > 999999) nAnonymizeDarkcoinAmount = 999999;
    if(nAnonymizeDarkcoinAmount < 2) nAnonymizeDarkcoinAmount = 2;

    fEnableInstantX = GetBoolArg("-enableinstantx", fEnableInstantX);
    nInstantXDepth = GetArg("-instantxdepth", nInstantXDepth);
    nInstantXDepth = std::min(std::max(nInstantXDepth, 0), 60);

    //lite mode disables all Masternode and Darksend related functionality
    fLiteMode = GetBoolArg("-litemode", false);
    if(fMasterNode && fLiteMode){
        return InitError("You can not start a masternode in litemode");
    }

    LogPrintf("fLiteMode %d\n", fLiteMode);
    LogPrintf("nInstantXDepth %d\n", nInstantXDepth);
    LogPrintf("Darksend rounds %d\n", nDarksendRounds);
    LogPrintf("Anonymize Piratecash Amount %d\n", nAnonymizeDarkcoinAmount);

    /* Denominations
       A note about convertability. Within Darksend pools, each denomination
       is convertable to another.
       For example:
       1PIRATE+1000 == (.1PIRATE+100)*10
       10PIRATE+10000 == (1PIRATE+1000)*10
    */
    darkSendDenominations.push_back( (1000        * COIN)+1000000 );
    darkSendDenominations.push_back( (100         * COIN)+100000 );
    darkSendDenominations.push_back( (10          * COIN)+10000 );
    darkSendDenominations.push_back( (1           * COIN)+1000 );
    darkSendDenominations.push_back( (.1          * COIN)+100 );
    /* Disabled till we need them
    darkSendDenominations.push_back( (.01      * COIN)+10 );
    darkSendDenominations.push_back( (.001     * COIN)+1 );
    */

    darkSendPool.InitCollateralAddress();

    threadGroup.create_thread(boost::bind(&ThreadCheckDarkSendPool));



    RandAddSeedPerfmon();

    // reindex addresses found in blockchain
    if(GetBoolArg("-reindexaddr", false))
    {
        uiInterface.InitMessage(_("Rebuilding address index..."));
        CBlockIndex *pblockAddrIndex = pindexBest;
	CTxDB txdbAddr("rw");
	while(pblockAddrIndex)
	{
	    uiInterface.InitMessage(strprintf("Rebuilding address index, block %i", pblockAddrIndex->nHeight));
	    bool ReadFromDisk(const CBlockIndex* pindex, bool fReadTransactions=true);
	    CBlock pblockAddr;
	    if(pblockAddr.ReadFromDisk(pblockAddrIndex, true))
	        pblockAddr.RebuildAddressIndex(txdbAddr);
	    pblockAddrIndex = pblockAddrIndex->pprev;
	}
    }

    //// debug print
    LogPrintf("mapBlockIndex.size() = %u\n",   mapBlockIndex.size());
    LogPrintf("nBestHeight = %d\n",                   nBestHeight);
#ifdef ENABLE_WALLET
    LogPrintf("setKeyPool.size() = %u\n",      pwalletMain ? pwalletMain->setKeyPool.size() : 0);
    LogPrintf("mapWallet.size() = %u\n",       pwalletMain ? pwalletMain->mapWallet.size() : 0);
    LogPrintf("mapAddressBook.size() = %u\n",  pwalletMain ? pwalletMain->mapAddressBook.size() : 0);
#endif

    if (GetBoolArg("-listenonion", DEFAULT_LISTEN_ONION))
        StartTorControl(threadGroup, scheduler);

    StartNode(threadGroup);
#ifdef ENABLE_WALLET
    // InitRPCMining is needed here so getwork/getblocktemplate in the GUI debug console works properly.
    InitRPCMining();
#endif
    if (fServer)
        RPCServer::OnStopped(&OnRPCStopped);
        RPCServer::OnPreCommand(&OnRPCPreCommand);
        StartRPCThreads();

#ifdef ENABLE_WALLET
    // Mine proof-of-stake blocks in the background
    if (!GetBoolArg("-staking", true))
        LogPrintf("Staking disabled\n");
    else if (pwalletMain)
        threadGroup.create_thread(boost::bind(&ThreadStakeMiner, pwalletMain));
#endif

    // ********************************************************* Step 11: finished

    uiInterface.InitMessage(_("Done loading"));

#ifdef ENABLE_WALLET
    if (pwalletMain) {
        // Add wallet transactions that aren't already in a block to mapTransactions
        pwalletMain->ReacceptWalletTransactions();

        // Run a thread to flush wallet periodically
        threadGroup.create_thread(boost::bind(&ThreadFlushWalletDB, boost::ref(pwalletMain->strWalletFile)));
    }
#endif

    return !fRequestShutdown;
}

// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/piratecash-config.h>
#endif

#include <chainparams.h>
#include <clientversion.h>
#include <compat.h>
#include <fs.h>
#include <interfaces/chain.h>
#include <rpc/server.h>
#include <init.h>
#include <noui.h>
#include <shutdown.h>
#include <ui_interface.h>
#include <util/system.h>
#include <httpserver.h>
#include <httprpc.h>
#include <util/strencodings.h>
#include <util/threadnames.h>
#include <util/translation.h>
#include <stacktraces.h>

#include <functional>
#include <stdio.h>

const std::function<std::string(const char*)> G_TRANSLATION_FUN = nullptr;

/* Introduction text for doxygen: */

/*! \mainpage Developer documentation
 *
 * \section intro_sec Introduction
 *
 * This is the developer documentation of the reference client for an experimental new digital currency called PirateCash (https://www.dash.org/),
 * which enables instant payments to anyone, anywhere in the world. PirateCash uses peer-to-peer technology to operate
 * with no central authority: managing transactions and issuing money are carried out collectively by the network.
 *
 * The software is a community-driven open source project, released under the MIT license.
 *
 * \section Navigation
 * Use the buttons <code>Namespaces</code>, <code>Classes</code> or <code>Files</code> at the top of the page to start navigating the code.
 */

static void WaitForShutdown()
{
    while (!ShutdownRequested())
    {
        UninterruptibleSleep(std::chrono::milliseconds{200});
    }
    Interrupt();
}

//////////////////////////////////////////////////////////////////////////////
//
// Start
//
static bool AppInit(int argc, char* argv[])
{
    InitInterfaces interfaces;
    interfaces.chain = interfaces::MakeChain();

    bool fRet = false;

    util::ThreadSetInternalName("init");

    // If Qt is used, parameters/dash.conf are parsed in qt/bitcoin.cpp's main()
    SetupServerArgs();
    std::string error;
    if (!gArgs.ParseParameters(argc, argv, error)) {
        return InitError(Untranslated(strprintf("Error parsing command line arguments: %s\n", error)));
    }

    if (gArgs.IsArgSet("-printcrashinfo")) {
        std::cout << GetCrashInfoStrFromSerializedStr(gArgs.GetArg("-printcrashinfo", "")) << std::endl;
        return true;
    }

    // Process help and version before taking care about datadir
    if (HelpRequested(gArgs) || gArgs.IsArgSet("-version")) {
        std::string strUsage = PACKAGE_NAME " Daemon version " + FormatFullVersion() + "\n";

        if (gArgs.IsArgSet("-version"))
        {
            strUsage += FormatParagraph(LicenseInfo()) + "\n";
        }
        else
        {
            strUsage += "\nUsage:  piratecashd [options]                     Start " PACKAGE_NAME " Daemon\n";
            strUsage += "\n" + gArgs.GetHelpMessage();
        }

        tfm::format(std::cout, "%s", strUsage);
        return true;
    }

    try
    {
        bool datadirFromCmdLine = gArgs.IsArgSet("-datadir");
        if (datadirFromCmdLine && !fs::is_directory(GetDataDir(false)))
        {
            return InitError(Untranslated(strprintf("Specified data directory \"%s\" does not exist.\n", gArgs.GetArg("-datadir", ""))));
        }
        if (!gArgs.ReadConfigFiles(error, true)) {
            return InitError(Untranslated(strprintf("Error reading configuration file: %s\n", error)));
        }
        if (!datadirFromCmdLine && !fs::is_directory(GetDataDir(false)))
        {
            tfm::format(std::cerr, "Error: Specified data directory \"%s\" from config file does not exist.\n", gArgs.GetArg("-datadir", ""));
            return EXIT_FAILURE;
        }
        // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
        try {
            SelectParams(gArgs.GetChainName());
        } catch (const std::exception& e) {
            return InitError(Untranslated(strprintf("%s\n", e.what())));
        }

        // Error out when loose non-argument tokens are encountered on command line
        for (int i = 1; i < argc; i++) {
            if (!IsSwitchChar(argv[i][0])) {
                return InitError(Untranslated(strprintf("Command line contains unexpected token '%s', see piratecashd -h for a list of options.\n", argv[i])));
            }
        }

        // -server defaults to true for piratecashd but not for the GUI so do this here
        gArgs.SoftSetBoolArg("-server", true);
        // Set this early so that parameter interactions go to console
        InitLogging();
        InitParameterInteraction();
        if (!AppInitBasicSetup())
        {
            // InitError will have been called with detailed error, which ends up on console
            return false;
        }
        if (!AppInitParameterInteraction())
        {
            // InitError will have been called with detailed error, which ends up on console
            return false;
        }
        if (!AppInitSanityChecks())
        {
            // InitError will have been called with detailed error, which ends up on console
            return false;
        }
        if (gArgs.GetBoolArg("-daemon", false))
        {
#if HAVE_DECL_DAEMON
#if defined(MAC_OSX)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
            tfm::format(std::cout, PACKAGE_NAME "daemon starting\n");

            // Daemonize
            if (daemon(1, 0)) { // don't chdir (1), do close FDs (0)
                return InitError(Untranslated(strprintf("daemon() failed: %s\n", strerror(errno))));
            }
#if defined(MAC_OSX)
#pragma GCC diagnostic pop
#endif
#else
            return InitError(Untranslated("-daemon is not supported on this operating system\n"));
#endif // HAVE_DECL_DAEMON
        }
        // Lock data directory after daemonization
        if (!AppInitLockDataDirectory())
        {
            // If locking the data directory failed, exit immediately
            return false;
        }
        fRet = AppInitMain(interfaces);
    } catch (...) {
        PrintExceptionContinue(std::current_exception(), "AppInit()");
    }

    if (!fRet)
    {
        Interrupt();
    } else {
        WaitForShutdown();
    }
    Shutdown(interfaces);

    return fRet;
}

int main(int argc, char* argv[])
{
    RegisterPrettyTerminateHander();
    RegisterPrettySignalHandlers();

#ifdef WIN32
    util::WinCmdLineArgs winArgs;
    std::tie(argc, argv) = winArgs.get();
#endif
    SetupEnvironment();

    // Connect piratecashd signal handlers
    noui_connect();

    return (AppInit(argc, argv) ? EXIT_SUCCESS : EXIT_FAILURE);
}

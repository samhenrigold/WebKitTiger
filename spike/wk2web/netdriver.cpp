/*
 * Copyright (C) 2026 WebKitTiger.
 *
 * TIGER: the smallest thing that makes bin/TigerNetworkProcess actually fetch.
 *
 * It stands in for both of the processes the network process normally talks to,
 * and for nothing else. There is no WebPageProxy, no PageClient, no drawing area
 * and no web process: just the four messages, in order, over real IPC.
 *
 *   as the UI process, on the connection it creates by forking and exec'ing:
 *     1. NetworkProcess::InitializeNetworkProcess            [async reply, a barrier]
 *     2. NetworkProcess::AddWebsiteDataStore                 [creates the session]
 *     3. NetworkProcess::CreateNetworkConnectionToWebProcess [async reply; it carries
 *        an IPC::ConnectionHandle, i.e. a second socket over SCM_RIGHTS]
 *
 *   as the web process, on that second connection:
 *     4. NetworkConnectionToWebProcess::PerformSynchronousLoad
 *        -> (ResourceError, ResourceResponse, Vector<uint8_t>)
 *
 * Every parameter struct goes through the generated serializers, which is the whole
 * reason this is C++ linked against libWebKit.a rather than a C program: hand
 * encoding them would mean reimplementing the encoder and the generated code.
 *
 *   netdriver <TigerNetworkProcess> <url> [<url> ...]
 *
 * Exit status is 0 only if every URL came back with an empty ResourceError and an
 * HTTP status the caller asked for (see --expect-status).
 */

#include "config.h"

#include "Connection.h"
#include "IPCUtilities.h"
#include "NetworkConnectionToWebProcessMessages.h"
#include "NetworkProcessConnectionParameters.h"
#include "NetworkProcessCreationParameters.h"
#include "NetworkProcessMessages.h"
#include "NetworkResourceLoadParameters.h"
#include "WebKit2Initialize.h"
#include "WebsiteDataStoreParameters.h"

#include <WebCore/HTTPCookieAcceptPolicy.h>
#include <WebCore/RegistrableDomain.h>
#include <WebCore/ResourceLoaderIdentifier.h>
#include <WebCore/OrganizationStorageAccessPromptQuirk.h>
#include <WebCore/ResourceError.h>
#include <WebCore/ResourceRequest.h>
#include <WebCore/ResourceResponse.h>
#include <WebCore/SecurityOrigin.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <wtf/MainThread.h>
#include <wtf/RunLoop.h>
#include <wtf/text/CString.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/WTFString.h>

extern "C" char** environ;

namespace {

double monotonicMilliseconds()
{
    struct timeval now;
    gettimeofday(&now, nullptr);
    return now.tv_sec * 1000.0 + now.tv_usec / 1000.0;
}

long residentKilobytes(pid_t process)
{
    char command[128];
    snprintf(command, sizeof command, "ps -o rss= -p %d", static_cast<int>(process));
    FILE* pipe = popen(command, "r");
    if (!pipe)
        return -1;
    long kilobytes = -1;
    if (fscanf(pipe, "%ld", &kilobytes) != 1)
        kilobytes = -1;
    pclose(pipe);
    return kilobytes;
}

// A connection client that does nothing but notice. Neither connection here ever
// receives a message the driver cares about: the replies come back through their
// own completion handlers, and everything else the network process volunteers (cookie policy
// updates and the like) is addressed to receivers that do not exist in this
// process, which is what didReceiveInvalidMessage reports.
class SilentClient final : public IPC::Connection::Client {
public:
    void ref() const final { }
    void deref() const final { }

    void didClose(IPC::Connection&) final
    {
        fprintf(stderr, "netdriver: the network process closed the connection\n");
        m_closed = true;
    }

    void didReceiveInvalidMessage(IPC::Connection&, IPC::MessageName name, const Vector<uint32_t>&) final
    {
        fprintf(stderr, "netdriver: unhandled message %s\n", IPC::description(name).characters());
    }

    bool closed() const { return m_closed; }

private:
    bool m_closed { false };
};

// InitializeNetworkProcess and CreateNetworkConnectionToWebProcess are declared
// "-> ()" in NetworkProcess.messages.in, which is async-with-reply, not Synchronous
// -- only PerformSynchronousLoad is the latter. So the two handshake steps hand
// their answer to a completion handler on the main run loop, and the driver, having
// no run loop of its own to return to, spins one until the answer arrives.
void runUntil(bool& done)
{
    while (!done)
        RunLoop::cycle();
}

struct Fetch {
    String url;
    bool ok { false };
    int status { 0 };
    size_t bodySize { 0 };
    String mimeType;
    String errorDescription;
    double milliseconds { 0 };
    String bodyHead;
};

} // namespace

int main(int argc, char** argv)
{
    const char* processPath = nullptr;
    int expectedStatus = 200;
    Vector<String> urls;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--expect-status") && i + 1 < argc) {
            expectedStatus = atoi(argv[++i]);
            continue;
        }
        if (!processPath) {
            processPath = argv[i];
            continue;
        }
        urls.append(String::fromUTF8(argv[i]));
    }

    if (!processPath || urls.isEmpty()) {
        fprintf(stderr, "usage: netdriver [--expect-status N] <TigerNetworkProcess> <url> [<url> ...]\n");
        return 2;
    }

    WebKit::InitializeWebKit2();

    // --- The launcher half of ProcessLauncherTiger, by hand. ------------------
    // SOCK_DGRAM because that is what ConnectionUnix's SOCKET_TYPE is on Darwin,
    // and close-on-exec on the end we keep so only the child's end survives.
    // Through IPC::createPlatformConnection, so the pair is set up exactly as
    // ProcessLauncherTiger sets one up -- including the raised socket buffers.
    auto socketPair = IPC::createPlatformConnection(SOCK_DGRAM, IPC::PlatformConnectionOptions::SetCloexecOnServer);

    char identifierString[24];
    snprintf(identifierString, sizeof identifierString, "1");
    char descriptorString[24];
    snprintf(descriptorString, sizeof descriptorString, "%d", socketPair.client.value());

    double launchedAt = monotonicMilliseconds();
    pid_t networkProcess = fork();
    if (networkProcess < 0) {
        perror("fork");
        return 1;
    }
    if (!networkProcess) {
        char* childArgv[] = { const_cast<char*>(processPath), identifierString, descriptorString, nullptr };
        execve(processPath, childArgv, environ);
        _exit(127);
    }
    socketPair.client = { };

    SilentClient uiSideClient;
    Ref<IPC::Connection> toNetworkProcess = IPC::Connection::createServerConnection(
        IPC::Connection::Identifier { WTF::move(socketPair.server) });
    if (!toNetworkProcess->open(uiSideClient)) {
        fprintf(stderr, "netdriver: could not open the connection\n");
        return 1;
    }

    // --- 1. InitializeNetworkProcess, which is also the handshake. ------------
    WebKit::NetworkProcessCreationParameters creationParameters;

    bool initialized = false;
    toNetworkProcess->sendWithAsyncReply(
        Messages::NetworkProcess::InitializeNetworkProcess(WTF::move(creationParameters)),
        [&initialized] { initialized = true; }, 0);
    runUntil(initialized);
    double initializedAt = monotonicMilliseconds();
    printf("InitializeNetworkProcess: ok, %.0f ms after fork\n", initializedAt - launchedAt);

    // --- 2. A session to load in. --------------------------------------------
    // Everything in here has a default that means "no disk", which is what a
    // driver wants: no cache directory, no cookie file, no storage.
    WebKit::WebsiteDataStoreParameters dataStoreParameters;
    dataStoreParameters.networkSessionParameters.sessionID = PAL::SessionID::defaultSessionID();
    toNetworkProcess->send(Messages::NetworkProcess::AddWebsiteDataStore(WTF::move(dataStoreParameters)), 0);

    // --- 3. The second socket, which arrives over SCM_RIGHTS. -----------------
    auto webProcessIdentifier = WebCore::ProcessIdentifier::generate();
    WebKit::NetworkProcessConnectionParameters connectionParameters;

    // The network process will not load for a first party it was not told about:
    // performSynchronousLoad opens with a MESSAGE_CHECK on
    // NetworkProcess::allowsFirstPartyForCookies, and the allow list is per web
    // process and arrives here, once, with the connection. A real UI process adds
    // to it as the web process is told which sites it may load; the driver knows
    // its whole list up front.
    for (auto& url : urls)
        connectionParameters.allowedFirstPartiesForCookies.add(WebCore::RegistrableDomain { URL { url } });

    bool gotConnection = false;
    std::optional<IPC::ConnectionHandle> connectionHandle;
    WebCore::HTTPCookieAcceptPolicy cookieAcceptPolicy { WebCore::HTTPCookieAcceptPolicy::AlwaysAccept };
    toNetworkProcess->sendWithAsyncReply(
        Messages::NetworkProcess::CreateNetworkConnectionToWebProcess(webProcessIdentifier,
            PAL::SessionID::defaultSessionID(), WTF::move(connectionParameters)),
        [&](std::optional<IPC::ConnectionHandle>&& handle, WebCore::HTTPCookieAcceptPolicy policy) {
            connectionHandle = WTF::move(handle);
            cookieAcceptPolicy = policy;
            gotConnection = true;
        }, 0);
    runUntil(gotConnection);

    if (!connectionHandle) {
        fprintf(stderr, "netdriver: the network process handed back no connection\n");
        return 1;
    }
    printf("CreateNetworkConnectionToWebProcess: got a second socket (valid=%d), cookie policy %u, "
        "network process RSS %ld KB\n", !!*connectionHandle, static_cast<unsigned>(cookieAcceptPolicy),
        residentKilobytes(networkProcess));

    SilentClient webSideClient;
    Ref<IPC::Connection> toNetworkConnection = IPC::Connection::createClientConnection(
        IPC::Connection::Identifier { WTF::move(*connectionHandle) });
    if (!toNetworkConnection->open(webSideClient)) {
        fprintf(stderr, "netdriver: could not open the second connection\n");
        return 1;
    }
    printf("second connection open, valid=%d, network process RSS %ld KB\n",
        toNetworkConnection->isValid(), residentKilobytes(networkProcess));

    // --- 4. The loads. --------------------------------------------------------
    // PerformSynchronousLoad is the smallest fetch there is: one round trip, no
    // NetworkResourceLoader client needed to receive DidReceiveResponse and the
    // data callbacks that ScheduleResourceLoad would send.
    Vector<Fetch> fetches;
    bool allGood = true;

    for (auto& url : urls) {
        Fetch fetch;
        fetch.url = url;

        // The first four members have no defaults -- an ObjectIdentifier has no
        // default constructor -- so they go through the brace, as WebLoaderStrategy
        // does, and everything after that has a sane default member initializer.
        WebKit::NetworkResourceLoadParameters loadParameters {
            WebKit::WebPageProxyIdentifier::generate(),
            WebCore::PageIdentifier::generate(),
            WebCore::FrameIdentifier::generate(),
            WebCore::ResourceRequest { URL { url } }
        };
        // Both of these are required, and the network process says so in its own
        // way: no identifier trips a RELEASE_ASSERT in performSynchronousLoad, and a
        // first party that is not in the list above fails the MESSAGE_CHECK on the
        // line before it -- which drops the reply rather than answering, so the
        // symptom is a driver that waits forever.
        loadParameters.identifier = WebCore::ResourceLoaderIdentifier::generate();
        loadParameters.request.setFirstPartyForCookies(URL { url });
        loadParameters.parentPID = getpid();
        loadParameters.isMainFrameNavigation = true;
        loadParameters.topOrigin = WebCore::SecurityOrigin::create(URL { url });
        loadParameters.sourceOrigin = loadParameters.topOrigin;

        double startedAt = monotonicMilliseconds();
        auto loadResult = toNetworkConnection->sendSync(
            Messages::NetworkConnectionToWebProcess::PerformSynchronousLoad(WTF::move(loadParameters)), 0);
        fetch.milliseconds = monotonicMilliseconds() - startedAt;

        if (!loadResult.succeeded()) {
            fetch.errorDescription = makeString("IPC failure: "_s, IPC::errorAsString(loadResult.error()));
            fetches.append(WTF::move(fetch));
            allGood = false;
            continue;
        }

        auto [error, response, data] = loadResult.takeReply();
        if (!error.isNull()) {
            fetch.errorDescription = makeString(error.domain(), ' ',
                String::number(error.errorCode()), ": "_s, error.localizedDescription());
            fetches.append(WTF::move(fetch));
            allGood = false;
            continue;
        }

        fetch.status = response.httpStatusCode();
        fetch.mimeType = response.mimeType();
        fetch.bodySize = data.size();
        fetch.bodyHead = String::fromUTF8(std::span { data }.first(std::min<size_t>(data.size(), 64)));
        fetch.ok = fetch.status == expectedStatus;
        if (!fetch.ok)
            allGood = false;
        fetches.append(WTF::move(fetch));
    }

    long networkProcessRSS = residentKilobytes(networkProcess);

    printf("\n%-52s %6s %8s %9s  %s\n", "URL", "status", "bytes", "ms", "mime");
    for (auto& fetch : fetches) {
        if (!fetch.errorDescription.isNull()) {
            printf("%-52s  FAILED  %s\n", fetch.url.utf8().data(), fetch.errorDescription.utf8().data());
            continue;
        }
        printf("%-52s %6d %8zu %9.0f  %s%s\n", fetch.url.utf8().data(), fetch.status,
            fetch.bodySize, fetch.milliseconds, fetch.mimeType.utf8().data(),
            fetch.ok ? "" : "   <- unexpected status");
        if (!fetch.bodyHead.isEmpty())
            printf("    first bytes: %s\n", fetch.bodyHead.utf8().data());
    }
    printf("\nnetwork process RSS after the loads: %ld KB (%.1f MB)\n",
        networkProcessRSS, networkProcessRSS / 1024.0);

    // Let it go the way the launcher would: close both ends and let the parent
    // watch in ConnectionUnix notice.
    toNetworkConnection->invalidate();
    toNetworkProcess->invalidate();

    int status = 0;
    for (int i = 0; i < 100; i++) {
        if (waitpid(networkProcess, &status, WNOHANG) == networkProcess) {
            printf("network process exited: %s %d\n",
                WIFSIGNALED(status) ? "signal" : "status",
                WIFSIGNALED(status) ? WTERMSIG(status) : WEXITSTATUS(status));
            return allGood ? 0 : 1;
        }
        usleep(100000);
    }
    printf("network process did not exit within 10 s; killing\n");
    kill(networkProcess, SIGKILL);
    waitpid(networkProcess, &status, 0);
    return allGood ? 0 : 1;
}

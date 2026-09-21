/*
 * Copyright (C) 2026 WebKitTiger.
 *
 * TIGER: enough of a UI process to make bin/TigerWebProcess load a page and hand
 * back what it drew.
 *
 * It wears three hats at once and none of them well, which is the point -- there
 * is no WebPageProxy, no PageClient, no view and no window:
 *
 *   as the UI process, to the network process:
 *     InitializeNetworkProcess, AddWebsiteDataStore, CreateNetworkConnectionToWebProcess
 *   as the UI process, to the web process:
 *     InitializeWebProcess, SetWebsiteDataStoreParameters, CreateWebPage, LoadRequest,
 *     and it answers WebProcessProxy::GetNetworkProcessConnection when asked
 *   as the GPU process, on the socket the web process makes for one:
 *     GPUConnectionToWebProcess::CreateWCLayerTreeHost, and then every
 *     RemoteWCLayerTreeHost::Update -- the layer deltas and the ShareableBitmap
 *     tiles that are the actual rendering
 *
 * The WC path only produces pixels here if the web process rasterizes locally, so
 * the driver turns UseGPUProcessForDOMRenderingEnabled off: DrawingAreaWC then
 * takes ImageBuffer::create<ImageBufferShareableBitmapBackend>, which is cairo on
 * this port, instead of asking a GPU process that does not exist for a remote
 * buffer.
 *
 *   pagedriver <TigerWebProcess> <TigerNetworkProcess> <url> [<png-out>]
 */

#include "config.h"

#include "Connection.h"
#include "GPUConnectionToWebProcessMessages.h"
#include "HandleMessage.h"
#include "IPCUtilities.h"
#include "LoadParameters.h"
#include "MessageNames.h"
#include "NetworkProcessConnectionParameters.h"
#include "DrawingAreaMessages.h"
#include "DrawingAreaProxyMessages.h"
#include "FrameInfoData.h"
#include "NavigationActionData.h"
#include "NetworkProcessConnectionInfo.h"
#include "PolicyDecision.h"
#include "UpdateInfo.h"
#include "NetworkProcessCreationParameters.h"
#include "NetworkProcessMessages.h"
#include "RemoteWCLayerTreeHostMessages.h"
#include "WebKit2Initialize.h"
#include "WebPageCreationParameters.h"
#include "WebPageMessages.h"
#include "WebPageProxyMessages.h"
#include "WebProcessCreationParameters.h"
#include "WebProcessDataStoreParameters.h"
#include "WebProcessMessages.h"
#include "WebProcessProxyMessages.h"
#include "WebsiteDataStoreParameters.h"
#include "WCUpdateInfo.h"

#include <WebCore/HTTPCookieAcceptPolicy.h>
#include <WebCore/OrganizationStorageAccessPromptQuirk.h>
#include <WebCore/RegistrableDomain.h>
#include <WebCore/ResourceRequest.h>
#include <WebCore/ShareableBitmap.h>

#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <wtf/RunLoop.h>
#include <wtf/text/CString.h>
#include <wtf/text/StringBuilder.h>
#include <wtf/text/WTFString.h>

extern "C" char** environ;

namespace {

double millisecondsNow()
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

// Through IPC::createPlatformConnection rather than socketpair() directly, so the
// pair gets the same treatment ProcessLauncherTiger's does -- in particular the
// raised SO_SNDBUF/SO_RCVBUF without which any message over 2 KB fails on 10.4.
pid_t launch(const char* path, UnixFileDescriptor& serverSocketOut)
{
    auto pair = IPC::createPlatformConnection(SOCK_DGRAM, IPC::PlatformConnectionOptions::SetCloexecOnServer);

    char identifier[24];
    snprintf(identifier, sizeof identifier, "1");
    char descriptor[24];
    snprintf(descriptor, sizeof descriptor, "%d", pair.client.value());

    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        return -1;
    }
    if (!child) {
        char* argv[] = { const_cast<char*>(path), identifier, descriptor, nullptr };
        execve(path, argv, environ);
        _exit(127);
    }
    pair.client = { };
    serverSocketOut = WTF::move(pair.server);
    return child;
}

// BGRA8 premultiplied, which is what ImageBufferShareableBitmapBackend produces
// here, straight out to an RGBA PNG. libpng is in the x86_64 sysroot already.
bool writePNG(const char* path, std::span<const uint8_t> pixels, int width, int height, unsigned bytesPerRow)
{
    FILE* file = fopen(path, "wb");
    if (!file)
        return false;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(file);
        return false;
    }
    png_init_io(png, file);
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA,
        PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    Vector<uint8_t> row(width * 4);
    for (int y = 0; y < height; y++) {
        auto source = pixels.subspan(static_cast<size_t>(y) * bytesPerRow, static_cast<size_t>(width) * 4);
        for (int x = 0; x < width; x++) {
            // BGRA -> RGBA. Premultiplied stays premultiplied; the point is to see
            // the picture, not to round-trip it.
            row[x * 4 + 0] = source[x * 4 + 2];
            row[x * 4 + 1] = source[x * 4 + 1];
            row[x * 4 + 2] = source[x * 4 + 0];
            row[x * 4 + 3] = source[x * 4 + 3];
        }
        png_write_row(png, row.span().data());
    }
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    fclose(file);
    return true;
}

const char* changeName(WebKit::WCLayerChange change)
{
    using namespace WebKit;
    switch (change) {
    case WCLayerChange::Children: return "children";
    case WCLayerChange::MaskLayer: return "mask";
    case WCLayerChange::ReplicaLayer: return "replica";
    case WCLayerChange::Position: return "position";
    case WCLayerChange::AnchorPoint: return "anchor";
    case WCLayerChange::Size: return "size";
    case WCLayerChange::BoundsOrigin: return "boundsOrigin";
    case WCLayerChange::MasksToBounds: return "masksToBounds";
    case WCLayerChange::ContentsRectClipsDescendants: return "contentsRectClips";
    case WCLayerChange::ShowDebugBorder: return "debugBorder";
    case WCLayerChange::ShowRepaintCounter: return "repaintCounter";
    case WCLayerChange::ContentsVisible: return "contentsVisible";
    case WCLayerChange::BackfaceVisibility: return "backface";
    case WCLayerChange::Preserves3D: return "preserves3D";
    case WCLayerChange::SolidColor: return "solidColor";
    case WCLayerChange::DebugBorderColor: return "debugBorderColor";
    case WCLayerChange::Opacity: return "opacity";
    case WCLayerChange::DebugBorderWidth: return "debugBorderWidth";
    case WCLayerChange::RepaintCount: return "repaintCount";
    case WCLayerChange::ContentsRect: return "contentsRect";
    case WCLayerChange::Background: return "background";
    case WCLayerChange::Transform: return "transform";
    case WCLayerChange::ChildrenTransform: return "childrenTransform";
    case WCLayerChange::Filters: return "filters";
    case WCLayerChange::BackdropFilters: return "backdropFilters";
    case WCLayerChange::BackdropFiltersRect: return "backdropFiltersRect";
    case WCLayerChange::ContentsClippingRect: return "contentsClippingRect";
    case WCLayerChange::PlatformLayer: return "platformLayer";
    case WCLayerChange::RemoteFrame: return "remoteFrame";
    }
    return "?";
}

class PageDriver final : public IPC::Connection::Client {
public:
    PageDriver(const char* webProcessPath, const char* networkProcessPath, const String& url, const char* pngPath)
        : m_url(url)
        , m_pngPath(pngPath)
        , m_webProcessPath(webProcessPath)
        , m_networkProcessPath(networkProcessPath)
    {
    }

    void ref() const final { }
    void deref() const final { }

    void didClose(IPC::Connection& connection) final
    {
        printf("[%s] connection closed\n", nameFor(connection));
        m_finished = true;
    }

    void didReceiveInvalidMessage(IPC::Connection& connection, IPC::MessageName name, const Vector<uint32_t>&) final
    {
        // Expected and loud on purpose: the driver implements a handful of the UI
        // process's messages and none of WebPageProxy's hundreds, so every one the
        // page sends that is not in the switch below lands here.
        if (m_verbose)
            printf("[%s] ignored %s\n", nameFor(connection), IPC::description(name).characters());
    }

    void didReceiveMessage(IPC::Connection& connection, IPC::Decoder& decoder) final
    {
        using namespace IPC;
        if (decoder.messageName() == MessageName::WebProcessProxy_CreateGPUProcessConnection) {
            handleMessageWithoutUsingIPCConnection<Messages::WebProcessProxy::CreateGPUProcessConnection>(
                decoder, this, &PageDriver::createGPUProcessConnection);
            return;
        }
        if (decoder.messageName() == MessageName::GPUConnectionToWebProcess_CreateWCLayerTreeHost) {
            handleMessage<Messages::GPUConnectionToWebProcess::CreateWCLayerTreeHost>(
                connection, decoder, this, &PageDriver::createWCLayerTreeHost);
            return;
        }
        if (decoder.messageName() == MessageName::RemoteWCLayerTreeHost_Update) {
            handleMessageAsync<Messages::RemoteWCLayerTreeHost::Update>(
                connection, decoder, this, &PageDriver::wcUpdate);
            return;
        }
        if (decoder.messageName() == MessageName::WebPageProxy_DecidePolicyForNavigationActionAsync) {
            handleMessageAsync<Messages::WebPageProxy::DecidePolicyForNavigationActionAsync>(
                connection, decoder, this, &PageDriver::decidePolicyForNavigationAction);
            return;
        }
        if (decoder.messageName() == MessageName::WebPageProxy_DecidePolicyForResponse) {
            handleMessageAsync<Messages::WebPageProxy::DecidePolicyForResponse>(
                connection, decoder, this, &PageDriver::decidePolicyForResponse);
            return;
        }
        if (decoder.messageName() == MessageName::DrawingAreaProxy_Update) {
            handleMessage<Messages::DrawingAreaProxy::Update>(connection, decoder, this, &PageDriver::drawingAreaUpdate);
            return;
        }
        if (decoder.messageName() == MessageName::WebPageProxy_DidChangeContentSize) {
            handleMessage<Messages::WebPageProxy::DidChangeContentSize>(connection, decoder, this, &PageDriver::didChangeContentSize);
            return;
        }
        if (decoder.messageName() == MessageName::WebPageProxy_DidFinishLoadForFrame) {
            m_didFinishLoad = millisecondsNow();
            printf("DidFinishLoadForFrame at %.0f ms after LoadRequest\n", m_didFinishLoad - m_loadRequestedAt);
            m_loadFinished = true;
            return;
        }
        if (m_verbose)
            printf("[%s] unhandled %s\n", nameFor(connection), IPC::description(decoder.messageName()).characters());
        decoder.markInvalid();
    }

    void didReceiveSyncMessage(IPC::Connection& connection, IPC::Decoder& decoder, UniqueRef<IPC::Encoder>& replyEncoder) final
    {
        using namespace IPC;
        if (decoder.messageName() == MessageName::WebProcessProxy_GetNetworkProcessConnection) {
            handleMessageSynchronous<Messages::WebProcessProxy::GetNetworkProcessConnection>(
                connection, decoder, replyEncoder, this, &PageDriver::getNetworkProcessConnection);
            return;
        }
        if (m_verbose)
            printf("[%s] unhandled sync %s\n", nameFor(connection), IPC::description(decoder.messageName()).characters());
        decoder.markInvalid();
    }

    int run();

private:
    const char* nameFor(IPC::Connection& connection) const
    {
        if (m_toWebProcess && m_toWebProcess.get() == &connection)
            return "web";
        if (m_toNetworkProcess && m_toNetworkProcess.get() == &connection)
            return "net";
        return "gpu";
    }

    void spin(bool& until, double timeoutMilliseconds, const char* what);
    void dumpBitmap(WebCore::ShareableBitmap&, const char* source);

    // --- the three messages the driver actually implements --------------------
    void getNetworkProcessConnection(CompletionHandler<void(WebKit::NetworkProcessConnectionInfo&&)>&& reply)
    {
        printf("web process asked for its network connection\n");
        reply(WebKit::NetworkProcessConnectionInfo { WTF::move(m_pendingNetworkConnection), m_cookieAcceptPolicy });
    }

    void createGPUProcessConnection(WebKit::GPUProcessConnectionIdentifier, IPC::ConnectionHandle&& handle)
    {
        printf("web process asked for a GPU connection; standing in for one\n");
        m_toGPUSide = IPC::Connection::createClientConnection(IPC::Connection::Identifier { WTF::move(handle) });
        m_toGPUSide->open(*this);
    }

    void createWCLayerTreeHost(WebKit::WCLayerTreeHostIdentifier, uint64_t nativeWindow, bool usesOffscreenRendering)
    {
        printf("CreateWCLayerTreeHost: nativeWindow=%llu offscreen=%d\n",
            static_cast<unsigned long long>(nativeWindow), usesOffscreenRendering);
    }

    void wcUpdate(WebKit::WCUpdateInfo&&, CompletionHandler<void(std::optional<WebKit::UpdateInfo>)>&&);

    // The web process will not get past the provisional load without an answer to
    // this. Nothing else in the driver has to decide anything; every navigation is
    // allowed.
    void decidePolicyForNavigationAction(WebKit::NavigationActionData&&, CompletionHandler<void(WebKit::PolicyDecision&&)>&& reply)
    {
        printf("DecidePolicyForNavigationActionAsync -> Use\n");
        reply(WebKit::PolicyDecision { .policyAction = WebCore::PolicyAction::Use });
    }

    void didChangeContentSize(const WebCore::IntSize& size)
    {
        printf("DidChangeContentSize %dx%d at %.0f ms\n", size.width(), size.height(),
            millisecondsNow() - m_loadRequestedAt);
    }

    void decidePolicyForResponse(WebKit::FrameInfoData&&, std::optional<WebCore::NavigationIdentifier>,
        WebCore::ResourceResponse&& response, WebCore::ResourceRequest&&, bool canShowMIMEType, String&&, bool,
        WebCore::CrossOriginOpenerPolicyValue, CompletionHandler<void(WebKit::PolicyDecision&&)>&& reply)
    {
        printf("DecidePolicyForResponse: %d %s (canShowMIMEType=%d) -> Use\n",
            response.httpStatusCode(), response.mimeType().utf8().data(), canShowMIMEType);
        reply(WebKit::PolicyDecision { .policyAction = WebCore::PolicyAction::Use });
    }

    // DrawingAreaWC's NON-accelerated path: a page with no composited layer paints
    // the whole view into one ShareableBitmap and sends it here rather than as a
    // layer tree. This is where a plain HTML page's pixels actually arrive.
    void drawingAreaUpdate(uint64_t backingStoreStateID, WebKit::UpdateInfo&& updateInfo);

    String m_url;
    const char* m_pngPath { nullptr };
    const char* m_webProcessPath { nullptr };
    const char* m_networkProcessPath { nullptr };

    RefPtr<IPC::Connection> m_toWebProcess;
    RefPtr<IPC::Connection> m_toNetworkProcess;
    RefPtr<IPC::Connection> m_toGPUSide;

    pid_t m_webProcess { 0 };
    pid_t m_networkProcess { 0 };

    IPC::Connection::Handle m_pendingNetworkConnection;
    WebCore::HTTPCookieAcceptPolicy m_cookieAcceptPolicy { WebCore::HTTPCookieAcceptPolicy::AlwaysAccept };

    WebKit::DrawingAreaIdentifier m_drawingAreaIdentifier { WebKit::DrawingAreaIdentifier::generate() };
    double m_loadRequestedAt { 0 };
    double m_didFinishLoad { 0 };
    bool m_loadFinished { false };
    bool m_finished { false };
    bool m_sawUpdateWithTile { false };
    unsigned m_updateCount { 0 };
    unsigned m_updatesBeforeForce { 0 };
    bool m_verbose { !!getenv("PAGEDRIVER_VERBOSE") };
};

void PageDriver::spin(bool& until, double timeoutMilliseconds, const char* what)
{
    double deadline = millisecondsNow() + timeoutMilliseconds;
    while (!until && !m_finished && millisecondsNow() < deadline)
        RunLoop::cycle();
    if (!until)
        printf("  (timed out waiting for %s after %.0f ms)\n", what, timeoutMilliseconds);
}

void PageDriver::dumpBitmap(WebCore::ShareableBitmap& bitmap, const char* source)
{
    auto size = bitmap.size();
    auto pixels = bitmap.span();
    printf("  bitmap %dx%d, %u bytes/row, from %s\n", size.width(), size.height(), bitmap.bytesPerRow(), source);

    size_t opaque = 0, nonWhite = 0;
    for (int y = 0; y < size.height(); y++) {
        auto row = pixels.subspan(static_cast<size_t>(y) * bitmap.bytesPerRow());
        for (int x = 0; x < size.width(); x++) {
            uint8_t b = row[x * 4 + 0], g = row[x * 4 + 1], r = row[x * 4 + 2], a = row[x * 4 + 3];
            if (a)
                opaque++;
            if (a && !(r > 0xf0 && g > 0xf0 && b > 0xf0))
                nonWhite++;
        }
    }
    printf("  %zu non-transparent pixels, %zu of them not near-white\n", opaque, nonWhite);

    if (!m_pngPath)
        return;
    if (writePNG(m_pngPath, pixels, size.width(), size.height(), bitmap.bytesPerRow())) {
        printf("  -> wrote %s\n", m_pngPath);
        if (nonWhite > 100)
            m_sawUpdateWithTile = true;
    } else
        printf("  -> PNG write FAILED\n");
}

void PageDriver::wcUpdate(WebKit::WCUpdateInfo&& update, CompletionHandler<void(std::optional<WebKit::UpdateInfo>)>&& reply)
{
    m_updateCount++;
    printf("\n=== WC update %u, %.0f ms after LoadRequest ===\n", m_updateCount, millisecondsNow() - m_loadRequestedAt);
    printf("viewport %dx%d  rootLayer=%llu  added=%zu removed=%zu changed=%zu\n",
        update.viewport.width(), update.viewport.height(),
        update.rootLayer ? static_cast<unsigned long long>(update.rootLayer->object().toUInt64()) : 0ull,
        update.addedLayers.size(), update.removedLayers.size(), update.changedLayers.size());

    for (auto& layer : update.changedLayers) {
        StringBuilder changes;
        for (auto change : layer.changes) {
            if (!changes.isEmpty())
                changes.append(',');
            changes.append(StringView::fromLatin1(changeName(change)));
        }
        printf("  layer %llu  pos=(%.0f,%.0f) size=%.0fx%.0f opacity=%.2f children=%zu  [%s]\n",
            static_cast<unsigned long long>(layer.id.object().toUInt64()),
            layer.position.x(), layer.position.y(), layer.size.width(), layer.size.height(),
            layer.opacity, layer.children.size(), changes.toString().utf8().data());

        if (!layer.background.tileUpdates.isEmpty()) {
            auto colorDescription = layer.background.color.isValid()
                ? layer.background.color.debugDescription() : "invalid"_str;
            printf("    background: hasBackingStore=%d size=%dx%d tiles=%zu color=%s\n",
                layer.background.hasBackingStore,
                layer.background.backingStoreSize.width(), layer.background.backingStoreSize.height(),
                layer.background.tileUpdates.size(), colorDescription.utf8().data());
        }

        for (auto& tile : layer.background.tileUpdates) {
            RefPtr bitmap = tile.backingStore.bitmap();
            printf("    tile (%d,%d) willRemove=%d dirty=%dx%d@%d,%d bitmap=%s\n",
                tile.index.x(), tile.index.y(), tile.willRemove,
                tile.dirtyRect.width(), tile.dirtyRect.height(), tile.dirtyRect.x(), tile.dirtyRect.y(),
                bitmap ? "yes" : "no");

            if (bitmap)
                dumpBitmap(*bitmap, "WC tile");
        }
    }
    reply(std::nullopt);
}

void PageDriver::drawingAreaUpdate(uint64_t backingStoreStateID, WebKit::UpdateInfo&& updateInfo)
{
    m_updateCount++;
    printf("\n=== DrawingAreaProxy::Update %u (state %llu), %.0f ms after LoadRequest ===\n",
        m_updateCount, static_cast<unsigned long long>(backingStoreStateID),
        millisecondsNow() - m_loadRequestedAt);
    printf("viewSize %dx%d  scale %.1f  updateBounds %dx%d@%d,%d  rects=%zu  bitmap=%s\n",
        updateInfo.viewSize.width(), updateInfo.viewSize.height(), updateInfo.deviceScaleFactor,
        updateInfo.updateRectBounds.width(), updateInfo.updateRectBounds.height(),
        updateInfo.updateRectBounds.x(), updateInfo.updateRectBounds.y(),
        updateInfo.updateRects.size(), updateInfo.bitmapHandle ? "yes" : "no");

    if (updateInfo.bitmapHandle) {
        if (RefPtr bitmap = WebCore::ShareableBitmap::create(WTF::move(*updateInfo.bitmapHandle)))
            dumpBitmap(*bitmap, "DrawingAreaProxy::Update");
        else
            printf("  the bitmap handle did not map\n");
    }

    // Without this the web process waits for the display and never paints again.
    m_toWebProcess->send(Messages::DrawingArea::DisplayDidRefresh(MonotonicTime::now()), m_drawingAreaIdentifier.toUInt64());
}

int PageDriver::run()
{
    // --- the network process, and a session ----------------------------------
    UnixFileDescriptor networkSocket;
    m_networkProcess = launch(m_networkProcessPath, networkSocket);
    if (m_networkProcess < 0)
        return 1;
    m_toNetworkProcess = IPC::Connection::createServerConnection(
        IPC::Connection::Identifier { WTF::move(networkSocket) });
    m_toNetworkProcess->open(*this);

    bool networkReady = false;
    m_toNetworkProcess->sendWithAsyncReply(
        Messages::NetworkProcess::InitializeNetworkProcess(WebKit::NetworkProcessCreationParameters { }),
        [&networkReady] { networkReady = true; }, 0);
    spin(networkReady, 15000, "InitializeNetworkProcess");

    WebKit::WebsiteDataStoreParameters dataStoreParameters;
    dataStoreParameters.networkSessionParameters.sessionID = PAL::SessionID::defaultSessionID();
    m_toNetworkProcess->send(Messages::NetworkProcess::AddWebsiteDataStore(WTF::move(dataStoreParameters)), 0);

    // The web process's network connection is made now rather than when it asks,
    // so that answering its synchronous GetNetworkProcessConnection is a reply and
    // not a second nested conversation.
    auto webProcessIdentifier = WebCore::ProcessIdentifier::generate();
    WebKit::NetworkProcessConnectionParameters connectionParameters;
    connectionParameters.allowedFirstPartiesForCookies.add(WebCore::RegistrableDomain { URL { m_url } });

    bool haveNetworkConnection = false;
    m_toNetworkProcess->sendWithAsyncReply(
        Messages::NetworkProcess::CreateNetworkConnectionToWebProcess(webProcessIdentifier,
            PAL::SessionID::defaultSessionID(), WTF::move(connectionParameters)),
        [&](std::optional<IPC::ConnectionHandle>&& handle, WebCore::HTTPCookieAcceptPolicy policy) {
            if (handle)
                m_pendingNetworkConnection = WTF::move(*handle);
            m_cookieAcceptPolicy = policy;
            haveNetworkConnection = true;
        }, 0);
    spin(haveNetworkConnection, 15000, "CreateNetworkConnectionToWebProcess");

    // --- the web process ------------------------------------------------------
    UnixFileDescriptor webSocket;
    double launchedAt = millisecondsNow();
    m_webProcess = launch(m_webProcessPath, webSocket);
    if (m_webProcess < 0)
        return 1;
    m_toWebProcess = IPC::Connection::createServerConnection(
        IPC::Connection::Identifier { WTF::move(webSocket) });
    m_toWebProcess->open(*this);

    WebKit::WebProcessCreationParameters webProcessParameters;
    bool webReady = false;
    m_toWebProcess->sendWithAsyncReply(
        Messages::WebProcess::InitializeWebProcess(WTF::move(webProcessParameters)),
        [&](WebCore::ProcessIdentity&&) { webReady = true; }, 0);
    spin(webReady, 20000, "InitializeWebProcess");
    printf("InitializeWebProcess: ok, %.0f ms after fork\n", millisecondsNow() - launchedAt);

    WebKit::WebProcessDataStoreParameters webDataStoreParameters { .sessionID = PAL::SessionID::defaultSessionID() };
    m_toWebProcess->send(Messages::WebProcess::SetWebsiteDataStoreParameters(WTF::move(webDataStoreParameters)), 0);

    // --- the page -------------------------------------------------------------
    auto pageIdentifier = WebCore::PageIdentifier::generate();
    WebKit::WebPreferencesStore preferences;
    // Without this DrawingAreaWC asks a GPU process that does not exist for its
    // image buffers and no pixels ever come back. Off means cairo, in process.
    preferences.setBoolValueForKey("UseGPUProcessForDOMRenderingEnabled"_s, false);
    preferences.setBoolValueForKey("AcceleratedCompositingEnabled"_s, true);

    // Designated initialisers, because the struct has some three hundred members
    // and the only ones that MUST be named are the ones with no default member
    // initialiser -- an ObjectIdentifier has no default constructor, so leaving one
    // out does not compile. Everything else is upstream's default.
    WebKit::WebPageCreationParameters pageParameters {
        .viewSize = WebCore::IntSize { 800, 600 },
        .activityState = { WebCore::ActivityState::IsVisible, WebCore::ActivityState::IsInWindow,
            WebCore::ActivityState::IsFocused, WebCore::ActivityState::WindowIsActive },
        .store = WTF::move(preferences),
        .drawingAreaIdentifier = m_drawingAreaIdentifier,
        .webPageProxyIdentifier = WebKit::WebPageProxyIdentifier::generate(),
        .pageGroupData = WebKit::WebPageGroupData { "TigerPageDriver"_s, WebKit::PageGroupIdentifier::generate() },
        .visitedLinkTableID = WebKit::VisitedLinkTableIdentifier::generate(),
        .userContentControllerParameters = { .identifier = WebKit::UserContentControllerIdentifier::generate() },
        .mainFrameIdentifier = WebCore::FrameIdentifier::generate(),
    };
    m_toWebProcess->send(Messages::WebProcess::CreateWebPage(pageIdentifier, WTF::move(pageParameters)), 0);
    printf("CreateWebPage %llu, 800x600\n", static_cast<unsigned long long>(pageIdentifier.toUInt64()));

    // --- the load -------------------------------------------------------------
    WebKit::LoadParameters loadParameters;
    loadParameters.request = WebCore::ResourceRequest { URL { m_url } };
    loadParameters.request.setFirstPartyForCookies(URL { m_url });
    loadParameters.shouldTreatAsContinuingLoad = WebCore::ShouldTreatAsContinuingLoad::No;

    m_loadRequestedAt = millisecondsNow();
    m_toWebProcess->send(Messages::WebPage::LoadRequest(WTF::move(loadParameters)), pageIdentifier.toUInt64());
    printf("LoadRequest %s\n", m_url.utf8().data());

    spin(m_loadFinished, 30000, "DidFinishLoadForFrame");

    // The load being finished is not the same as the page having been laid out and
    // painted. This port has no display refresh monitor -- no CVDisplayLink, no
    // GLib frame clock, no compositor -- so nothing schedules a rendering update
    // except DrawingArea::DisplayDidRefresh coming back from the UI process. A real
    // UI process sends one per frame; the driver fakes a 60 Hz vsync for a second
    // and asks for a full repaint on the way in.
    m_updatesBeforeForce = m_updateCount;
    m_toWebProcess->send(Messages::DrawingArea::ForceUpdate(), m_drawingAreaIdentifier.toUInt64());
    double vsyncUntil = millisecondsNow() + 3000;
    double nextTick = 0;
    while (millisecondsNow() < vsyncUntil && !m_finished) {
        if (millisecondsNow() >= nextTick) {
            m_toWebProcess->send(Messages::DrawingArea::DisplayDidRefresh(MonotonicTime::now()),
                m_drawingAreaIdentifier.toUInt64());
            nextTick = millisecondsNow() + 16;
        }
        RunLoop::cycle();
    }

    // Let anything still in flight land.
    double settle = millisecondsNow() + 2000;
    while (millisecondsNow() < settle && !m_finished)
        RunLoop::cycle();

    printf("\nweb process RSS after the load: %ld KB (%.1f MB)\n", residentKilobytes(m_webProcess), residentKilobytes(m_webProcess) / 1024.0);
    printf("network process RSS:            %ld KB\n", residentKilobytes(m_networkProcess));
    printf("WC updates received:            %u%s\n", m_updateCount, m_sawUpdateWithTile ? ", at least one with a tile" : "");

    if (m_toGPUSide)
        m_toGPUSide->invalidate();
    m_toWebProcess->invalidate();
    m_toNetworkProcess->invalidate();

    int status = 0;
    for (int i = 0; i < 60; i++) {
        bool webGone = waitpid(m_webProcess, &status, WNOHANG) == m_webProcess;
        if (webGone) {
            printf("web process exited: %s %d\n", WIFSIGNALED(status) ? "signal" : "status",
                WIFSIGNALED(status) ? WTERMSIG(status) : WEXITSTATUS(status));
            break;
        }
        usleep(100000);
    }
    kill(m_webProcess, SIGKILL);
    kill(m_networkProcess, SIGKILL);
    waitpid(m_webProcess, &status, 0);
    waitpid(m_networkProcess, &status, 0);

    return m_loadFinished && m_sawUpdateWithTile ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <TigerWebProcess> <TigerNetworkProcess> <url> [<png-out>]\n", argv[0]);
        return 2;
    }
    WebKit::InitializeWebKit2();
    PageDriver driver(argv[1], argv[2], String::fromUTF8(argv[3]), argc > 4 ? argv[4] : "/tmp/tiger-tile.png");
    return driver.run();
}

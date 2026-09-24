#include "PixelArtModule.h"
#include "Channels.h"
#include "MeshService.h"
#include "MessageStore.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "graphics/PixelArtDecoder.h"
#include "graphics/Screen.h"
#include "graphics/draw/MessageRenderer.h"
#include "main.h"

PixelArtModule *pixelArtModule = nullptr;

PixelArtModule::PixelArtModule() : SinglePortModule("pixelart", meshtastic_PortNum_PRIVATE_APP)
{
}

bool PixelArtModule::wantPacket(const meshtastic_MeshPacket *p)
{
    if (!SinglePortModule::wantPacket(p))
        return false;
    const auto &d = p->decoded;
    return PixelArt::isPixelArtPacket(d.payload.bytes, d.payload.size);
}

ProcessMessage PixelArtModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
    LOG_INFO("Received pixel art msg from=0x%08x, id=0x%08x, size=%u", mp.from, mp.id, mp.decoded.payload.size);
#endif

    IF_SCREEN(
        if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_COLOR) {
            const StoredMessage *sm = messageStore.tryAddFromPacket(mp);
            if (!sm)
                return ProcessMessage::CONTINUE;

            auto *display = screen ? screen->getDisplayDevice() : nullptr;
            graphics::MessageRenderer::handleNewMessage(display, *sm, mp);
        })

    if (shouldWakeOnReceivedMessage() && !isMutedForPacket(mp)) {
        powerFSM.trigger(EVENT_RECEIVED_MSG);
    }

    notifyObservers(&mp);

    return ProcessMessage::CONTINUE;
}

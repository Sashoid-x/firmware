#pragma once

#include "Observer.h"
#include "SinglePortModule.h"

/**
 * Pixel Art message handling for Meshtastic.
 *
 * Listens on PortNum_PRIVATE_APP (256) for compressed pixel art bitmaps.
 * Validates against MFT and decodes/stores pixel art in the central message history
 * so it renders in the chat frame alongside text messages.
 */
class PixelArtModule : public SinglePortModule, public Observable<const meshtastic_MeshPacket *>
{
  public:
    PixelArtModule();

  protected:
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;
};

extern PixelArtModule *pixelArtModule;

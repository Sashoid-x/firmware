#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_REPLYBOT
/*
* ReplyBotModule.cpp
*
* This module implements a simple reply bot for the Meshtastic firmware.  It listens for
* specific text commands delivered either via a direct message (DM) or a broadcast on the primary channel.  
* When a supported command is received the bot responds with a short status message that includes the hop count 
* (minimum number of relays), RSSI and SNR of the received packet.  To avoid spamming
* the network it enforces a per‑sender cooldown between responses.  By default the
* module is disabled. See the official firmware documentation for guidance on adding modules.
* To enable this module, set `#undef MESHTASTIC_EXCLUDE_REPLYBOT` in your variant.h file.
*/
#include "Channels.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "ReplyBotModule.h"
#include "mesh/MeshTypes.h"
#include <Arduino.h>
#include <cctype>
#include <cstring>
#include "mesh/generated/meshtastic/cannedmessages.pb.h"

// Объявляем доступ к глобальному конфигу из CannedMessageModule.cpp
extern meshtastic_CannedMessageModuleConfig cannedMessageModuleConfig;

//
// Rate limiting data structures
//
struct ReplyBotCooldownEntry {
    uint32_t from = 0;
    uint32_t lastMs = 0;
};

static constexpr uint8_t REPLYBOT_COOLDOWN_SLOTS = 8;          // ring buffer size
static constexpr uint32_t REPLYBOT_DM_COOLDOWN_MS = 1 * 1000;  // 1 seconds for DMs
static constexpr uint32_t REPLYBOT_LF_COOLDOWN_MS = 1 * 1000;  // 1 seconds for LongFast broadcasts
static ReplyBotCooldownEntry replybotCooldown[REPLYBOT_COOLDOWN_SLOTS];
static uint8_t replybotCooldownIdx = 0;

// Return true if a reply should be rate‑limited for this sender, updating the
// entry table as needed.
static bool replybotRateLimited(uint32_t from, uint32_t cooldownMs)
{
    const uint32_t now = millis();
    for (auto &e : replybotCooldown) {
        if (e.from == from) {
            if ((uint32_t)(now - e.lastMs) < cooldownMs) {
                return true;
            }
            e.lastMs = now;
            return false;
        }
    }
    replybotCooldown[replybotCooldownIdx].from = from;
    replybotCooldown[replybotCooldownIdx].lastMs = now;
    replybotCooldownIdx = (replybotCooldownIdx + 1) % REPLYBOT_COOLDOWN_SLOTS;
    return false;
}

// Constructor
ReplyBotModule::ReplyBotModule() : SinglePortModule("replybot", meshtastic_PortNum_TEXT_MESSAGE_APP)
{
    isPromiscuous = true;
}

void ReplyBotModule::setup()
{
    // In future we may add a protobuf configuration; for now the module is
    // always enabled when compiled in.
}

bool ReplyBotModule::wantPacket(const meshtastic_MeshPacket *p)
{
    return (p && p->decoded.portnum == ourPortNum);
}

ProcessMessage ReplyBotModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // Если led_heartbeat == false, модуль просто игнорирует входящие сообщения
    if (config.device.led_heartbeat_disabled) {
        return ProcessMessage::CONTINUE;
    }

    const uint32_t ourNode = nodeDB->getNodeNum();
    
    // [ИЗМЕНЕНИЕ 1] Игнорируем сообщения, отправленные самим собой
    if (mp.rx_rssi == 0) {
        return ProcessMessage::CONTINUE;
    }

    const bool isDM = (mp.to == ourNode);
    const bool isPrimaryChannel = (mp.channel == channels.getPrimaryIndex()) && isBroadcast(mp.to);

    if (!isDM && !isPrimaryChannel) {
        return ProcessMessage::CONTINUE;
    }

    // Ignore empty payloads
    if (mp.decoded.payload.size == 0) {
        return ProcessMessage::CONTINUE;
    }

    // Copy payload into a null‑terminated buffer
    char buf[260];
    memset(buf, 0, sizeof(buf));
    size_t n = mp.decoded.payload.size;
    if (n > sizeof(buf) - 1)
        n = sizeof(buf) - 1;
    memcpy(buf, mp.decoded.payload.bytes, n);
    
    for (int i = 0; buf[i]; i++) {
        buf[i] = tolower((unsigned char)buf[i]);
    }

    // React only to supported slash commands
    if (!isCommand(buf)) {
        return ProcessMessage::CONTINUE;
    }

    // Apply rate limiting per sender depending on DM/broadcast
    const uint32_t cooldownMs = isDM ? REPLYBOT_DM_COOLDOWN_MS : REPLYBOT_LF_COOLDOWN_MS;
    if (replybotRateLimited(mp.from, cooldownMs)) {
        return ProcessMessage::CONTINUE;
    }

    int hopsAway = getHopsAway(mp);

    // Normalize RSSI
    int rssi = mp.rx_rssi;
    if (rssi > 0) {
        rssi -= 200;
    }
    float snr = mp.rx_snr;

    // [ИЗМЕНЕНИЕ 2] Получаем первую команду из конфига для использования в качестве эмодзи/суффикса
    char suffix[64] = "🤖"; // Значение по умолчанию, если конфиг пуст
    char configCopy[sizeof(cannedMessageModuleConfig.messages)];
    strncpy(configCopy, cannedMessageModuleConfig.messages, sizeof(configCopy) - 1);
    configCopy[sizeof(configCopy) - 1] = '\0';
    
    char *firstToken = strtok(configCopy, "|");
    if (firstToken && strlen(firstToken) > 0) {
        strncpy(suffix, firstToken, sizeof(suffix) - 1);
        suffix[sizeof(suffix) - 1] = '\0';
    }

    // Build the reply message and send it back via DM
    char reply[96];
    if (hopsAway > 0) {
        // Используем динамический суффикс вместо жестко прописанного 🤖
        snprintf(reply, sizeof(reply), "%d%s", hopsAway, suffix);
    } else {
        snprintf(reply, sizeof(reply), "%.1f/%d%s", snr, rssi, suffix);
    }

    sendDm(mp, reply, isDM);
    return ProcessMessage::CONTINUE;
}

bool ReplyBotModule::isCommand(const char *msg) const
{
    if (!msg)
        return false;

    // Пропускаем начальные пробелы
    while (*msg == ' ' || *msg == '\t')
        msg++;

    auto isEndOrSpace = [](char c) { return c == '\0' || std::isspace(static_cast<unsigned char>(c)); };

    // Копируем строку с командами во временный буфер для токенизации
    char configCopy[sizeof(cannedMessageModuleConfig.messages)];
    strncpy(configCopy, cannedMessageModuleConfig.messages, sizeof(configCopy) - 1);
    configCopy[sizeof(configCopy) - 1] = '\0';

    // Разделяем строку по символу '|'
    char *token = strtok(configCopy, "|");
    while (token != NULL) {
        // Приводим токен к нижнему регистру
        for (int i = 0; token[i]; i++) {
            token[i] = tolower((unsigned char)token[i]);
        }

        size_t tokenLen = strlen(token);
        
        // Проверяем, начинается ли сообщение с текущей команды и идет ли после нее пробел/конец строки
        if (tokenLen > 0 && strncmp(msg, token, tokenLen) == 0 && isEndOrSpace(msg[tokenLen])) {
            return true;
        }
        token = strtok(NULL, "|");
    }
    return false;
}

// Send a direct message back to the originating node.
void ReplyBotModule::sendDm(const meshtastic_MeshPacket &rx, const char *text, bool isDM)
{
    if (!text)
        return;

    meshtastic_MeshPacket *p = allocDataPacket();
    if (isDM) {
        p->to = rx.from;
    } else {
        p->to = NODENUM_BROADCAST;
    }
    
    p->channel = rx.channel;
    p->want_ack = false;
    p->decoded.want_response = false;
    p->decoded.reply_id = rx.id;
    p->decoded.emoji = true;

    size_t len = strlen(text);
    if (len > sizeof(p->decoded.payload.bytes)) {
        len = sizeof(p->decoded.payload.bytes);
    }
    
    p->decoded.payload.size = len;
    memcpy(p->decoded.payload.bytes, text, len);

    LOG_INFO("ReplyBOT: Sent response  msg=%s", p);
    service->sendToMesh(p);
}

#endif // MESHTASTIC_EXCLUDE_REPLYBOT

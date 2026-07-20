#if !MESHTASTIC_EXCLUDE_STATUS
#include "StatusMessageModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "ProtobufModule.h"
#include <cstring>

StatusMessageModule *statusMessageModule;

// Статические переменные для хранения состояния ротации
static int currentStatusIndex = 0;
static char lastProcessedStatus[256] = {0}; // Для отслеживания изменений в конфиге

int32_t StatusMessageModule::runOnce()
{
    if (moduleConfig.has_statusmessage && moduleConfig.statusmessage.node_status[0] != '\0') {
        
        // 1. Безопасно копируем текущий статус из конфига для сравнения
        char currentConfigStatus[256];
        strncpy(currentConfigStatus, moduleConfig.statusmessage.node_status, sizeof(currentConfigStatus) - 1);
        currentConfigStatus[sizeof(currentConfigStatus) - 1] = '\0'; // Гарантируем null-терминатор

        // 2. Проверяем, изменился ли текст статуса в настройках
        if (strcmp(lastProcessedStatus, currentConfigStatus) != 0) {
            currentStatusIndex = 0; // Сбрасываем счетчик на первый статус
            strcpy(lastProcessedStatus, currentConfigStatus); // Обновляем сохраненный статус
        }

        // 3. Создаем копию строки для токенизации (strtok модифицирует строку)
        char statusCopy[256];
        strcpy(statusCopy, currentConfigStatus); // Используем уже скопированную и проверенную строку

        // 4. Считаем количество статусов (количество '|' + 1)
        int statusCount = 1;
        for (int i = 0; statusCopy[i]; i++) {
            if (statusCopy[i] == '|') {
                statusCount++;
            }
        }

        // 5. Защита от выхода за границы (на случай, если конфиг изменился и статусов стало меньше)
        if (currentStatusIndex >= statusCount) {
            currentStatusIndex = 0;
        }

        // 6. Ищем нужный токен (статус) по текущему индексу
        meshtastic_StatusMessage ourStatus = meshtastic_StatusMessage_init_zero;
        char *token = strtok(statusCopy, "|");
        int currentIndex = 0;
        while (token != NULL) {
            if (currentIndex == currentStatusIndex) {
                // Убираем лишние пробелы по краям для красивого вывода
                while (*token == ' ') token++;
                char *end = token + strlen(token) - 1;
                while (end > token && *end == ' ') end--;
                *(end + 1) = '\0';
                
                strncpy(ourStatus.status, token, sizeof(ourStatus.status));
                ourStatus.status[sizeof(ourStatus.status) - 1] = '\0';
                break;
            }
            currentIndex++;
            token = strtok(NULL, "|");
        }

        // 7. Увеличиваем индекс для следующего вызова (с циклическим переходом)
        currentStatusIndex = (currentStatusIndex + 1) % statusCount;

        // 8. Формируем и отправляем пакет
        meshtastic_MeshPacket *p = allocDataPacket();
        p->decoded.payload.size = pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes),
                                                     meshtastic_StatusMessage_fields, &ourStatus);
        p->to = NODENUM_BROADCAST;
        p->decoded.want_response = false;
        p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
        p->channel = 0;
        service->sendToMesh(p);
    }
    
    // Интервал отправки (используем стандартный интервал обновления телеметрии)
    uint32_t intervalSecs = moduleConfig.telemetry.device_update_interval;
    
    // [ДОПОЛНЕНИЕ] Защита от нулевого интервала, чтобы модуль не завис или не спамил
    if (intervalSecs == 0) {
        intervalSecs = 900; // По умолчанию 15 минут, если в конфиге 0
    }
    
    return (intervalSecs * 1000);
}

ProcessMessage StatusMessageModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        meshtastic_StatusMessage incomingMessage = meshtastic_StatusMessage_init_zero;
        if (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, meshtastic_StatusMessage_fields,
                                 &incomingMessage)) {
            LOG_INFO("Received a NodeStatus message %s", incomingMessage.status);
            if (nodeDB)
                nodeDB->setNodeStatus(mp.from, incomingMessage);
        }
    }
    return ProcessMessage::CONTINUE;
}

#endif

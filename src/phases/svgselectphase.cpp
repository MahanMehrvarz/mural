#include "svgselectphase.h"
#include "LittleFS.h"

SvgSelectPhase::SvgSelectPhase(PhaseManager* manager) {
    this->manager = manager;
}

void SvgSelectPhase::handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
    extern void addLog(const String& msg);
    if (!index)
    {
        if (LittleFS.exists("/commands")) {
            LittleFS.remove("/commands");
        }

        int freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
        addLog(String("SvgSelect: upload started, size=") + String(request->contentLength()) +
               "B free=" + String(freeBytes) + "B");

        if (freeBytes < (int)request->contentLength()) {
            addLog("SvgSelect: ERROR not enough space on LittleFS");
            request->send(400, "text/plain", "Not enough space for upload");
            return;
        }

        request->_tempFile = LittleFS.open("/commands", "w");
    }

    if (len)
    {
        request->_tempFile.write(data, len);
    }

    if (final)
    {
        request->_tempFile.close();
        addLog(String("SvgSelect: upload complete, ") + String(index + len) + "B written");
        manager->setPhase(PhaseManager::BeginDrawing);
    }
}

const char* SvgSelectPhase::getName() {
    return "SvgSelect";
}
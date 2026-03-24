#include "extendtohomephase.h"
void ExtendToHomePhase::extendToHome(AsyncWebServerRequest *request) {
    extern void addLog(const String& msg);
    addLog("ExtendToHome: starting belt extension");
    auto moveTime = movement->extendToHome() + 1; // extra second of waiting for good measure
    addLog(String("ExtendToHome: moving, estimated time=") + String(moveTime) + "s");
    request->send(200, "text/plain", String(moveTime));
}

ExtendToHomePhase::ExtendToHomePhase(PhaseManager* manager, Movement* movement) {
    this->manager = manager;
    this->movement = movement;
}

const char* ExtendToHomePhase::getName() {
    return "ExtendToHome";
}

void ExtendToHomePhase::loopPhase() {
    if (movement->hasStartedHoming() && !movement->isMoving()) {
        extern void addLog(const String& msg);
        addLog("ExtendToHome: motion complete, belts at home position");
        manager->setPhase(PhaseManager::PenCalibration);
    }
}
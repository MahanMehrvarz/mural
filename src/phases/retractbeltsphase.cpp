#include "retractbeltsphase.h"
#include "commandhandlingphase.h"
RetractBeltsPhase::RetractBeltsPhase(PhaseManager* manager, Movement* movement) : CommandHandlingPhase(movement) {
    this->manager = manager;
    this->movement = movement;
}

void RetractBeltsPhase::doneWithPhase(AsyncWebServerRequest *request) {
    extern void addLog(const String& msg);
    addLog("RetractBelts: user confirmed belts retracted");
    manager->setPhase(PhaseManager::SetTopDistance);
    manager->respondWithState(request);
}

const char* RetractBeltsPhase::getName() {
    return "RetractBelts";
}
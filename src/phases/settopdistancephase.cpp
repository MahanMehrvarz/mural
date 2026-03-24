#include "settopdistancephase.h"
#include "commandhandlingphase.h"
SetTopDistancePhase::SetTopDistancePhase(PhaseManager* manager, Movement* movement, Pen* pen) : CommandHandlingPhase(movement) {
    this->manager = manager;
    this->movement = movement;
    this->pen = pen;
}

void SetTopDistancePhase::setTopDistance(AsyncWebServerRequest *request) {
    const AsyncWebParameter* p = request->getParam(0);
    int distance = p->value().toInt();
    extern void addLog(const String& msg);
    addLog(String("SetTopDistance: param='") + p->value() + "' parsed=" + String(distance));
    movement->setTopDistance(distance);
    manager->setPhase(PhaseManager::ExtendToHome);
    manager->respondWithState(request);
}

void SetTopDistancePhase::setServo(AsyncWebServerRequest *request) {
    const AsyncWebParameter* p = request->getParam(0);
    int angle = p->value().toInt();
    pen->setRawValue(angle);
    request->send(200, "text/plain", "OK"); 
}

void SetTopDistancePhase::estepsCalibration(AsyncWebServerRequest* request) {
    Serial.println("Extending 1000mm");
    movement->extend1000mm();
    request->send(200, "text/plain", "OK");
}

const char* SetTopDistancePhase::getName() {
    return "SetTopDistance";
}
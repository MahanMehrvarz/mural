#include "phasemanager.h"
#include "retractbeltsphase.h"
#include "settopdistancephase.h"
#include "extendtohomephase.h"
#include "pencalibrationphase.h"
#include "svgselectphase.h"
#include "begindrawingphase.h"
#include "AsyncJson.h"
#include "ArduinoJson.h"
#include <stdexcept>

PhaseManager::PhaseManager(Movement* movement, Pen* pen, Runner* runner, AsyncWebServer* server) {
    retractBeltsPhase = new RetractBeltsPhase(this, movement);
    setTopDistancePhase = new SetTopDistancePhase(this, movement, pen);
    extendToHomePhase = new ExtendToHomePhase(this, movement);
    penCalibrationPhase = new PenCalibrationPhase(this, pen);
    svgSelectPhase = new SvgSelectPhase(this);
    beginDrawingPhase = new BeginDrawingPhase(this, runner, server);

    this->movement = movement;
    reset();
}

Phase* PhaseManager::getCurrentPhase() {
    return currentPhase;
}

void PhaseManager::setPhase(PhaseNames name) {
    extern void addLog(const String& msg);
    switch (name) {
        case PhaseNames::RetractBelts:
            addLog("Phase -> RetractBelts");
            currentPhase = retractBeltsPhase;
            break;
        case PhaseNames::SetTopDistance:
            addLog("Phase -> SetTopDistance");
            currentPhase = setTopDistancePhase;
            break;
        case PhaseNames::ExtendToHome:
            addLog("Phase -> ExtendToHome");
            currentPhase = extendToHomePhase;
            break;
        case PhaseNames::PenCalibration:
            addLog("Phase -> PenCalibration");
            currentPhase = penCalibrationPhase;
            break;
        case PhaseNames::SvgSelect:
            addLog("Phase -> SvgSelect");
            currentPhase = svgSelectPhase;
            break;
        case PhaseNames::BeginDrawing:
            addLog("Phase -> BeginDrawing");
            currentPhase = beginDrawingPhase;
            break;
        default:
            addLog("ERROR: setPhase called with invalid phase name");
            throw std::invalid_argument("Invalid Phase");
    }
}

void PhaseManager::respondWithState(AsyncWebServerRequest *request) {
    auto currentPhase = getCurrentPhase()->getName();
    auto moving = movement->isMoving();
    auto startedHoming = movement->hasStartedHoming();
    auto homePosition = movement->getHomeCoordinates();

    auto topDistance = movement->getTopDistance();
    auto safeWidth = topDistance != -1 ? movement->getWidth() : -1;

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonBuffer jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();

    root["phase"] = currentPhase;
    root["moving"] = moving;
    root["topDistance"] = topDistance;
    root["safeWidth"] = safeWidth;
    root["homeX"] = homePosition.x;
    root["homeY"] = homePosition.y;

    root.printTo(*response);
    request->send(response);
}

void PhaseManager::reset() {
    setPhase(PhaseManager::RetractBelts);
}
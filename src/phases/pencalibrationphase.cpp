#include "pencalibrationphase.h"
PenCalibrationPhase::PenCalibrationPhase(PhaseManager* manager, Pen* pen) {
    this->manager = manager;
    this->pen = pen;
    this->runner = runner;
}

void PenCalibrationPhase::setServo(AsyncWebServerRequest *request) {
    const AsyncWebParameter* p = request->getParam(0);
    int angle = p->value().toInt();
    pen->setRawValue(angle);
    request->send(200, "text/plain", "OK"); 
}

void PenCalibrationPhase::setPenDistance(AsyncWebServerRequest *request) {
    extern void addLog(const String& msg);
    const AsyncWebParameter* p = request->getParam(0);
    int angle = p->value().toInt();
    addLog(String("PenCalibration: pen distance set, angle=") + String(angle));
    pen->setPenDistance(angle);
    pen->slowUp();
    addLog("PenCalibration: complete, pen raised");
    manager->setPhase(PhaseManager::SvgSelect);
    manager->respondWithState(request);
}

const char* PenCalibrationPhase::getName() {
    return "PenCalibration";
}
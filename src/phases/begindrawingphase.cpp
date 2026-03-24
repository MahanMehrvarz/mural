#include "begindrawingphase.h"
BeginDrawingPhase::BeginDrawingPhase(PhaseManager* manager, Runner* runner, AsyncWebServer* server) {
    this->manager = manager;
    this->runner = runner;
    this->server = server;
}

void BeginDrawingPhase::run(AsyncWebServerRequest *request) {
    extern void addLog(const String& msg);
    addLog("BeginDrawing: starting plot job");
    runner->start();
    addLog("BeginDrawing: runner started, web server shutting down for drawing");
    request->send(200, "text/plain", "OK");
    server->end();
}

void BeginDrawingPhase::doneWithPhase(AsyncWebServerRequest *request) {
    extern void addLog(const String& msg);
    addLog("BeginDrawing: reset requested by user");
    manager->reset();
    manager->respondWithState(request);
}

const char* BeginDrawingPhase::getName() {
    return "BeginDrawing";
}
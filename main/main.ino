#include <AsyncTCP.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

const char* WIFI_SSID = "IanGostosa";
const char* WIFI_PASS = "12345678";

const int LDR_PIN = 1;

constexpr uint32_t DELAY        = 500;   // Sensor poll interval [ms]
constexpr float    LIGHT_DELTA  = 100.0;  // Min change to recompute window % [mV]
constexpr uint32_t GRACE_PERIOD = 300;   // How long a manual /openness override holds [s]

constexpr float DEFAULT_MV_BRIGHT     = 2200.0; // [mV]
constexpr float DEFAULT_MV_DARK       = 2800.0; // [mV]
constexpr float DEFAULT_REACTION_TIME = 60.0; // [s]

constexpr const char* NVS_NS         = "window";
constexpr const char* NVS_MV_BRIGHT  = "mv_bright";
constexpr const char* NVS_MV_DARK    = "mv_dark";
constexpr const char* NVS_REACT_TIME = "react_time";

float mv_bright;
float mv_dark;
float reaction_time;
float smoothing;

float    light_avg        = 0.0;
float    last_light_level = 0.0;
float    current_perc     = 0.0;
uint32_t grace_until      = 0; // The time to wait until the perc calculation should resume

AsyncWebServer    server(80);
Preferences       prefs;
SemaphoreHandle_t state_mtx;

void recompute_smoothing() {
    smoothing = reaction_time * 1000.0f / (float)DELAY - 1.0f;
}

float calculate_window_perc(float light_level) {
    float t = (light_level - mv_bright) / (mv_dark - mv_bright);
    return constrain(t, 0.0f, 1.0f);
}

void load_prefs() {
    prefs.begin(NVS_NS, true);
    mv_bright     = prefs.getFloat(NVS_MV_BRIGHT,  DEFAULT_MV_BRIGHT);
    mv_dark       = prefs.getFloat(NVS_MV_DARK,    DEFAULT_MV_DARK);
    reaction_time = prefs.getFloat(NVS_REACT_TIME,  DEFAULT_REACTION_TIME);
    prefs.end();
    recompute_smoothing();
}

void save_pref_float(const char* key, float value) {
    prefs.begin(NVS_NS, false);
    prefs.putFloat(key, value);
    prefs.end();
}

void setup_routes() {
    // GET /sensor
    server.on("/sensor", HTTP_GET, [](AsyncWebServerRequest* req) {
        xSemaphoreTake(state_mtx, portMAX_DELAY);
        float mv = light_avg;
        xSemaphoreGive(state_mtx);

        AsyncJsonResponse* res = new AsyncJsonResponse();
        res->getRoot().to<JsonObject>()["mv"] = mv;
        res->setLength();
        req->send(res);
    });

    // GET /window
    server.on("/window", HTTP_GET, [](AsyncWebServerRequest* req) {
        xSemaphoreTake(state_mtx, portMAX_DELAY);
        float perc  = current_perc;
        bool  grace = (millis() < grace_until);
        xSemaphoreGive(state_mtx);

        AsyncJsonResponse* res = new AsyncJsonResponse();
        JsonObject root = res->getRoot().to<JsonObject>();
        root["perc"]            = perc;
        root["manual_override"] = grace;
        res->setLength();
        req->send(res);
    });

    // POST /openness
    server.on("/openness", HTTP_POST, [](AsyncWebServerRequest* req, JsonVariant& json) {
        if (!json["perc"].is<float>()) {
            req->send(400, "application/json", "{\"error\":\"expected {\\\"perc\\\": <0.0-1.0>}\"}");
            return;
        }

        float perc = constrain(json["perc"].as<float>(), 0.0f, 1.0f);

        xSemaphoreTake(state_mtx, portMAX_DELAY);
        current_perc = perc;
        grace_until  = millis() + GRACE_PERIOD * 1000u;
        xSemaphoreGive(state_mtx);

        AsyncJsonResponse* res = new AsyncJsonResponse();
        JsonObject root = res->getRoot().to<JsonObject>();
        root["perc"]           = perc;
        root["grace_period_s"] = GRACE_PERIOD;
        res->setLength();
        req->send(res);
    });

    // POST /calibrate/bright
    server.on("/calibrate/bright", HTTP_POST, [](AsyncWebServerRequest* req) {
        xSemaphoreTake(state_mtx, portMAX_DELAY);
        mv_bright    = light_avg;
        float sample = mv_bright;
        xSemaphoreGive(state_mtx);

        save_pref_float(NVS_MV_BRIGHT, sample);

        AsyncJsonResponse* res = new AsyncJsonResponse();
        res->getRoot().to<JsonObject>()["mv_bright"] = sample;
        res->setLength();
        req->send(res);
    });

    // POST /calibrate/dark
    server.on("/calibrate/dark", HTTP_POST, [](AsyncWebServerRequest* req) {
        xSemaphoreTake(state_mtx, portMAX_DELAY);
        mv_dark      = light_avg;
        float sample = mv_dark;
        xSemaphoreGive(state_mtx);

        save_pref_float(NVS_MV_DARK, sample);

        AsyncJsonResponse* res = new AsyncJsonResponse();
        res->getRoot().to<JsonObject>()["mv_dark"] = sample;
        res->setLength();
        req->send(res);
    });

    // POST /config/reaction_time
    server.on("/config/reaction_time", HTTP_POST, [](AsyncWebServerRequest* req, JsonVariant& json) {
        if (!json["seconds"].is<float>()) {
            req->send(400, "application/json", "{\"error\":\"expected {\\\"seconds\\\": <float>}\"}");
            return;
        }
        float secs = max(json["seconds"].as<float>(), 1.0f);

        xSemaphoreTake(state_mtx, portMAX_DELAY);
        reaction_time = secs;
        recompute_smoothing();
        float new_smoothing = smoothing;
        xSemaphoreGive(state_mtx);

        save_pref_float(NVS_REACT_TIME, secs);

        AsyncJsonResponse* res = new AsyncJsonResponse();
        JsonObject root = res->getRoot().to<JsonObject>();
        root["reaction_time_s"] = secs;
        root["smoothing"]       = new_smoothing;
        res->setLength();
        req->send(res);
    });
}

void setup() {
    Serial.begin(115200);
    analogReadResolution(12);
    analogSetPinAttenuation(LDR_PIN, ADC_11db);

    state_mtx = xSemaphoreCreateMutex();

    load_prefs();

    light_avg = (float)analogReadMilliVolts(LDR_PIN);
    last_light_level = light_avg;
    current_perc = calculate_window_perc(last_light_level);

    WiFi.softAP(WIFI_SSID, WIFI_PASS);
    Serial.print("Starting server");
    // Serial.print("Connecting to WiFi");
    // while (WiFi.status() != WL_CONNECTED) {
    //     delay(500);
    //     Serial.print(".");
    // }
    // Serial.println();
    // Serial.print("IP: ");
    // Serial.println(WiFi.localIP());

    setup_routes();
    server.begin();
    Serial.println("Server started");
}

void loop() {
    uint32_t mV = analogReadMilliVolts(LDR_PIN);

    xSemaphoreTake(state_mtx, portMAX_DELAY);

    light_avg += ((float)mV - light_avg) / (smoothing + 1);

    bool in_grace = (millis() < grace_until);
    if (!in_grace && fabs(light_avg - last_light_level) > LIGHT_DELTA) {
        last_light_level = light_avg;
        current_perc = calculate_window_perc(last_light_level);
    }

    float _avg  = light_avg;
    float _last = last_light_level;
    float _perc = current_perc;

    xSemaphoreGive(state_mtx);

    Serial.print("last_light:");
    Serial.println(_last);
    Serial.print("light_avg:");
    Serial.println(_avg);
    Serial.print("perc:");
    Serial.println(_perc);

    delay(DELAY);
}

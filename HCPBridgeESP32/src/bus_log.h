#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

class BusLog {
public:
    enum Kind : uint8_t { REQUEST, POSITION, STATE, LAMP, UNKNOWN_STATE, MQTT_QUEUED, MQTT_FAILED, REGISTER };
    struct Entry { uint32_t sequence, ms; uint16_t address, oldValue, value, extra; int16_t currentPercent, targetPercent; uint8_t kind, state, flags, function; };
    static constexpr size_t Capacity = 512;
private:
    Entry entries[Capacity] = {};
    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    uint32_t sequence = 0, overwritten = 0;
    size_t head = 0, count = 0;
public:
    void add(Kind kind, uint16_t address, uint16_t oldValue, uint16_t value, int current, int target, uint8_t state, uint8_t flags = 0, uint8_t function = 0, uint16_t extra = 0) {
        Entry e{}; e.ms = millis(); e.kind = kind; e.address = address; e.oldValue = oldValue; e.value = value; e.currentPercent = current; e.targetPercent = target; e.state = state; e.flags = flags; e.function = function; e.extra = extra;
        portENTER_CRITICAL(&lock);
        e.sequence = ++sequence; entries[head] = e; head = (head + 1) % Capacity;
        if (count < Capacity) ++count; else ++overwritten;
        portEXIT_CRITICAL(&lock);
    }
    void json(JsonDocument &out, uint32_t after = 0, size_t limit = 32) {
        std::vector<Entry> copy; copy.reserve(limit);
        uint32_t total, lost, oldestId;
        portENTER_CRITICAL(&lock);
        size_t oldest = (head + Capacity - count) % Capacity;
        oldestId = count ? entries[oldest].sequence : 0;
        for (size_t i = 0; i < count && copy.size() < limit; ++i) {
            const Entry &entry = entries[(oldest + i) % Capacity]; if (entry.sequence > after) copy.push_back(entry);
        }
        total = sequence; lost = overwritten;
        portEXIT_CRITICAL(&lock);
        out["capacity"] = Capacity; out["nextSequence"] = total; out["overwritten"] = lost; out["uptimeMs"] = millis();
        out["oldestSequence"] = oldestId;
        out["storage"] = "RAM";
        JsonArray rows = out["entries"].to<JsonArray>(); size_t added = 0;
        for (const Entry &e : copy) {
            if (e.sequence <= after || added >= limit) continue;
            ++added; JsonObject row = rows.add<JsonObject>();
            static const char *names[] = {"request","position","state","lamp","unknown_state","mqtt_queued","mqtt_failed","register"};
            row["sequence"] = e.sequence; row["ms"] = e.ms; row["event"] = names[e.kind];
            char hex[7]; snprintf(hex,sizeof(hex),"0x%04X",e.address); row["register"] = hex;
            snprintf(hex,sizeof(hex),"0x%04X",e.oldValue); row["previousRaw"] = hex;
            snprintf(hex,sizeof(hex),"0x%04X",e.value); row["raw"] = hex;
            row["currentRaw"] = e.value & 255; row["targetRaw"] = e.value >> 8;
            row["position"] = e.currentPercent; row["target"] = e.targetPercent; row["state"] = e.state;
            row["flags"] = e.flags; row["function"] = e.function; row["extra"] = e.extra;
        }
    }
};
inline BusLog busLog;

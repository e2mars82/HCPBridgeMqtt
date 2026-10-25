#pragma once
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <vector>
#include "flow_values.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <atomic>

// All execution and HTTP traffic runs on a low-priority worker, never on Modbus.
class WledFlows {
    class BoundedResponse : public Stream {
    public:
        String data;
        size_t write(uint8_t c) override { return write(&c, 1); }
        size_t write(const uint8_t *buffer, size_t size) override {
            if (data.length() + size > 8192) return 0;
            return data.concat(reinterpret_cast<const char*>(buffer), size) ? size : 0;
        }
        int available() override { return 0; }
        int read() override { return -1; }
        int peek() override { return -1; }
        void flush() override {}
    };
    struct DoorEvent { uint8_t state; uint32_t at; };
    QueueHandle_t doorEvents = nullptr;
    std::atomic<uint32_t> lostDoorEvents{0};
    String lastDoorEvent = "", busMessage = "Busdaten werden geprueft";
    bool busValid = false;
    int busAge = -1;
    struct Job { int id; uint32_t due; String context; };
    JsonDocument config, variables, winFields, winResult;
    uint32_t winRequestId = 0;
    String winRequestState = "idle", winRequestMessage = "Noch keine WLED-Abfrage";
    bool variablesDirty = false, inspectWin = false;
    uint32_t variableFlushAt = 0;
    Preferences store;
    SemaphoreHandle_t mutex = nullptr;
    std::vector<Job> jobs;
    String last = "Bereit", previous;
    bool seen = false, boot = false, manual = false, previousLight = false;
    uint32_t timerAt[24] = {}, pollAt = 0;
    using Snapshot = void (*)(JsonDocument&);
    Snapshot snapshot;
    bool (*auth)(AsyncWebServerRequest*);
    JsonObject node(int id) {
        for (JsonObject n : config["nodes"].as<JsonArray>()) if (n["id"].as<int>() == id) return n;
        return JsonObject();
    }
    JsonObject definition(const String &name) {
        for (JsonObject v : config["variables"].as<JsonArray>()) if (v["name"].as<String>() == name) return v;
        return JsonObject();
    }
    void initializeVariables(JsonDocument &previous) {
        variables.to<JsonObject>();
        for (JsonObject v : config["variables"].as<JsonArray>()) {
            String name = v["name"], type = v["type"];
            JsonVariant dest = variables[name].to<JsonVariant>();
            bool persistent = v["persist"] | true;
            if (persistent && previous[name]["type"].as<String>() == type && !previous[name]["value"].isNull()) {
                String text;
                if (previous[name]["value"].is<const char*>()) text = previous[name]["value"].as<String>();
                else serializeJson(previous[name]["value"], text);
                if (FlowValues::convert(text, type, dest)) continue;
            }
            FlowValues::convert(v["initial"].as<String>(), type, dest);
        }
    }
    void variableSnapshot(JsonDocument &out) {
        out.to<JsonObject>();
        for (JsonObject v : config["variables"].as<JsonArray>()) {
            String name = v["name"]; if (!(v["persist"] | true)) continue;
            out[name]["type"] = v["type"]; out[name]["value"] = variables[name];
        }
    }
    void flushVariables() {
        if (!variablesDirty || millis() - variableFlushAt < 5000) return;
        variableFlushAt = millis(); JsonDocument saved; variableSnapshot(saved); String data; serializeJson(saved, data);
        if (store.putString("vars", data) == data.length()) variablesDirty = false;
        else last = "Fehler: Variablenspeicher nicht geschrieben";
    }
    bool assign(const String &name, const String &text, JsonDocument &target) {
        JsonObject v = definition(name);
        if (v.isNull() || !FlowValues::convert(text, v["type"].as<String>(), target[name].to<JsonVariant>())) {
            last = "Fehler: Wert passt nicht zu Variable " + name; return false;
        }
        return true;
    }
    void commitVariables(JsonDocument &updated) {
        bool changed = false;
        for (JsonPair kv : updated.as<JsonObject>()) {
            if (variables[kv.key()] != kv.value()) {
                variables[kv.key()] = kv.value();
                if (definition(kv.key().c_str())["persist"] | true) changed = true;
            }
        }
        variablesDirty = variablesDirty || changed;
    }
    bool outputVariable(const String &name, JsonDocument &ctx) {
        JsonObject v = definition(name);
        if (v.isNull()) { last = "Fehler: Unbekannte Variable " + name; return false; }
        ctx["input"]["name"] = name; ctx["input"]["type"] = v["type"]; ctx["input"]["value"] = variables[name];
        return true;
    }
    JsonVariant lookup(JsonDocument &ctx, String path) {
        JsonVariant value = ctx.as<JsonVariant>();
        if (path.startsWith("vars.")) { value = variables.as<JsonVariant>(); path = path.substring(5); }
        while (path.length()) { int dot = path.indexOf('.'); String part = dot < 0 ? path : path.substring(0, dot); value = value[part]; if (dot < 0) break; path = path.substring(dot + 1); }
        return value;
    }
    bool readWin() {
        if (!WiFi.isConnected()) { last = "Fehler: WLAN nicht verbunden"; return false; }
        WiFiClient client; HTTPClient h; h.setConnectTimeout(1500); h.setTimeout(2000);
        if (!h.begin(client, "http://" + config["host"].as<String>() + "/win")) { last = "Fehler: HTTP Start"; return false; }
        int code = h.GET(); BoundedResponse response;
        if (code < 200 || code >= 300) {
            last = "WLED /win: HTTP " + String(code);
            if (code < 0) last += " (" + HTTPClient::errorToString(code) + ")";
            h.end(); return false;
        }
        if (h.getSize() > 8192) { h.end(); last = "WLED /win: Antwort groesser als 8192 Byte"; return false; }
        int received = h.writeToStream(&response);
        if (received < 0) { h.end(); last = "WLED /win: Lesefehler " + String(received) + " (" + HTTPClient::errorToString(received) + ")"; return false; }
        if (!response.data.length()) { h.end(); last = "WLED /win: Leere HTTP-Antwort"; return false; }
        h.end(); JsonDocument parsed; String error;
        if (!FlowValues::parseWin(response.data, parsed, error)) { last = "Fehler: " + error; return false; }
        winFields.set(parsed); last = "WLED /win gelesen"; return true;
    }
    bool http(bool write, JsonDocument &body, const String &query = "") {
        if (!WiFi.isConnected()) { last = "Fehler: WLAN nicht verbunden"; return false; }
        WiFiClient client;
        HTTPClient h;
        h.setConnectTimeout(750); h.setTimeout(750);
        String host = config["host"].as<String>();
        if (!h.begin(client, "http://" + host + (query.length() ? "/win&" + query : String("/json/state")))) { last = "Fehler: HTTP Start"; return false; }
        int code;
        if (write) {
            String payload; serializeJson(body, payload);
            h.addHeader("Content-Type", "application/json"); code = h.POST(payload);
        } else code = h.GET();
        bool ok = code >= 200 && code < 300;
        if (ok && !write && !query.length()) {
            // HTTPClient decodes chunked transfer into a strictly bounded sink.
            BoundedResponse response;
            if (h.getSize() > 8192 || h.writeToStream(&response) < 0) ok = false;
            else ok = !deserializeJson(body, response.data) && body.is<JsonObject>();
            if (!ok) { h.end(); last = "Fehler: WLED-Status ungueltig oder groesser als 8192 Byte"; return false; }
        }
        last = ok ? "WLED OK (HTTP " + String(code) + ")" : "WLED Fehler (HTTP " + String(code) + ")";
        h.end(); return ok;
    }
    void enqueue(int id, const String &ctx, uint32_t due) {
        if (jobs.size() >= 16) { last = "Fehler: Ablaufwarteschlange voll"; return; }
        jobs.push_back({id, due, ctx});
    }
    void next(int id, int port, JsonDocument &ctx, uint32_t due) {
        String data; serializeJson(ctx, data);
        if (data.length() > 4096) { last = "Fehler: Statusdaten zu gross"; return; }
        for (JsonObject e : config["edges"].as<JsonArray>())
            if (e["from"].as<int>() == id && (e["port"] | 0) == port) {
                JsonDocument outgoing; outgoing.set(ctx);
                if (e["input"] == "int") {
                    if (ctx["input"]["type"] != "int" || !ctx["input"]["value"].is<int>()) { last = "Fehler: int-Eingang ohne int-Wert"; continue; }
                    outgoing["presetInput"] = ctx["input"]["value"];
                } else outgoing.remove("presetInput");
                String serialized; serializeJson(outgoing, serialized);
                if (serialized.length() <= 4096) enqueue(e["to"], serialized, due);
                else last = "Fehler: Statusdaten zu gross";
            }
    }
    void trigger(const String &event) {
        for (JsonObject n : config["nodes"].as<JsonArray>()) {
            if (n["type"] == "trigger" && n["event"].as<String>() == event) {
                JsonDocument ctx; ctx["event"] = event;
                next(n["id"], 0, ctx, millis());
            }
        }
    }
    void tick() {
        flushVariables();
        if (inspectWin) {
            inspectWin = false;
            bool ok = readWin();
            winRequestState = ok ? "done" : "error"; winRequestMessage = last;
            if (ok) winResult.set(winFields); else winResult.clear();
            return;
        }
        uint32_t now = millis();
        bool active = (config["enabled"] | false) && WiFi.isConnected();
        DoorEvent event;
        while (doorEvents && xQueueReceive(doorEvents, &event, 0) == pdTRUE) {
            if (!active || now - event.at > 10000) continue;
            static const char *names[] = {"open", "opening", "closed", "closing", "open h", "opening v", "venting", "opening h", "stopped"};
            if (event.state < sizeof(names) / sizeof(names[0])) { lastDoorEvent = names[event.state]; trigger(lastDoorEvent); trigger("change"); }
        }
        if (now - pollAt >= 250) {
            pollAt = now; JsonDocument state; snapshot(state);
            busValid = state["valid"] | false; busAge = state["busResponseAge"] | -1;
            busMessage = busValid ? "Bus aktiv" : (busAge < 0 ? "Kein Buspaket seit Start: Board, RS485-Pins und Busanmeldung pruefen" : "Busdaten veraltet: keine zuverlaessige Torerkennung");
            if (active && busValid) {
                bool light = state["light"] | false;
                if (seen && light != previousLight) trigger(light ? "light_on" : "light_off");
                previousLight = light; seen = true;
            } else seen = false;
        }
        if (!active) return;
        if (!boot) { boot = true; trigger("boot"); }
        if (manual) { manual = false; trigger("manual"); }
        int i = 0;
        for (JsonObject n : config["nodes"].as<JsonArray>()) {
            if (n["type"] == "trigger" && n["event"] == "timer") {
                uint32_t interval = n["ms"] | 10000;
                if (now - timerAt[i] >= interval) {
                    timerAt[i] = now; JsonDocument ctx; ctx["event"] = "timer"; next(n["id"], 0, ctx, now);
                }
            }
            ++i;
        }
        // One job per tick, with bounded fan-out and acyclic graphs.
        for (size_t j = 0; j < jobs.size(); ++j) {
            if (int32_t(now - jobs[j].due) < 0) continue;
            Job job = jobs[j]; jobs.erase(jobs.begin() + j);
            JsonObject n = node(job.id); if (n.isNull()) return;
            JsonDocument ctx; deserializeJson(ctx, job.context);
            String type = n["type"].as<String>(); int port = 0;
            if (type == "bridge") { JsonDocument s; snapshot(s); ctx["bridge"] = s.as<JsonVariant>(); }
            else if (type == "read") { JsonDocument s; if (!http(false, s)) return; ctx["wled"] = s.as<JsonVariant>(); }
            else if (type == "setvar") {
                String value = n["value"].as<String>();
                if (n["from"] == "field") {
                    JsonVariant current = lookup(ctx, n["field"].as<String>());
                    if (current.isNull()) { last = "Fehler: Statusfeld fuer Variable fehlt"; return; }
                    value = ""; if (current.is<const char*>()) value = current.as<String>(); else serializeJson(current, value);
                }
                JsonDocument updated; if (!assign(n["variable"].as<String>(), value, updated)) return;
                commitVariables(updated); if (!outputVariable(n["variable"].as<String>(), ctx)) return;
            } else if (type == "getvar") {
                if (!outputVariable(n["variable"].as<String>(), ctx)) return;
            } else if (type == "winvar") {
                if (!readWin()) return;
                JsonDocument updated;
                for (JsonObject m : n["mappings"].as<JsonArray>()) {
                    String field = m["field"]; int index = m["index"] | 0;
                    JsonArray entries = winFields[field].as<JsonArray>();
                    if (entries.isNull() || index >= int(entries.size())) { last = "Fehler: /win Feld fehlt: " + field + "[" + String(index) + "]"; return; }
                    if (!assign(m["variable"].as<String>(), entries[index].as<String>(), updated)) return;
                }
                commitVariables(updated);
                if (!outputVariable(n["variable"].as<String>(), ctx)) return;
            } else if (type == "http" || type == "preset") {
                JsonDocument unused;
                int preset = n["preset"] | 0;
                if (type == "preset" && n["source"] == "input") {
                    if (!ctx["presetInput"].is<int>()) { last = "Fehler: Preset benoetigt den verbundenen int-Eingang"; return; }
                    preset = ctx["presetInput"];
                }
                if (type == "preset" && (preset < 1 || preset > 250)) { last = "Fehler: Preset ausserhalb 1 bis 250"; return; }
                String query = type == "preset" ? "PL=" + String(preset) : n["query"].as<String>();
                if (!http(false, unused, query)) return;
            } else if (type == "write") {
                JsonDocument payload;
                if (deserializeJson(payload, n["body"].as<String>())) { last = "Fehler: JSON"; return; }
                if (!http(true, payload)) return;
            } else if (type == "condition") {
                JsonVariant value = lookup(ctx, n["field"].as<String>());
                String actual; if (value.is<const char*>()) actual = value.as<String>(); else serializeJson(value, actual);
                String op = n["op"].as<String>(), expected = n["value"].as<String>();
                bool result = false;
                if (!value.isNull()) {
                    if (op == "eq") result = actual == expected;
                    else if (op == "ne") result = actual != expected;
                    else if (value.is<float>() || value.is<int>()) result = op == "gt" ? value.as<float>() > expected.toFloat() : value.as<float>() < expected.toFloat();
                }
                port = result ? 0 : 1;
            }
            next(job.id, port, ctx, millis() + (type == "delay" ? n["ms"].as<uint32_t>() : 0)); return;
        }
    }
public:
    void captureDoorState(uint8_t state) {
        if (!doorEvents) return;
        DoorEvent event{state, millis()};
        if (xQueueSend(doorEvents, &event, 0) != pdTRUE) ++lostDoorEvents;
    }
    static String validate(JsonDocument &d) {
        IPAddress ip;
        String host = d["host"] | "";
        if (!d["enabled"].is<bool>() || !d["nodes"].is<JsonArray>() || !d["edges"].is<JsonArray>()) return "Ungueltiges Format";
        if ((d["enabled"] == true || host.length()) && (!ip.fromString(host) || ip[0] == 0 || ip[0] >= 224 || ip[3] == 255)) return "IPv4-Adresse erforderlich";
        if (!d["variables"].isNull() && !d["variables"].is<JsonArray>()) return "Ungueltige Variablenliste";
        JsonArray vs = d["variables"].as<JsonArray>();
        if (vs.size() > 24) return "Maximal 24 Variablen";
        for (JsonObject v : vs) {
            String name = v["name"] | "", type = v["type"] | "";
            if (!FlowValues::identifier(name) || (type != "string" && type != "int" && type != "bool") || !v["initial"].is<String>() || (!v["persist"].isNull() && !v["persist"].is<bool>())) return "Ungueltige Variablendefinition";
            int copies = 0; for (JsonObject other : vs) if (other["name"].as<String>() == name) ++copies;
            if (copies != 1) return "Doppelter Variablenname";
            JsonDocument converted; if (!FlowValues::convert(v["initial"].as<String>(), type, converted.to<JsonVariant>())) return "Ungueltiger Variablen-Anfangswert";
        }
        auto findVar = [&](const String &name) -> JsonObject { for (JsonObject v : vs) if (v["name"].as<String>() == name) return v; return JsonObject(); };
        JsonArray ns = d["nodes"], es = d["edges"];
        if (ns.size() > 24 || es.size() > 48) return "Maximal 24 Bloecke und 48 Verbindungen";
        int ids[24], degree[24] = {}; int count = 0;
        for (JsonObject n : ns) {
            if (!n["id"].is<int>() || n["id"].as<int>() < 1) return "Ungueltige Block-ID";
            if (!n["x"].is<float>() || !n["y"].is<float>() || n["x"].as<float>() < 0 || n["x"].as<float>() > 2200 || n["y"].as<float>() < 0 || n["y"].as<float>() > 1500) return "Ungueltige Blockposition";
            int id = n["id"]; for (int k = 0; k < count; ++k) if (ids[k] == id) return "Doppelte Block-ID";
            ids[count++] = id;
            String t = n["type"] | "";
            if (t != "trigger" && t != "bridge" && t != "read" && t != "condition" && t != "delay" && t != "preset" && t != "write" && t != "http" && t != "getvar" && t != "setvar" && t != "winvar") return "Unbekannter Block";
            if (t == "trigger") { String e = n["event"] | ""; if (e != "light_on" && e != "light_off" && e != "manual" && e != "boot" && e != "timer" && e != "change" && e != "open" && e != "closed" && e != "opening" && e != "closing" && e != "opening v" && e != "opening h" && e != "open h" && e != "venting" && e != "stopped") return "Unbekannter Trigger"; }
            if (t == "delay" || (t == "trigger" && n["event"] == "timer")) {
                if (!n["ms"].is<unsigned long>() || n["ms"].as<unsigned long>() > 3600000 || n["ms"].as<unsigned long>() < (t == "delay" ? 0UL : 1000UL)) return "Ungueltige Zeit (Timer ab 1000 ms, maximal 3600000 ms)";
            }
            if (t == "preset") {
                if (!n["source"].isNull() && n["source"] != "literal" && n["source"] != "input") return "Ungueltige Presetquelle";
                if (n["source"] != "input" && (!n["preset"].is<int>() || n["preset"].as<int>() < 1 || n["preset"].as<int>() > 250)) return "Preset muss 1 bis 250 sein";
                if (n["source"] == "input") { int inputs = 0; for (JsonObject e : es) if (e["to"] == n["id"] && e["input"] == "int") ++inputs; if (!inputs) return "Preset: int-Eingang verbinden"; }
            }
            if (t == "getvar" || t == "setvar" || t == "winvar") {
                JsonObject v = findVar(n["variable"].as<String>());
                if (v.isNull()) return "Block verweist auf unbekannte Variable";
                if (t == "setvar") {
                    if (n["from"] != "literal" && n["from"] != "field") return "Ungueltige Variablenquelle";
                    if (n["from"] == "field" && (!n["field"].is<String>() || !n["field"].as<String>().length())) return "Statusfeld fehlt";
                    if (n["from"] == "literal") { JsonDocument converted; if (!n["value"].is<String>() || !FlowValues::convert(n["value"].as<String>(), v["type"].as<String>(), converted.to<JsonVariant>())) return "Wert passt nicht zum Variablentyp"; }
                }
                if (t == "winvar") {
                    if (!n["mappings"].is<JsonArray>() || !n["mappings"].size() || n["mappings"].size() > 24) return "WLED-Zuordnungen fehlen (maximal 24)";
                    bool outputMapped = false;
                    for (JsonObject m : n["mappings"].as<JsonArray>()) {
                        String field = m["field"] | "";
                        if (!field.length() || field.length() > 96 || !m["index"].is<int>() || m["index"].as<int>() < 0 || m["index"].as<int>() > 127 || findVar(m["variable"].as<String>()).isNull()) return "Ungueltige WLED-Zuordnung";
                        if (m["variable"].as<String>() == n["variable"].as<String>()) outputMapped = true;
                        int copies = 0; for (JsonObject other : n["mappings"].as<JsonArray>()) if (other["variable"] == m["variable"]) ++copies;
                        if (copies > 1) return "WLED-Zielvariable doppelt zugeordnet";
                    }
                    if (!outputMapped) return "WLED-Ausgangsvariable muss zugeordnet sein";
                }
            }
            if (t == "http") {
                String q = n["query"] | "";
                if (!q.length() || q.length() > 512) return "HTTP-Parameter fehlen oder sind zu lang";
                for (size_t c = 0; c < q.length(); ++c) if (!isalnum((unsigned char)q[c]) && String("&=~,-._%").indexOf(q[c]) < 0) return "Ungueltige HTTP-Parameter";
            }
            if (t == "write") { JsonDocument p; String s = n["body"] | ""; if (s.length() > 1024 || deserializeJson(p, s) || !p.is<JsonObject>()) return "API-Text muss ein JSON-Objekt sein (max. 1024 Zeichen)"; }
            if (t == "condition") { String op = n["op"] | ""; if (!n["field"].is<String>() || !n["value"].is<String>() || (op != "eq" && op != "ne" && op != "gt" && op != "lt")) return "Ungueltige Bedingung"; }
        }
        for (JsonObject e : es) {
            if (!e["from"].is<int>() || !e["to"].is<int>() || !e["port"].is<int>()) return "Ungueltige Verbindung";
            int a = -1, b = -1;
            for (int k = 0; k < count; ++k) { if (ids[k] == e["from"].as<int>()) a = k; if (ids[k] == e["to"].as<int>()) b = k; }
            if (a < 0 || b < 0 || a == b) return "Ungueltige Verbindung";
            String at = ns[a]["type"] | "", bt = ns[b]["type"] | ""; int port = e["port"];
            if (bt == "trigger" || port < 0 || port > (at == "condition" ? 1 : 0)) return "Ungueltiger Anschluss";
            String input = e["input"] | "flow";
            if (input != "flow" && input != "int") return "Unbekannter Eingang";
            if (input == "int") {
                JsonObject variable = findVar(ns[a]["variable"].as<String>());
                if (bt != "preset" || ns[b]["source"] != "input" || (at != "getvar" && at != "setvar" && at != "winvar") || variable.isNull() || variable["type"] != "int") return "int-Eingang erfordert einen int-Variablenausgang";
            }
            int copies = 0;
            for (JsonObject other : es)
                if (other["from"] == e["from"] && other["to"] == e["to"] && other["port"] == e["port"] && String(other["input"] | "flow") == input) ++copies;
            if (copies > 1) return "Doppelte Verbindung";
            ++degree[b];
        }
        for (int step = 0; step < count; ++step) {
            int a = -1; for (int k = 0; k < count; ++k) if (degree[k] == 0) { a = k; break; }
            if (a < 0) return "Kreise sind nicht erlaubt; Timer-Trigger verwenden";
            degree[a] = -1;
            for (JsonObject e : es) if (e["from"].as<int>() == ids[a]) for (int k = 0; k < count; ++k) if (ids[k] == e["to"].as<int>()) --degree[k];
        }
        return "";
    }
    void begin(AsyncWebServer &server, bool (*authenticate)(AsyncWebServerRequest*), Snapshot getSnapshot) {
        auth = authenticate; snapshot = getSnapshot; mutex = xSemaphoreCreateMutex(); doorEvents = xQueueCreate(32, sizeof(DoorEvent));
        store.begin("wledflows", false);
        String saved = store.getString("config", "");
        if (deserializeJson(config, saved) || validate(config).length()) deserializeJson(config, "{\"enabled\":false,\"host\":\"\",\"nodes\":[],\"edges\":[]}");
        JsonDocument savedVariables; deserializeJson(savedVariables, store.getString("vars", "{}")); initializeVariables(savedVariables);
        server.on(AsyncURIMatcher::exact("/api/flows/win"), HTTP_POST, [this](AsyncWebServerRequest *r) {
            if (!auth(r)) return;
            if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) != pdTRUE) { r->send(503, "text/plain", "Bridge beschaeftigt, erneut versuchen"); return; }
            if (!config["host"].as<String>().length()) { xSemaphoreGive(mutex); r->send(409, "text/plain", "WLED-IP zuerst speichern"); return; }
            if (winRequestState == "pending") { xSemaphoreGive(mutex); r->send(409, "text/plain", "WLED-Abfrage laeuft bereits"); return; }
            ++winRequestId; inspectWin = true; winResult.clear();
            winRequestState = "pending"; winRequestMessage = "WLED /win wird gelesen";
            JsonDocument d; d["id"] = winRequestId; d["state"] = winRequestState; String payload; serializeJson(d, payload);
            xSemaphoreGive(mutex);
            auto response = r->beginResponse(202, "application/json", payload); response->addHeader("Cache-Control", "no-store"); r->send(response);
        });
        server.on(AsyncURIMatcher::exact("/api/flows/win"), HTTP_GET, [this](AsyncWebServerRequest *r) {
            if (!auth(r)) return;
            if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) != pdTRUE) { r->send(503, "text/plain", "WLED-Abfrage laeuft, bitte warten"); return; }
            JsonDocument d; d["id"] = winRequestId; d["state"] = winRequestState;
            d["message"] = winRequestMessage; d["host"] = config["host"];
            if (winRequestState == "done") d["fields"] = winResult.as<JsonVariant>();
            String payload; serializeJson(d, payload); xSemaphoreGive(mutex);
            auto response = r->beginResponse(200, "application/json", payload); response->addHeader("Cache-Control", "no-store"); r->send(response);
        });
        server.on(AsyncURIMatcher::exact("/api/flows"), HTTP_GET, [this](AsyncWebServerRequest *r) {
            if (!auth(r)) return;
            if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) != pdTRUE) { r->send(503, "text/plain", "Beschaeftigt, erneut versuchen"); return; }
            String s; serializeJson(config, s); xSemaphoreGive(mutex); r->send(200, "application/json", s);
        });
        auto handler = new AsyncCallbackJsonWebHandler(AsyncURIMatcher::exact("/api/flows"), [this](AsyncWebServerRequest *r, JsonVariant &json) {
            if (!auth(r)) return;
            JsonDocument d; d.set(json); String error = validate(d);
            if (error.length()) { r->send(400, "text/plain", error); return; }
            String s; serializeJson(d, s);
            if (s.length() > 12288) { r->send(413, "text/plain", "Ablauf zu gross"); return; }
            if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) != pdTRUE) { r->send(503, "text/plain", "Beschaeftigt, erneut versuchen"); return; }
            bool ok = store.putString("config", s) == s.length();
            if (ok) { JsonDocument previousValues; variableSnapshot(previousValues); config.set(d); initializeVariables(previousValues); variablesDirty = true; winFields.clear(); winResult.clear(); inspectWin = false; winRequestState = "idle"; winRequestMessage = "Konfiguration geaendert; erneut abfragen"; if (doorEvents) xQueueReset(doorEvents); jobs.clear(); manual = false; seen = false; boot = false; for (auto &t : timerAt) t = millis(); last = "Gespeichert"; }
            xSemaphoreGive(mutex); r->send(ok ? 200 : 500, "text/plain", ok ? "Gespeichert" : "Speicherfehler");
        });
        handler->setMethod(HTTP_POST); handler->setMaxContentLength(12288); server.addHandler(handler);
        server.on(AsyncURIMatcher::exact("/api/flows/run"), HTTP_POST, [this](AsyncWebServerRequest *r) {
            if (!auth(r)) return;
            if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) != pdTRUE) { r->send(503, "text/plain", "Beschaeftigt"); return; }
            bool enabled = config["enabled"] | false; if (enabled) manual = true;
            xSemaphoreGive(mutex); r->send(enabled ? 202 : 409, "text/plain", enabled ? "Manueller Trigger vorgemerkt" : "Ablauf ist deaktiviert");
        });
        server.on(AsyncURIMatcher::exact("/api/flows/status"), HTTP_GET, [this](AsyncWebServerRequest *r) {
            if (!auth(r)) return;
            if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) != pdTRUE) { r->send(503, "text/plain", "Beschaeftigt"); return; }
            JsonDocument d; d["message"] = last; d["busValid"] = busValid; d["busAge"] = busAge; d["busMessage"] = busMessage; d["lastDoorEvent"] = lastDoorEvent; d["lostDoorEvents"] = lostDoorEvents.load(); d["queued"] = jobs.size(); d["variables"] = variables.as<JsonVariant>(); d["winFields"] = winFields.as<JsonVariant>(); String s; serializeJson(d, s);
            xSemaphoreGive(mutex); r->send(200, "application/json", s);
        });
        xTaskCreate([](void *p) { auto self = static_cast<WledFlows*>(p); for (;;) { if (xSemaphoreTake(self->mutex, pdMS_TO_TICKS(50)) == pdTRUE) { self->tick(); xSemaphoreGive(self->mutex); } vTaskDelay(pdMS_TO_TICKS(25)); } }, "WledFlows", 10000, this, 1, nullptr);
    }
};

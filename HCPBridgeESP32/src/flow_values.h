#pragma once
#include <ArduinoJson.h>
#include <Arduino.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <vector>

namespace FlowValues {
inline bool identifier(const String &s) {
    if (!s.length() || s.length() > 24 || (!isalpha((unsigned char)s[0]) && s[0] != '_')) return false;
    for (size_t i = 1; i < s.length(); ++i) if (!isalnum((unsigned char)s[i]) && s[i] != '_') return false;
    return true;
}
inline bool convert(const String &text, const String &type, JsonVariant out) {
    if (type == "string") { if (text.length() > 256) return false; out.set(text); return true; }
    String s = text; s.trim();
    if (type == "bool") {
        s.toLowerCase();
        if (s == "true" || s == "1") { out.set(true); return true; }
        if (s == "false" || s == "0") { out.set(false); return true; }
        return false;
    }
    if (type != "int" || !s.length()) return false;
    size_t pos = s[0] == '-' || s[0] == '+' ? 1 : 0;
    if (pos == s.length()) return false;
    for (size_t i = pos; i < s.length(); ++i) if (!isdigit((unsigned char)s[i])) return false;
    errno = 0; char *end = nullptr; long long v = strtoll(s.c_str(), &end, 10);
    if (errno || !end || *end || v < INT32_MIN || v > INT32_MAX) return false;
    out.set(int32_t(v)); return true;
}
inline bool utf8(String &out, uint32_t cp) {
    if (!cp || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xc0 | (cp >> 6)); out += char(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { out += char(0xe0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 63)); out += char(0x80 | (cp & 63)); }
    else { out += char(0xf0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 63)); out += char(0x80 | ((cp >> 6) & 63)); out += char(0x80 | (cp & 63)); }
    return true;
}
inline bool decode(const String &in, String &out) {
    out = "";
    for (size_t i = 0; i < in.length(); ++i) {
        if (in[i] != '&') { out += in[i]; continue; }
        int end = in.indexOf(';', i + 1); if (end < 0 || end - int(i) > 12) return false;
        String e = in.substring(i + 1, end);
        if (e == "amp") out += '&'; else if (e == "lt") out += '<'; else if (e == "gt") out += '>';
        else if (e == "quot") out += '"'; else if (e == "apos") out += '\'';
        else if (e.startsWith("#")) {
            bool hex = e.length() > 2 && (e[1] == 'x' || e[1] == 'X');
            String digits = e.substring(hex ? 2 : 1); if (!digits.length()) return false;
            for (size_t k = 0; k < digits.length(); ++k) if (hex ? !isxdigit((unsigned char)digits[k]) : !isdigit((unsigned char)digits[k])) return false;
            char *stop = nullptr; unsigned long cp = strtoul(digits.c_str(), &stop, hex ? 16 : 10);
            if (!stop || *stop || !utf8(out, cp)) return false;
        } else return false;
        i = end;
    }
    return out.length() <= 256;
}
// Small bounded XML leaf reader. No DTDs, external entities or expansion.
// Repeated elements (e.g. <cl>R</cl><cl>G</cl><cl>B</cl>) retain their order.
inline bool parseWin(const String &xml, JsonDocument &fields, String &error) {
    struct Element { String name, text; bool child = false; };
    std::vector<Element> stack; fields.to<JsonObject>();
    bool root = false, closed = false; unsigned leaves = 0; int p = 0;
    auto fail = [&](const char *s) { error = s; fields.clear(); return false; };
    while (p < int(xml.length())) {
        if (xml[p] != '<') {
            int end = xml.indexOf('<', p); if (end < 0) end = xml.length();
            String text = xml.substring(p, end);
            if (stack.empty()) { text.trim(); if (text.length()) return fail("Text ausserhalb des XML-Wurzelelements"); }
            else { stack.back().text += text; if (stack.back().text.length() > 1024) return fail("XML-Wert zu lang"); }
            p = end; continue;
        }
        if (xml.substring(p, p + 4) == "<!--") { int e = xml.indexOf("-->", p + 4); if (e < 0) return fail("XML-Kommentar unvollstaendig"); p = e + 3; continue; }
        if (xml.substring(p, p + 2) == "<?") { int e = xml.indexOf("?>", p + 2); if (e < 0) return fail("XML-Deklaration unvollstaendig"); p = e + 2; continue; }
        if (xml.substring(p, p + 2) == "<!") return fail("Nicht unterstuetzte XML-Deklaration");
        int end = p + 1; char quote = 0;
        for (; end < int(xml.length()); ++end) { char c = xml[end]; if (quote) { if (c == quote) quote = 0; } else if (c == '\'' || c == '"') quote = c; else if (c == '>') break; }
        if (end == int(xml.length())) return fail("XML-Tag unvollstaendig");
        String tag = xml.substring(p + 1, end); tag.trim(); p = end + 1;
        bool closing = tag.startsWith("/"), selfClosing = tag.endsWith("/");
        if (closing) {
            String name = tag.substring(1); name.trim();
            if (stack.empty() || name != stack.back().name) return fail("XML-Tags passen nicht zusammen");
            if (!stack.back().child && stack.size() > 1) {
                String key;
                for (size_t k = 1; k < stack.size(); ++k) { if (key.length()) key += '.'; key += stack[k].name; }
                String text; if (!decode(stack.back().text, text)) return fail("XML-Wert oder Zeichenreferenz ungueltig");
                if (++leaves > 128 || (fields[key].isNull() && fields.size() >= 64)) return fail("Zu viele XML-Felder");
                if (fields[key].isNull()) fields[key].to<JsonArray>();
                fields[key].as<JsonArray>().add(text);
            }
            stack.pop_back(); if (stack.empty()) closed = true;
        } else {
            int len = 0; while (len < int(tag.length()) && !isspace((unsigned char)tag[len]) && tag[len] != '/') ++len;
            String name = tag.substring(0, len);
            if (!identifier(name) || stack.size() >= 8 || closed) return fail("XML-Element ungueltig");
            if (stack.empty()) { if (root || name != "vs") return fail("Keine WLED-/win-Antwort (Wurzel vs fehlt)"); root = true; }
            else stack.back().child = true;
            if (selfClosing) {
                if (stack.empty()) return fail("Leere WLED-Antwort");
                // Empty leaf values can be stored as strings.
                String key; for (size_t k = 1; k < stack.size(); ++k) { if (key.length()) key += '.'; key += stack[k].name; }
                if (key.length()) key += '.'; key += name;
                if (++leaves > 128 || (fields[key].isNull() && fields.size() >= 64)) return fail("Zu viele XML-Felder");
                if (fields[key].isNull()) fields[key].to<JsonArray>(); fields[key].as<JsonArray>().add("");
            } else stack.push_back({name, "", false});
        }
    }
    if (!root || !closed || !stack.empty() || !leaves) return fail("WLED-XML leer oder unvollstaendig");
    error = ""; return true;
}
}

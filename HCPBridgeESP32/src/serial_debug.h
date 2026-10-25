#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_log.h>
#include <stdarg.h>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
extern bool debugEnabled;
class SerialDebugOutput : public Print {
    struct Line { uint32_t sequence, ms; char text[161]; bool truncated; };
    static constexpr size_t Capacity = 96;
    Line lines[Capacity] = {};
    char pending[161] = {}; size_t length = 0, head = 0, count = 0;
    bool truncated = false;
    uint32_t sequence = 0, overwritten = 0;
    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    vprintf_like_t previous = nullptr;
public:
    void capture(const char *data, size_t size) {
        if (!debugEnabled) return;
        portENTER_CRITICAL(&lock);
        for (size_t i=0;i<size;++i) {
            char c=data[i]; if(c=='\r')continue;
            if(c=='\n') {
                Line &line=lines[head]; line.sequence=++sequence; line.ms=millis(); line.truncated=truncated;
                memcpy(line.text,pending,length); line.text[length]=0;
                head=(head+1)%Capacity;if(count<Capacity)++count;else ++overwritten;
                length=0;pending[0]=0;truncated=false;
            } else if(length<160){pending[length++]=c;pending[length]=0;}else truncated=true;
        }
        portEXIT_CRITICAL(&lock);
    }
    size_t write(uint8_t c) override { return write(&c,1); }
    size_t write(const uint8_t *data,size_t size) override {
        size_t written=Serial.write(data,size);capture(reinterpret_cast<const char*>(data),size);return written;
    }
    void begin();
    int log(const char *format, va_list args) {
        char data[256];va_list copy;va_copy(copy,args);int length=vsnprintf(data,sizeof(data),format,copy);va_end(copy);
        if(length>0){capture(data,size_t(length)<sizeof(data)?size_t(length):sizeof(data)-1);if(size_t(length)>=sizeof(data))capture("\n",1);}
        return previous?previous(format,args):length;
    }
    void json(JsonDocument &out,uint32_t after) {
        std::vector<Line> copy;copy.reserve(32);uint32_t total,lost,oldestId;char fragment[161];
        portENTER_CRITICAL(&lock);
        size_t oldest=(head+Capacity-count)%Capacity;oldestId=count?lines[oldest].sequence:0;
        for(size_t i=0;i<count&&copy.size()<32;++i){const Line &line=lines[(oldest+i)%Capacity];if(line.sequence>after)copy.push_back(line);}
        total=sequence;lost=overwritten;memcpy(fragment,pending,sizeof(fragment));
        portEXIT_CRITICAL(&lock);
        out["enabled"]=debugEnabled;out["capacity"]=Capacity;out["nextSequence"]=total;out["oldestSequence"]=oldestId;out["overwritten"]=lost;out["uptimeMs"]=millis();out["storage"]="RAM";out["pending"]=fragment;
        JsonArray rows=out["entries"].to<JsonArray>();for(const Line &line:copy){JsonObject row=rows.add<JsonObject>();row["sequence"]=line.sequence;row["ms"]=line.ms;row["text"]=line.text;row["truncated"]=line.truncated;}
    }
};
inline SerialDebugOutput serialDebug;
inline int serialLogHook(const char *format,va_list args){return serialDebug.log(format,args);}
inline void SerialDebugOutput::begin(){previous=esp_log_set_vprintf(serialLogHook);if(debugEnabled)esp_log_level_set("*",ESP_LOG_INFO);}

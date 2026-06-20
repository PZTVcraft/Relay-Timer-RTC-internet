#include <ESP8266WiFi.h>
#include <WiFiManager.h> 
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h> 
#include <time.h>
#include <RtcDS1302.h>
#include <EEPROM.h>

#define WIFI_RESET_PIN D3

const int RELAY_PINS[4] = {D1, D2, D4, D0};

#define RELAY_ON LOW
#define RELAY_OFF HIGH

const char* www_username = "admin";
const char* www_password = "ovt69";

struct TimerSlot {
    uint8_t active; 
    uint8_t on_h, on_m, off_h, off_m;
};

struct RelayConfig {
    char name[48]; 
    uint8_t mode;  
    uint8_t state; 
    TimerSlot t1;
    TimerSlot t2;
};

struct AppConfig {
    RelayConfig r[4];
} config;

ThreeWire myWire(D6, D5, D7); 
RtcDS1302<ThreeWire> Rtc(myWire);
ESP8266WebServer server(80); 

const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;
bool isRelayActive[4] = {false, false, false, false};
unsigned long lastRtcSync = 0; // ตัวแปรเก็บเวลาอัปเดต RTC

// ==============================================================================
// หน้าเว็บ HTML + JS
// ==============================================================================
const char PAGE_HTML[] PROGMEM = R"=====(
<!DOCTYPE html><html lang='th'><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1.0'>
<title>Smart Timer Control</title>
<style>
  body { font-family: 'Segoe UI', sans-serif; background: #f0f2f5; color: #333; margin: 0; padding: 15px; }
  .card { background: #ffffff; padding: 20px; border-radius: 12px; margin: 15px auto; max-width: 450px; box-shadow: 0 4px 12px rgba(0,0,0,0.08); }
  .logo-img { max-width: 150px; margin-bottom: 5px; } 
  h1 { color: #2c3e50; font-size: 3.5em; margin: 5px 0; text-align: center; }
  h2 { font-size: 1.3em; margin-top: 0; display: flex; justify-content: space-between; align-items: center; color: #2c3e50; border-bottom: 2px solid #00bcd4; padding-bottom: 10px; margin-bottom: 15px;}
  .relay-name { font-size: 1em; font-weight: bold; color: #2c3e50; border: none; background: transparent; border-bottom: 1px dashed #ccc; width: 140px; font-family: inherit; padding: 2px; }
  .relay-name:focus { outline: none; border-bottom: 1px solid #00bcd4; background: #f8f9fa;}
  .badge { padding: 5px 12px; border-radius: 20px; font-size: 0.7em; color: white; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
  .on { background: #4CAF50; } .off { background: #F44336; }
  .row { display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px; padding-bottom: 8px; border-bottom: 1px solid #eaeaea; }
  .sub-text { font-size: 0.75em; color: #888; font-weight: normal; }
  .switch { position: relative; display: inline-block; width: 46px; height: 24px; }
  .switch input { opacity: 0; width: 0; height: 0; }
  .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .3s; border-radius: 24px; }
  .slider:before { position: absolute; content: ""; height: 18px; width: 18px; left: 3px; bottom: 3px; background-color: white; transition: .3s; border-radius: 50%; box-shadow: 0 2px 5px rgba(0,0,0,0.2); }
  input:checked + .slider { background-color: #00bcd4; } 
  input:checked + .slider:before { transform: translateX(22px); }
  input:disabled + .slider { background-color: #e9ecef; cursor: not-allowed; }
  input:disabled + .slider:before { background-color: #f8f9fa; box-shadow: none; }
  input[type="time"] { background: #f8f9fa; color: #333; border: 1px solid #ced4da; padding: 6px; border-radius: 6px; font-size: 1.1em; width: 110px; text-align: center;}
  .time-box { display: flex; flex-direction: column; align-items: center; color: #555; font-weight: 600; font-size: 0.85em; }
  .loader { color: #666; text-align: center; font-size: 0.9em; }
  .timer-section { margin-top: 10px; padding: 10px; background: #fafafa; border-radius: 8px; border: 1px solid #eee; }
</style>
<script>
  window.onload = function() {
    renderRelays();
    fetch('/api/config').then(r=>r.json()).then(c=>{
      for(let i=0; i<4; i++) {
        document.getElementById('name'+i).value = c.r[i].n; 
        setUI(i, c.r[i]);
      }
      setInterval(pollStatus, 1000); 
    });
  }

  function renderRelays() {
    let html = '';
    for(let i=0; i<4; i++) {
      html += `<div class='card'>
        <h2>⚡ <input type='text' id='name${i}' class='relay-name' onchange='rename(${i})' placeholder='ตั้งชื่ออุปกรณ์...'> <span id='badge${i}' class='badge off'>--</span></h2>
        <div class='row'><span><b>โหมดการทำงาน:</b><br><span class='sub-text'>(ซ้าย=Auto, ขวา=Manual)</span></span>
        <label class='switch'><input type='checkbox' id='m${i}' onchange='autoSave(${i})'><span class='slider'></span></label></div>
        <div class='row'><span><b>บังคับเปิด/ปิด:</b><br><span class='sub-text'>(เฉพาะโหมด Manual)</span></span>
        <label class='switch'><input type='checkbox' id='s${i}' onchange='autoSave(${i})'><span class='slider'></span></label></div>
        
        <div class='timer-section'>
          <div class='row' style='border:none; margin-bottom:5px; padding-bottom:0;'>
            <span style='color:#00bcd4; font-weight:bold;'>🕒 รอบที่ 1 (เปิดใช้งาน)</span>
            <label class='switch'><input type='checkbox' id='t1a${i}' onchange='autoSave(${i})'><span class='slider'></span></label>
          </div>
          <div style='display:flex; justify-content:space-around;'>
            <div class='time-box'><span>เปิด (ON)</span><input type='time' id='t1on${i}' onchange='autoSave(${i})'></div>
            <div class='time-box'><span>ปิด (OFF)</span><input type='time' id='t1off${i}' onchange='autoSave(${i})'></div>
          </div>
        </div>

        <div class='timer-section'>
          <div class='row' style='border:none; margin-bottom:5px; padding-bottom:0;'>
            <span style='color:#00bcd4; font-weight:bold;'>🕒 รอบที่ 2 (เปิดใช้งาน)</span>
            <label class='switch'><input type='checkbox' id='t2a${i}' onchange='autoSave(${i})'><span class='slider'></span></label>
          </div>
          <div style='display:flex; justify-content:space-around;'>
            <div class='time-box'><span>เปิด (ON)</span><input type='time' id='t2on${i}' onchange='autoSave(${i})'></div>
            <div class='time-box'><span>ปิด (OFF)</span><input type='time' id='t2off${i}' onchange='autoSave(${i})'></div>
          </div>
        </div>
      </div>`;
    }
    document.getElementById('relays-container').innerHTML = html;
  }

  function setUI(i, d) {
    document.getElementById('m'+i).checked = (d.m == 1);
    document.getElementById('s'+i).checked = (d.s == 1);
    document.getElementById('t1a'+i).checked = (d.t1.a == 1);
    document.getElementById('t1on'+i).value = d.t1.on;
    document.getElementById('t1off'+i).value = d.t1.off;
    document.getElementById('t2a'+i).checked = (d.t2.a == 1);
    document.getElementById('t2on'+i).value = d.t2.on;
    document.getElementById('t2off'+i).value = d.t2.off;
    updateLocks(i);
  }

  function updateLocks(i) {
    let isManual = document.getElementById('m'+i).checked;
    document.getElementById('s'+i).disabled = !isManual;
    document.getElementById('t1a'+i).disabled = isManual;
    document.getElementById('t2a'+i).disabled = isManual;
  }

  function rename(i) {
    let n = document.getElementById('name'+i).value;
    fetch(`/api/rename?i=${i}&n=${encodeURIComponent(n)}`);
  }

  function autoSave(i) {
    updateLocks(i);
    let m = document.getElementById('m'+i).checked ? 1 : 0;
    let s = document.getElementById('s'+i).checked ? 1 : 0;
    let t1a = document.getElementById('t1a'+i).checked ? 1 : 0;
    let t1on = document.getElementById('t1on'+i).value;
    let t1off = document.getElementById('t1off'+i).value;
    let t2a = document.getElementById('t2a'+i).checked ? 1 : 0;
    let t2on = document.getElementById('t2on'+i).value;
    let t2off = document.getElementById('t2off'+i).value;
    fetch(`/api/update?i=${i}&m=${m}&s=${s}&t1a=${t1a}&t1on=${t1on}&t1off=${t1off}&t2a=${t2a}&t2on=${t2on}&t2off=${t2off}`);
  }

  function pollStatus() {
    fetch('/api/status').then(r=>r.json()).then(d=>{
      document.getElementById('clock').innerText = d.time;
      document.getElementById('date').innerText = "วันที่: " + d.date;
      document.getElementById('net').innerText = "แหล่งเวลา: " + d.net;
      for(let i=0; i<4; i++) {
        let badge = document.getElementById('badge'+i);
        if(badge) {
          badge.className = 'badge ' + (d.st[i] ? 'on' : 'off');
          badge.innerText = d.st[i] ? 'กำลังเปิด' : 'ปิดอยู่';
        }
      }
    });
  }
</script>
</head><body>

<div class='card' style='text-align:center;'>
  <img src="https://i.postimg.cc/3JkJ0L1m/470197430-3485369958437489-2974965790952699684-n-(1).jpg" alt="Logo" class="logo-img">
  <h1 id='clock'>--:--:--</h1>
  <div class='loader' id='date'>กำลังเชื่อมต่อ...</div>
  <div class='loader' id='net' style='color:#4CAF50; font-weight:bold; margin-top:5px;'></div>
</div>

<div id="relays-container"></div>

</body></html>
)=====";

// ==============================================================================
// ฟังก์ชัน "เวลาอัจฉริยะ" (เลือกระหว่าง อินเทอร์เน็ต NTP กับ ถ่าน RTC)
// ==============================================================================
RtcDateTime getAccurateTime() {
    time_t now_time = time(nullptr);
    
    // ถ้าระบบมีเวลามาตรฐาน (มากกว่าปี 2001) แปลว่าดึง NTP สำเร็จ ให้ใช้เวลาเน็ตเป็นหลัก
    if (now_time > 1000000000) {
        struct tm timeinfo;
        localtime_r(&now_time, &timeinfo);
        
        // ฟีเจอร์แถม: อัปเดตเวลาเน็ตลงโมดูล RTC วันละ 1 ครั้ง เพื่อให้ถ่านเวลาตรงเป๊ะตลอด
        if (millis() - lastRtcSync > 86400000 || lastRtcSync == 0) {
            Rtc.SetDateTime(RtcDateTime(timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
            lastRtcSync = millis();
        }
        
        return RtcDateTime(timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
    
    // ถ้าไม่มีเน็ต (หรือเพิ่งเปิดเครื่องแล้วเน็ตยังไม่มา) ให้ตกมาอ่านจากถ่านแทน
    return Rtc.GetDateTime();
}

// ==============================================================================

void loadConfig() {
    EEPROM.get(0, config);
    if(config.r[0].mode > 1 || (uint8_t)config.r[0].name[0] == 255 || config.r[0].t1.on_h > 23) { 
        for(int i=0; i<4; i++) {
            snprintf(config.r[i].name, sizeof(config.r[i].name), "Relay %d", i+1);
            config.r[i].mode = 0; config.r[i].state = 0;
            config.r[i].t1 = {1, 8, 0, 12, 0};  
            config.r[i].t2 = {0, 13, 0, 17, 0}; 
        }
        EEPROM.put(0, config); EEPROM.commit();
    }
}

bool isTimeActive(int current_h, int current_m, int on_h, int on_m, int off_h, int off_m) {
    int cur_mins = (current_h * 60) + current_m;
    int on_mins = (on_h * 60) + on_m;
    int off_mins = (off_h * 60) + off_m;
    if (on_mins == off_mins) return false; 
    if (on_mins < off_mins) return (cur_mins >= on_mins && cur_mins < off_mins);
    else return (cur_mins >= on_mins || cur_mins < off_mins); 
}

void checkRelays() {
    // เปลี่ยนมาดึงเวลาอัจฉริยะแทน
    RtcDateTime now = getAccurateTime();
    
    // ป้องกันกรณีโมดูลถ่านเสียและไม่มีเน็ตด้วย (ขยะ)
    if (!now.IsValid() || now.Year() < 2020) return; 

    int cur_h = now.Hour();
    int cur_m = now.Minute();

    for(int i=0; i<4; i++) {
        if (config.r[i].mode == 1) {
            isRelayActive[i] = (config.r[i].state == 1);
        } else {
            bool t1_active = config.r[i].t1.active && isTimeActive(cur_h, cur_m, config.r[i].t1.on_h, config.r[i].t1.on_m, config.r[i].t1.off_h, config.r[i].t1.off_m);
            bool t2_active = config.r[i].t2.active && isTimeActive(cur_h, cur_m, config.r[i].t2.on_h, config.r[i].t2.on_m, config.r[i].t2.off_h, config.r[i].t2.off_m);
            isRelayActive[i] = (t1_active || t2_active);
        }
        digitalWrite(RELAY_PINS[i], isRelayActive[i] ? RELAY_ON : RELAY_OFF);
    }
}

void handleRoot() { 
    if (!server.authenticate(www_username, www_password)) return server.requestAuthentication();
    server.send_P(200, "text/html", PAGE_HTML); 
}

void handleApiConfig() {
    if (!server.authenticate(www_username, www_password)) return server.requestAuthentication();
    String json = "{\"r\":[";
    for(int i=0; i<4; i++) {
        char buf[200];
        snprintf(buf, sizeof(buf), "{\"n\":\"%s\",\"m\":%d,\"s\":%d,\"t1\":{\"a\":%d,\"on\":\"%02d:%02d\",\"off\":\"%02d:%02d\"},\"t2\":{\"a\":%d,\"on\":\"%02d:%02d\",\"off\":\"%02d:%02d\"}}",
            config.r[i].name, config.r[i].mode, config.r[i].state,
            config.r[i].t1.active, config.r[i].t1.on_h, config.r[i].t1.on_m, config.r[i].t1.off_h, config.r[i].t1.off_m,
            config.r[i].t2.active, config.r[i].t2.on_h, config.r[i].t2.on_m, config.r[i].t2.off_h, config.r[i].t2.off_m);
        json += buf; if(i < 3) json += ",";
    }
    json += "]}";
    server.send(200, "application/json", json);
}

void handleApiStatus() {
    if (!server.authenticate(www_username, www_password)) return server.requestAuthentication();
    
    // เปลี่ยนมาอ่านเวลาอัจฉริยะสำหรับส่งโชว์หน้าเว็บ
    RtcDateTime now = getAccurateTime();
    
    String json = "{";
    char buf[100];
    snprintf(buf, sizeof(buf), "\"time\":\"%02u:%02u:%02u\",\"date\":\"%02u/%02u/%04u\",\"net\":\"%s\",\"st\":[",
             now.Hour(), now.Minute(), now.Second(), now.Day(), now.Month(), now.Year(),
             (time(nullptr) > 1000000000) ? "NTP (แม่นยำสูง)" : "RTC (ถ่านสำรอง)");
    json += buf;
    for(int i=0; i<4; i++) { json += String(isRelayActive[i] ? 1 : 0); if(i < 3) json += ","; }
    json += "]}";
    server.send(200, "application/json", json);
}

void handleApiUpdate() {
    if (!server.authenticate(www_username, www_password)) return server.requestAuthentication();
    int i = server.arg("i").toInt();
    if(i >= 0 && i < 4) {
        config.r[i].mode = server.arg("m").toInt(); config.r[i].state = server.arg("s").toInt();
        config.r[i].t1.active = server.arg("t1a").toInt();
        config.r[i].t1.on_h = server.arg("t1on").substring(0,2).toInt(); config.r[i].t1.on_m = server.arg("t1on").substring(3,5).toInt();
        config.r[i].t1.off_h = server.arg("t1off").substring(0,2).toInt(); config.r[i].t1.off_m = server.arg("t1off").substring(3,5).toInt();
        config.r[i].t2.active = server.arg("t2a").toInt();
        config.r[i].t2.on_h = server.arg("t2on").substring(0,2).toInt(); config.r[i].t2.on_m = server.arg("t2on").substring(3,5).toInt();
        config.r[i].t2.off_h = server.arg("t2off").substring(0,2).toInt(); config.r[i].t2.off_m = server.arg("t2off").substring(3,5).toInt();
        EEPROM.put(0, config); EEPROM.commit(); checkRelays(); 
    }
    server.send(200, "text/plain", "OK");
}

void handleApiRename() {
    if (!server.authenticate(www_username, www_password)) return server.requestAuthentication();
    int i = server.arg("i").toInt();
    String n = server.arg("n");
    if(i >= 0 && i < 4) {
        strncpy(config.r[i].name, n.c_str(), sizeof(config.r[i].name) - 1);
        config.r[i].name[sizeof(config.r[i].name) - 1] = '\0';
        EEPROM.put(0, config); EEPROM.commit();
    }
    server.send(200, "text/plain", "OK");
}

void checkWifiResetButton() {
    if (digitalRead(WIFI_RESET_PIN) == LOW) {
        delay(2000);
        WiFiManager wifiManager; wifiManager.resetSettings(); WiFi.disconnect(true); ESP.restart();               
    }
}

void setup() {
    Serial.begin(115200); 
    pinMode(WIFI_RESET_PIN, INPUT_PULLUP);
    for(int i=0; i<4; i++) { pinMode(RELAY_PINS[i], OUTPUT); digitalWrite(RELAY_PINS[i], RELAY_OFF); }

    EEPROM.begin(512); loadConfig();
    Rtc.Begin();
    if (Rtc.GetIsWriteProtected()) Rtc.SetIsWriteProtected(false);
    if (!Rtc.GetIsRunning()) Rtc.SetIsRunning(true);

    WiFiManager wifiManager;
    if (!wifiManager.autoConnect("NodeMCU_RTC_Setup")) { delay(3000); ESP.restart(); }

    if (MDNS.begin("ovt69")) { Serial.println("mDNS: http://ovt69.local"); }

    server.on("/", handleRoot);
    server.on("/api/config", handleApiConfig);
    server.on("/api/status", handleApiStatus);
    server.on("/api/update", handleApiUpdate);
    server.on("/api/rename", handleApiRename);
    server.begin();

    // เริ่มการซิงค์เวลาอัตโนมัติในเบื้องหลัง
    configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");
}

void loop() {
    checkWifiResetButton();
    MDNS.update();
    server.handleClient();
    checkRelays(); 
}

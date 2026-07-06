#include "wifi_config.h"
#include <WiFi.h>
#include <ArduinoJson.h>

// =============== HTML 页面（内嵌，极简版） ===============
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>BLE配置</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:sans-serif;background:#f0f2f5;color:#333;padding:16px;max-width:600px;margin:0 auto}
h1{text-align:center;color:#1a73e8;font-size:20px;margin:12px 0}
.card{background:#fff;border-radius:10px;padding:16px;margin-bottom:12px;box-shadow:0 1px 4px rgba(0,0,0,.1)}
.card h2{font-size:16px;margin-bottom:10px;color:#555}
.l{font-size:13px;color:#666;display:block;margin:6px 0 3px}
select,input[type=text]{width:100%;padding:8px;border:1px solid #ddd;border-radius:6px;font-size:14px;margin-bottom:6px}
.flex{display:flex;gap:8px;flex-wrap:wrap}
.flex>div{flex:1;min-width:100px}
.btn{padding:8px 16px;border:none;border-radius:6px;font-size:13px;cursor:pointer;color:#fff;display:inline-block;margin:4px 2px}
.btn1{background:#1a73e8}
.btn2{background:#34a853}
.btn3{background:#ea4335}
.btn4{background:#666}
table{width:100%;border-collapse:collapse;margin:8px 0;font-size:13px}
th,td{padding:8px;text-align:left;border-bottom:1px solid #eee}
th{color:#888;font-weight:600}
.tag{display:inline-block;padding:1px 6px;border-radius:8px;font-size:11px;color:#fff}
.t0{background:#1a73e8}
.t1{background:#34a853}
.t2{background:#fbbc04;color:#333}
.st{display:flex;justify-content:space-between;background:#e8f0fe;padding:8px 12px;border-radius:6px;font-size:13px;margin-bottom:10px}
.st .v{color:#1a73e8;font-weight:600}
</style></head><body>
<h1>BLE Dongle</h1>
<div class="st"><span>规则数</span><span class="v" id="c">0</span></div>
<div class="card">
<h2>添加规则</h2>
<div class="flex"><div><label class="l">类型</label>
<select id="t"><option value="0">MAC</option><option value="1">名称</option><option value="2">厂商ID</option></select></div>
<div style="flex:2"><label class="l">值</label><input type="text" id="v" placeholder="AA:BB:CC:DD:EE:FF"></div></div>
<div id="cidg" style="display:none"><label class="l">制造商ID(十进制)</label><input type="text" id="cid" placeholder="65535"></div>
<button class="btn btn1" onclick="add()">添加</button></div>
<div class="card">
<h2>白名单</h2>
<table><thead><tr><th>类型</th><th>值</th><th>操作</th></tr></thead><tbody id="tb"></tbody></table>
<div style="margin-top:8px">
<button class="btn btn2" onclick="save()">保存</button>
<button class="btn btn3" onclick="clr()">清空</button>
<button class="btn btn4" onclick="reb()">重启</button></div></div>
<script>
var d=[];
function ld(){fetch('/api/config').then(r=>r.json()).then(x=>{d=x.whitelist||[];r()})}
function r(){var h='',tl=['MAC','名称','厂商'];document.getElementById('c').textContent=d.length;
if(!d.length)h='<tr><td colspan="3" style="text-align:center;color:#999">空</td></tr>';
else d.forEach(function(e,i){var v=e.type===2?'0x'+e.companyId.toString(16).toUpperCase():e.value;
h+='<tr><td><span class="tag t'+e.type+'">'+(tl[e.type]||'')+'</span></td><td>'+v+'</td><td><button class="btn btn3" onclick="del('+i+')" style="padding:4px 10px;font-size:11px">删除</button></td></tr>'});
document.getElementById('tb').innerHTML=h}
function add(){var tp=parseInt(document.getElementById('t').value),vl=document.getElementById('v').value.trim(),ci=parseInt(document.getElementById('cid').value)||0;
if(!vl&&tp!==2){alert('请输入值');return}
d.push({type:tp,value:vl,companyId:ci,enabled:true});r();document.getElementById('v').value=''}
function del(i){d.splice(i,1);r()}
function save(){fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({whitelist:d})}).then(r=>r.json()).then(function(x){alert(x.message||'OK');ld()})}
function clr(){if(!confirm('清空所有规则?'))return;fetch('/api/config',{method:'DELETE'}).then(r=>r.json()).then(function(x){alert(x.message||'OK');ld()})}
function reb(){if(!confirm('重启设备?'))return;fetch('/api/reboot',{method:'POST'}).then(function(){alert('重启中...')})}
document.getElementById('t').onchange=function(){document.getElementById('cidg').style.display=this.value==='2'?'block':'none'};
ld();
</script></body></html>
)rawliteral";

// =============== WifiConfigServer ===============
WifiConfigServer::WifiConfigServer() : _config(nullptr), _server(nullptr) {}

bool WifiConfigServer::begin(ConfigManager* config) {
    _config = config;

    // 启动 Wi-Fi AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CLIENTS);

    IPAddress ip = WiFi.softAPIP();
    Serial.printf("[WiFi] AP started: SSID=%s IP=%s\n", WIFI_AP_SSID, ip.toString().c_str());

    // 启动 Web 服务器
    _server = new WebServer(80);
    _setupRoutes();
    _server->begin();
    Serial.println("[WiFi] HTTP server started on port 80");

    return true;
}

void WifiConfigServer::update() {
    if (_server) {
        _server->handleClient();
    }
}

int WifiConfigServer::getClientCount() const {
    return WiFi.softAPgetStationNum();
}

void WifiConfigServer::_setupRoutes() {
    _server->on("/",                   HTTP_GET,  [this](){ _handleRoot(); });
    _server->on("/api/config",         HTTP_GET,  [this](){ _handleGetConfig(); });
    _server->on("/api/config",         HTTP_POST, [this](){ _handleSaveConfig(); });
    _server->on("/api/config",         HTTP_DELETE, [this](){ _handleClearAll(); });
    _server->on("/api/entry",          HTTP_POST, [this](){ _handleAddEntry(); });
    _server->on("/api/entry",          HTTP_DELETE, [this](){ _handleDeleteEntry(); });
    _server->on("/api/reboot",         HTTP_POST, [this](){ _handleReboot(); });
    _server->onNotFound([this](){ _handleNotFound(); });
}

void WifiConfigServer::_handleRoot() {
    _server->send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void WifiConfigServer::_handleGetConfig() {
    String json = _config->whitelistToJson();
    _server->send(200, "application/json; charset=utf-8", json);
}

void WifiConfigServer::_handleSaveConfig() {
    String body = _server->arg("plain");
    if (_config->whitelistFromJson(body)) {
        _config->setWhitelist(_config->getWhitelist()); // 触发保存
        _server->send(200, "application/json", "{\"message\":\"保存成功\",\"status\":\"ok\"}");
        Serial.println("[WiFi] Config saved via web");
    } else {
        _server->send(400, "application/json", "{\"message\":\"JSON 解析失败\",\"status\":\"error\"}");
    }
}

void WifiConfigServer::_handleAddEntry() {
    String body = _server->arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        _server->send(400, "application/json", "{\"message\":\"JSON 解析失败\",\"status\":\"error\"}");
        return;
    }
    WhitelistEntry entry;
    entry.type      = static_cast<WhitelistEntry::Type>(doc["type"] | 0);
    entry.value     = doc["value"] | "";
    entry.companyId = doc["companyId"] | 0;
    entry.enabled   = doc["enabled"] | true;
    _config->addEntry(entry);
    _server->send(200, "application/json", "{\"message\":\"添加成功\",\"status\":\"ok\"}");
}

void WifiConfigServer::_handleDeleteEntry() {
    String idxStr = _server->arg("index");
    if (idxStr.isEmpty()) {
        // 从 body 中取
        String body = _server->arg("plain");
        JsonDocument doc;
        deserializeJson(doc, body);
        idxStr = doc["index"] | "";
    }
    int idx = idxStr.toInt();
    if (_config->removeEntry(idx)) {
        _server->send(200, "application/json", "{\"message\":\"删除成功\",\"status\":\"ok\"}");
    } else {
        _server->send(400, "application/json", "{\"message\":\"索引无效\",\"status\":\"error\"}");
    }
}

void WifiConfigServer::_handleClearAll() {
    _config->clearWhitelist();
    _server->send(200, "application/json", "{\"message\":\"已清空所有规则\",\"status\":\"ok\"}");
}

void WifiConfigServer::_handleReboot() {
    _server->send(200, "application/json", "{\"message\":\"设备即将重启...\",\"status\":\"ok\"}");
    delay(500);
    ESP.restart();
}

void WifiConfigServer::_handleNotFound() {
    _server->send(404, "text/plain", "404 Not Found");
}

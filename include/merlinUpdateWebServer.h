#pragma once

#include <ArduinoOTA.h>

#ifdef ESP8266
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#endif

#ifdef ESP32
#include <WebServer.h>
#endif

#ifdef DEBUG
#define DEBUG_PRINT(x)			Serial.print (x)
#define DEBUG_PRINTDEC(x,DEC)	Serial.print (x, DEC)
#define DEBUG_PRINTLN(x)		Serial.println (x)
#define DEBUG_PRINTLNDEC(x,DEC)	Serial.println (x, DEC)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTDEC(x,DEC)
#define DEBUG_PRINTLN(x) 
#define DEBUG_PRINTLNDEC(x,DEC)
#endif

/* Style */
String style =
"<style>:root{color-scheme:dark;--bg:#061116;--panel:#0b1c22;--panel2:#10272d;--line:#24434a;--text:#e7f3f2;--muted:#8aa5a5;--accent:#3ce0ae;--accent2:#4dbde8}*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 50% -15%,#123842 0,#061116 48%);color:var(--text);font:15px Arial,sans-serif;min-height:100vh}a{color:var(--accent2)}.shell{max-width:760px;margin:0 auto;padding:24px 16px 40px}.masthead{display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid var(--line);padding:0 0 18px;margin-bottom:18px}.brand{letter-spacing:2px;font-weight:bold;font-size:18px;color:var(--accent)}.brand small{display:block;color:var(--muted);font-size:10px;letter-spacing:1.5px;margin-top:5px}.badge{border:1px solid var(--accent);color:var(--accent);font-size:11px;letter-spacing:1px;padding:5px 8px}.panel{border:1px solid var(--line);background:rgba(11,28,34,.94);padding:18px;margin:14px 0}.panel h2{font-size:12px;letter-spacing:1.4px;color:var(--accent);margin:0 0 16px}.field{margin:0 0 15px}.field:last-child{margin-bottom:0}.field label:not(.toggle){display:block;color:var(--muted);font-size:12px;letter-spacing:.5px;margin:0 0 7px}input[type=text],input[type=number],select{width:100%;height:42px;background:#061116;color:var(--text);border:1px solid var(--line);padding:0 11px;font-size:15px}input:focus,select:focus{outline:1px solid var(--accent);border-color:var(--accent)}.toggle{display:flex;align-items:center;justify-content:space-between;gap:16px;cursor:pointer;min-height:42px;color:var(--text)}.toggle span:first-child{font-size:14px}.switch{width:44px;height:24px;border:1px solid var(--line);background:#061116;position:relative;flex:0 0 auto}.switch:after{content:'';position:absolute;width:16px;height:16px;left:3px;top:3px;background:var(--muted);transition:transform .18s,background .18s}input[type=checkbox]{position:absolute;opacity:0;pointer-events:none}input[type=checkbox]:checked+.switch{border-color:var(--accent);background:#123a36}input[type=checkbox]:checked+.switch:after{transform:translateX(18px);background:var(--accent)}.actions{display:flex;flex-wrap:wrap;gap:10px;margin-top:18px}.btn{min-height:42px;border:1px solid var(--accent);background:var(--accent);color:#04201a;padding:0 16px;font-weight:bold;letter-spacing:.5px;cursor:pointer}.btn.secondary{background:transparent;color:var(--accent2);border-color:var(--accent2)}.status{display:grid;grid-template-columns:1fr 1fr;gap:1px;background:var(--line);border:1px solid var(--line);margin-top:14px}.status div{background:var(--panel2);padding:12px}.status span{display:block;color:var(--muted);font-size:10px;letter-spacing:1px;margin-bottom:5px}.status strong{font-size:13px;word-break:break-word}#file-input{display:block;border:1px dashed var(--accent2);padding:12px;color:var(--accent2);cursor:pointer;text-align:center}#bar,#prgbar{background:#061116;height:8px}#bar{background:var(--accent);width:0%}@media(max-width:520px){.shell{padding:16px 12px 28px}.status{grid-template-columns:1fr}.panel{padding:15px}}</style>";

/* Login page */
String loginIndex =
"<form name=loginForm>"
"<input name=userid data-lpignore='true' placeholder='User ID' width:100%> "
"<input name=pwd data-lpignore='true' placeholder=Password type=Password> "
"<input type=submit onclick=check(this.form) class=btn value='Firmware Update - Login'>"
"<br>";

String loginIndex2 =
"</form>"
"<script>"
"const editor ="
"document.querySelector('grammarly-editor-plugin');"
"editor.config = {"
"suggestionCards: 'off'"
"};"
"function check(form) {"
"if(form.userid.value=='admin' && form.pwd.value=='1llusion_mb')"
"{window.open('/serverIndex')}"
"else"
"{alert('Error Password or Username')}"
"}"
"</script>"  ;

/* Server Index Page */
String serverIndex =
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
"<input type='file' name='update' id='file' onchange='sub(this)' style=display:none>"
"<label id='file-input' for='file'>   Choose file...</label>"
"<input type='submit' class=btn value='Update'>"
"<br><br>"
"<div id='prg'></div>"
"<br><div id='prgbar'><div id='bar'></div></div><br></form>"
"<script>"
"function sub(obj){"
"var fileName = obj.value.split('\\\\');"
"document.getElementById('file-input').innerHTML = '   '+ fileName[fileName.length-1];"
"};"
"$('form').submit(function(e){"
"e.preventDefault();"
"var form = $('#upload_form')[0];"
"var data = new FormData(form);"
"$.ajax({"
"url: '/update',"
"type: 'POST',"
"data: data,"
"contentType: false,"
"processData:false,"
"xhr: function() {"
"var xhr = new window.XMLHttpRequest();"
"xhr.upload.addEventListener('progress', function(evt) {"
"if (evt.lengthComputable) {"
"var per = evt.loaded / evt.total;"
"$('#prg').html('progress: ' + Math.round(per*100) + '%');"
"$('#bar').css('width',Math.round(per*100) + '%');"
"}"
"}, false);"
"return xhr;"
"},"
"success:function(d, s) {"
"console.log('success!') "
"},"
"error: function (a, b, c) {"
"}"
"});"
"});"
"const editor ="
"document.querySelector('grammarly-editor-plugin');"
"editor.config = {"
"suggestionCards: 'off'"
"};"
"</script>" + style;

#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF

#ifdef ESP8266
ESP8266HTTPUpdateServer _httpUpdater;
ESP8266WebServer _httpServer(80);
#endif

#ifdef ESP32
WebServer _httpServer(80);
#endif

String _webClientReturnString = "";
bool _updatingFirmware = false;

void handleSendToRoot()
{
	_httpServer.sendHeader("Location", "/", true); //Redirect to our html web page 
	_httpServer.send(302, "text/plane", "");
}

void setupOTA()
{
	ArduinoOTA.onStart([]() {
		_updatingFirmware = true;
		String type;
		if (ArduinoOTA.getCommand() == U_FLASH)
			type = "sketch";
		else // U_SPIFFS
			type = "filesystem";

		// NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
		DEBUG_PRINTLN("Start updating " + type);
		});

	ArduinoOTA.onEnd([]() {
		DEBUG_PRINTLN("\nEnd");
		});

	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		DEBUG_PRINT("Progress: \r"); DEBUG_PRINT((progress / (total / 100))); DEBUG_PRINTLN("%");
		});
	ArduinoOTA.onError([](ota_error_t error) {
		DEBUG_PRINT("Error[%u]: "); DEBUG_PRINTLN(error);
		if (error == OTA_AUTH_ERROR) DEBUG_PRINTLN("Auth Failed");
		else if (error == OTA_BEGIN_ERROR) DEBUG_PRINTLN("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) DEBUG_PRINTLN("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) DEBUG_PRINTLN("Receive Failed");
		else if (error == OTA_END_ERROR) DEBUG_PRINTLN("End Failed");
		delay(1000);
		ESP.restart();
		});

	ArduinoOTA.begin();
}


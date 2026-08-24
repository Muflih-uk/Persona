#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_BMP085_U.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SparkFun_APDS9960.h>

#define SDA_PIN             21
#define SCL_PIN             22
#define EMG_PIN             34
#define BTN_CONFIRM_PIN     25
#define BTN_POSTPONE_PIN    26
#define BUZZER_PIN          27
#define STATUS_LED_PIN       2

const float TREMOR_BEEP_THRESHOLD = 40.0f;
const float EMG_BEEP_THRESHOLD = 3000.0f;
const unsigned long ALERT_BEEP_INTERVAL_MS = 300UL;
unsigned long lastAlertBeep = 0;

#define MPU6050_ADDRESS     0x69
#define OLED_ADDRESS        0x3C

Adafruit_MPU6050 mpu;
SparkFun_APDS9960 apds = SparkFun_APDS9960();
Adafruit_BMP085_Unified bmp = Adafruit_BMP085_Unified(10085);

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const char* AP_SSID = "MUST";
const char* AP_PASS = "M9995757066";
WebServer server(80);

const unsigned long FAST_SAMPLE_INTERVAL_MS = 20;
const unsigned long SAMPLE_INTERVAL_MS = 100;

float accelX = 0.0;
float accelY = 0.0;
float accelZ = 0.0;
float prevAccelX = 0.0;
float prevAccelY = 0.0;
float prevAccelZ = 0.0;
float tremorMagnitude = 0.0;

int emgRaw = 0;
float emgEnvelope = 0.0;
const float EMG_ALPHA = 0.15;

bool deviceWorn = false;
float motorPerformanceIndex = 100.0;

// --- Environment (BMP180) readings ---
const float SEALEVEL_HPA = 1013.25f;
float pressureHPa = 0.0;
float temperatureC = 0.0;
float altitudeM = 0.0;

enum MedState
{
    WAITING,
    REMINDING,
    CONFIRMED,
    POSTPONED
};
MedState medState = WAITING;

const unsigned long MED_TRANSIENT_MS = 1500UL;
unsigned long medStateChangeMillis = 0;
unsigned long medEventStart = 0;
unsigned long lastReminderMillis = 0;
unsigned long confirmTimestamp = 0;
unsigned long medReminderInterval = 60UL * 1000UL;

float baselineTremor = -1.0;
float bestResponseTremor = 999.0;
unsigned long doseOnsetMillis = 0;
bool trackingResponse = false;
bool responsePausedNotWorn = false;

unsigned long lastFastSampleTime = 0;
unsigned long lastSampleTime = 0;

const unsigned long BUTTON_DEBOUNCE_MS = 300;
bool confirmBtnLastState = HIGH;
bool postponeBtnLastState = HIGH;
unsigned long lastConfirmPress = 0;
unsigned long lastPostponePress = 0;

bool motionOK = false;
bool wearOK = false;
bool envOK = false;
bool oledOK = false;

void setupWiFiDashboard();
void handleRoot();
void handleData();
void handleGraphs();
String medStateToString();
void sampleMotion();
void sampleWearState();
void sampleEMG();
void sampleEnvironment();
void computeMPI();
void updateDisplay();
void logSerial();
void handleMedicationLogic(unsigned long now);
void handleSensorAlerts();
void handleButtons();
void onMedicationConfirmed();
void onMedicationPostponed();
void evaluateMedicationResponse();
void showBootScreen();
void showErrorScreen();
void beepShort();
void beepConfirm();
void beepPostpone();

void setup()
{
    Serial.begin(115200);
    delay(500);

    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    pinMode(BTN_CONFIRM_PIN, INPUT_PULLUP);
    pinMode(BTN_POSTPONE_PIN, INPUT_PULLUP);
    confirmBtnLastState = digitalRead(BTN_CONFIRM_PIN);
    postponeBtnLastState = digitalRead(BTN_POSTPONE_PIN);

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    pinMode(EMG_PIN, INPUT);
    analogReadResolution(12);

    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(100000);

    motionOK = mpu.begin(MPU6050_ADDRESS, &Wire);
    if (motionOK)
    {
        mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
        mpu.setGyroRange(MPU6050_RANGE_500_DEG);
        mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    }

    wearOK = apds.init();
    if (wearOK)
    {
        if (!apds.enableProximitySensor(false))
        {
            wearOK = false;
        }
    }

    envOK = bmp.begin();

    oledOK = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
    if (oledOK)
    {
        display.setTextColor(SSD1306_WHITE);
        showBootScreen();
    }

    setupWiFiDashboard();
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());

    lastReminderMillis = millis();
    lastSampleTime = millis();
    lastFastSampleTime = millis();

    digitalWrite(STATUS_LED_PIN, HIGH);
}

void showBootScreen()
{
    display.clearDisplay();
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.print("PERSONA");

    display.setCursor(0, 12);
    display.print("Initializing...");

    display.setCursor(0, 24);
    display.print("MPU6050:");
    display.print(motionOK ? "OK" : "FAIL");

    display.setCursor(0, 34);
    display.print("APDS9960:");
    display.print(wearOK ? "OK" : "FAIL");

    display.setCursor(0, 44);
    display.print("BMP180:");
    display.print(envOK ? "OK" : "FAIL");

    display.setCursor(0, 54);
    display.print("OLED:OK");

    display.display();
    delay(1500);

    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("PERSONA READY");

    display.setCursor(0, 16);
    display.print("Wear device");

    display.setCursor(0, 28);
    display.print("Monitoring...");

    display.display();
    delay(1000);
}

void showErrorScreen()
{
    if (!oledOK)
    {
        return;
    }

    display.clearDisplay();
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.print("PERSONA ERROR");

    display.setCursor(0, 15);
    display.print("Check sensors");

    display.setCursor(0, 30);
    display.print("MPU:");
    display.print(motionOK ? "OK" : "FAIL");

    display.setCursor(0, 42);
    display.print("APDS:");
    display.print(wearOK ? "OK" : "FAIL");

    display.setCursor(0, 54);
    display.print("BMP:");
    display.print(envOK ? "OK" : "FAIL");

    display.display();
}

void setupWiFiDashboard()
{
    WiFi.mode(WIFI_AP);
    bool result = WiFi.softAP(AP_SSID, AP_PASS);

    if (!result)
    {
        return;
    }

    IPAddress ip = WiFi.softAPIP();

    server.on("/", HTTP_GET, handleRoot);
    server.on("/data", HTTP_GET, handleData);
    server.on("/graphs", HTTP_GET, handleGraphs);

    server.begin();
}

void handleRoot()
{
    String html =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<meta http-equiv='refresh' content='2'>"
        "<title>Persona Dashboard</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;background:#0f1b33;color:white;padding:20px;margin:0;}"
        "h1{color:#c9a227;}"
        ".card{background:#1a2a4a;padding:18px;margin:12px 0;border-radius:12px;}"
        ".value{font-size:28px;font-weight:bold;margin-top:6px;}"
        ".ok{color:#4caf50;}"
        ".warning{color:#ff9800;}"
        ".danger{color:#f44336;}"
        ".grid{display:flex;flex-wrap:wrap;gap:12px;}"
        ".grid .card{flex:1 1 140px;margin:0;}"
        ".navbtn{display:inline-block;background:#c9a227;color:#0f1b33;text-decoration:none;"
        "font-weight:bold;padding:12px 20px;border-radius:10px;margin-top:8px;}"
        "</style>"
        "</head>"
        "<body>"
        "<h1>PERSONA</h1>"
        "<p>Live Motor Monitoring</p>"
        "<a class='navbtn' href='/graphs'>View Live Graphs</a>";

    html += "<div class='card'><div>Motor Performance Index</div><div class='value'>";
    html += String(motorPerformanceIndex, 1);
    html += "</div></div>";

    html += "<div class='card'><div>Tremor Magnitude</div><div class='value'>";
    html += String(tremorMagnitude, 3);
    html += "</div></div>";

    html += "<div class='card'><div>EMG Activation</div><div class='value'>";
    html += String(emgEnvelope, 1);
    html += "</div></div>";

    html += "<div class='card'><div>Device Worn</div><div class='value'>";
    if (deviceWorn)
    {
        html += "<span class='ok'>YES</span>";
    }
    else
    {
        html += "<span class='warning'>NO</span>";
    }
    html += "</div></div>";

    html += "<div class='card'><div>Medication State</div><div class='value'>";
    html += medStateToString();
    html += "</div></div>";

    // Environment readings (pressure / temperature / altitude)
    html += "<div class='card'><div>Environment</div><div class='grid'>";

    html += "<div class='card'><div>Pressure (hPa)</div><div class='value'>";
    html += envOK ? String(pressureHPa, 1) : String("--");
    html += "</div></div>";

    html += "<div class='card'><div>Temperature (&deg;C)</div><div class='value'>";
    html += envOK ? String(temperatureC, 1) : String("--");
    html += "</div></div>";

    html += "<div class='card'><div>Altitude (m)</div><div class='value'>";
    html += envOK ? String(altitudeM, 1) : String("--");
    html += "</div></div>";

    html += "</div></div>";

    if (trackingResponse)
    {
        html += "<div class='card'><div>Medication Response</div>";

        if (responsePausedNotWorn)
        {
            html += "<p class='warning'>Paused - device not worn</p>";
        }

        html += "<p>Baseline Tremor: ";
        html += String(baselineTremor, 3);
        html += "</p><p>Best Tremor: ";
        html += String(bestResponseTremor, 3);
        html += "</p></div>";
    }

    html += "<div class='card'><div>Sensor Status</div>";
    html += "<p>MPU6050: ";
    html += motionOK ? "OK" : "FAILED";
    html += "</p><p>APDS9960: ";
    html += wearOK ? "OK" : "FAILED";
    html += "</p><p>BMP180: ";
    html += envOK ? "OK" : "FAILED";
    html += "</p><p>OLED: ";
    html += oledOK ? "OK" : "FAILED";
    html += "</p></div>";

    html += "<p style='opacity:.6'>Persona prototype monitoring system</p>";
    html += "</body></html>";

    server.send(200, "text/html", html);
}

void handleData()
{
    String json = "{";

    json += "\"mpi\":";
    json += String(motorPerformanceIndex, 2);

    json += ",\"tremor\":";
    json += String(tremorMagnitude, 4);

    json += ",\"emg\":";
    json += String(emgEnvelope, 2);

    json += ",\"worn\":";
    json += deviceWorn ? "true" : "false";

    json += ",\"medState\":\"";
    json += medStateToString();
    json += "\"";

    json += ",\"baselineTremor\":";
    json += String(baselineTremor, 4);

    json += ",\"bestResponseTremor\":";
    json += String(bestResponseTremor, 4);

    json += ",\"trackingResponse\":";
    json += trackingResponse ? "true" : "false";

    json += ",\"responsePausedNotWorn\":";
    json += responsePausedNotWorn ? "true" : "false";

    json += ",\"pressure\":";
    json += String(pressureHPa, 2);

    json += ",\"temperature\":";
    json += String(temperatureC, 2);

    json += ",\"altitude\":";
    json += String(altitudeM, 2);

    json += ",\"envOK\":";
    json += envOK ? "true" : "false";

    json += "}";

    server.send(200, "application/json", json);
}

void handleGraphs()
{
    String html =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Persona Live Graphs</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;background:#0f1b33;color:white;padding:20px;margin:0;}"
        "h1{color:#c9a227;margin-bottom:4px;}"
        ".navbtn{display:inline-block;background:#c9a227;color:#0f1b33;text-decoration:none;"
        "font-weight:bold;padding:10px 18px;border-radius:10px;margin:10px 0;}"
        ".card{background:#1a2a4a;padding:14px;margin:12px 0;border-radius:12px;}"
        ".card .title{display:flex;justify-content:space-between;align-items:baseline;}"
        ".card .value{font-weight:bold;font-size:18px;}"
        "canvas{width:100%;height:140px;display:block;background:#0f1b33;border-radius:8px;margin-top:8px;}"
        "</style>"
        "</head>"
        "<body>"
        "<h1>PERSONA</h1>"
        "<a class='navbtn' href='/'>&larr; Back to Dashboard</a>"

        "<div class='card'><div class='title'><span>Motor Performance Index</span>"
        "<span class='value' id='v-mpi'>--</span></div>"
        "<canvas id='c-mpi' width='600' height='140'></canvas></div>"

        "<div class='card'><div class='title'><span>Tremor Magnitude</span>"
        "<span class='value' id='v-tremor'>--</span></div>"
        "<canvas id='c-tremor' width='600' height='140'></canvas></div>"

        "<div class='card'><div class='title'><span>EMG Activation</span>"
        "<span class='value' id='v-emg'>--</span></div>"
        "<canvas id='c-emg' width='600' height='140'></canvas></div>"

        "<div class='card'><div class='title'><span>Pressure (hPa)</span>"
        "<span class='value' id='v-pressure'>--</span></div>"
        "<canvas id='c-pressure' width='600' height='140'></canvas></div>"

        "<div class='card'><div class='title'><span>Temperature (&deg;C)</span>"
        "<span class='value' id='v-temperature'>--</span></div>"
        "<canvas id='c-temperature' width='600' height='140'></canvas></div>"

        "<div class='card'><div class='title'><span>Altitude (m)</span>"
        "<span class='value' id='v-altitude'>--</span></div>"
        "<canvas id='c-altitude' width='600' height='140'></canvas></div>"

        "<script>"
        "const METRICS=[{key:'mpi',color:'#4caf50',digits:1},"
        "{key:'tremor',color:'#f44336',digits:3},"
        "{key:'emg',color:'#2196f3',digits:1},"
        "{key:'pressure',color:'#ff9800',digits:1},"
        "{key:'temperature',color:'#9c27b0',digits:1},"
        "{key:'altitude',color:'#00bcd4',digits:1}];"
        "const MAX_POINTS=60;"
        "const history={};"
        "METRICS.forEach(m=>history[m.key]=[]);"

        "function drawChart(canvas,data,color){"
        "const ctx=canvas.getContext('2d');"
        "const w=canvas.width,h=canvas.height;"
        "ctx.clearRect(0,0,w,h);"
        "if(data.length<2)return;"
        "let min=Math.min(...data),max=Math.max(...data);"
        "if(min===max){min-=1;max+=1;}"
        "const pad=(max-min)*0.1;"
        "min-=pad;max+=pad;"
        "ctx.strokeStyle='rgba(255,255,255,0.15)';"
        "ctx.lineWidth=1;"
        "for(let i=0;i<=3;i++){"
        "const y=(h/3)*i;"
        "ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(w,y);ctx.stroke();"
        "}"
        "ctx.strokeStyle=color;"
        "ctx.lineWidth=2;"
        "ctx.beginPath();"
        "data.forEach((v,i)=>{"
        "const x=(i/(MAX_POINTS-1))*w;"
        "const y=h-((v-min)/(max-min))*h;"
        "if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);"
        "});"
        "ctx.stroke();"
        "}"

        "async function poll(){"
        "try{"
        "const res=await fetch('/data');"
        "const d=await res.json();"
        "METRICS.forEach(m=>{"
        "const val=d[m.key];"
        "if(typeof val!=='number')return;"
        "const arr=history[m.key];"
        "arr.push(val);"
        "if(arr.length>MAX_POINTS)arr.shift();"
        "const canvas=document.getElementById('c-'+m.key);"
        "drawChart(canvas,arr,m.color);"
        "document.getElementById('v-'+m.key).textContent=val.toFixed(m.digits);"
        "});"
        "}catch(e){"
        "console.error('poll failed',e);"
        "}"
        "}"
        "poll();"
        "setInterval(poll,500);"
        "</script>"
        "</body></html>";

    server.send(200, "text/html", html);
}

String medStateToString()
{
    switch (medState)
    {
        case WAITING:
            return "Waiting";
        case REMINDING:
            return "Reminding";
        case CONFIRMED:
            return "Confirmed";
        case POSTPONED:
            return "Postponed";
    }

    return "Unknown";
}

void loop()
{
    server.handleClient();

    unsigned long now = millis();

    if (now - lastFastSampleTime >= FAST_SAMPLE_INTERVAL_MS)
    {
        lastFastSampleTime = now;

        sampleMotion();
        sampleEMG();
    }

    if (now - lastSampleTime >= SAMPLE_INTERVAL_MS)
    {
        lastSampleTime = now;

        sampleWearState();
        sampleEnvironment();
        computeMPI();
        updateDisplay();
        logSerial();
        evaluateMedicationResponse();
    }

    handleMedicationLogic(now);
    handleSensorAlerts();
    handleButtons();

    delay(1);
}

void sampleMotion()
{
    if (!motionOK)
    {
        return;
    }

    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    float ax = a.acceleration.x;
    float ay = a.acceleration.y;
    float az = a.acceleration.z;

    accelX = ax;
    accelY = ay;
    accelZ = az;

    float dx = ax - prevAccelX;
    float dy = ay - prevAccelY;
    float dz = az - prevAccelZ;

    tremorMagnitude = sqrt(dx * dx + dy * dy + dz * dz);

    prevAccelX = ax;
    prevAccelY = ay;
    prevAccelZ = az;

    if (trackingResponse)
    {
        if (deviceWorn)
        {
            if (responsePausedNotWorn)
            {
                responsePausedNotWorn = false;
            }

            if (tremorMagnitude < bestResponseTremor)
            {
                bestResponseTremor = tremorMagnitude;
            }
        }
        else if (!responsePausedNotWorn)
        {
            responsePausedNotWorn = true;
        }
    }
}

void sampleWearState()
{
    if (!wearOK)
    {
        deviceWorn = false;
        return;
    }

    uint8_t proximity = 0;

    if (!apds.readProximity(proximity))
    {
        deviceWorn = false;
        return;
    }

    deviceWorn = proximity < 5;
}

void sampleEMG()
{
    emgRaw = analogRead(EMG_PIN);

    float centered = static_cast<float>(emgRaw) - 2048.0f;
    float rectified = fabs(centered);

    emgEnvelope = (EMG_ALPHA * rectified) + ((1.0f - EMG_ALPHA) * emgEnvelope);
}

void sampleEnvironment()
{
    if (!envOK)
    {
        return;
    }

    sensors_event_t event;
    bmp.getEvent(&event);

    if (event.pressure)
    {
        pressureHPa = event.pressure;

        float temp = 0.0f;
        bmp.getTemperature(&temp);
        temperatureC = temp;

        altitudeM = bmp.pressureToAltitude(SEALEVEL_HPA, event.pressure);
    }
}

void computeMPI()
{
    float tremorScore = 100.0f - (tremorMagnitude * 20.0f);
    tremorScore = constrain(tremorScore, 0.0f, 100.0f);

    float emgScore = 100.0f - (emgEnvelope * 0.15f);
    emgScore = constrain(emgScore, 0.0f, 100.0f);

    motorPerformanceIndex = (tremorScore * 0.60f) + (emgScore * 0.40f);
    motorPerformanceIndex = constrain(motorPerformanceIndex, 0.0f, 100.0f);
}

void updateDisplay()
{
    if (!oledOK)
    {
        return;
    }

    display.clearDisplay();
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.print("MPI: ");
    display.print((int)motorPerformanceIndex);

    display.setCursor(0, 12);
    display.print("Tremor: ");
    display.print(tremorMagnitude, 2);

    display.setCursor(0, 24);
    display.print("EMG: ");
    display.print((int)emgEnvelope);

    display.setCursor(0, 36);
    display.print("Worn: ");
    display.print(deviceWorn ? "YES" : "NO");

    display.setCursor(0, 50);
    display.print("Med: ");
    display.print(medStateToString());

    display.display();
}

void logSerial() {}

void handleSensorAlerts()
{
    if (medState == REMINDING) return;

    unsigned long now = millis();
    if (now - lastAlertBeep < ALERT_BEEP_INTERVAL_MS) return;

    Serial.print("tremor value");
    Serial.println(tremorMagnitude);

    if (tremorMagnitude > TREMOR_BEEP_THRESHOLD)
    {
        tone(BUZZER_PIN, 1800, 180);
        lastAlertBeep = now;
    }
    else if (emgEnvelope > EMG_BEEP_THRESHOLD)
    {
        tone(BUZZER_PIN, 1800, 180);
        lastAlertBeep = now;
    }
}

void handleMedicationLogic(unsigned long now)
{
    if (medState == WAITING && (now - lastReminderMillis >= medReminderInterval))
    {
        medState = REMINDING;
        medEventStart = now;
        beepShort();
    }

    if (medState == REMINDING && now - lastAlertBeep >= ALERT_BEEP_INTERVAL_MS)
    {
        tone(BUZZER_PIN, 2500, 220);
        lastAlertBeep = now;
    }

    if (medState == REMINDING && (now - medEventStart >= 30000UL))
    {
        tone(BUZZER_PIN, 2500, 500);
        medEventStart = now;
    }

    if ((medState == CONFIRMED || medState == POSTPONED) &&
        (now - medStateChangeMillis >= MED_TRANSIENT_MS))
    {
        medState = WAITING;
    }
}

void handleButtons()
{
    unsigned long now = millis();

    bool confirmState = digitalRead(BTN_CONFIRM_PIN);

    if (confirmState == LOW &&
        confirmBtnLastState == HIGH &&
        (now - lastConfirmPress >= BUTTON_DEBOUNCE_MS))
    {
        lastConfirmPress = now;
        onMedicationConfirmed();
    }

    confirmBtnLastState = confirmState;

    bool postponeState = digitalRead(BTN_POSTPONE_PIN);

    if (postponeState == LOW &&
        postponeBtnLastState == HIGH &&
        (now - lastPostponePress >= BUTTON_DEBOUNCE_MS))
    {
        lastPostponePress = now;
        onMedicationPostponed();
    }

    postponeBtnLastState = postponeState;
}

void onMedicationConfirmed()
{
    if (medState != REMINDING)
    {
        return;
    }

    medState = CONFIRMED;
    medStateChangeMillis = millis();
    confirmTimestamp = millis();
    lastReminderMillis = millis();

    baselineTremor = tremorMagnitude;
    bestResponseTremor = tremorMagnitude;
    doseOnsetMillis = millis();
    trackingResponse = true;
    responsePausedNotWorn = !deviceWorn;

    beepConfirm();
}

void onMedicationPostponed()
{
    if (medState != REMINDING)
    {
        return;
    }

    medState = POSTPONED;
    medStateChangeMillis = millis();
    lastReminderMillis = millis() - medReminderInterval + 10000UL;

    beepPostpone();
}

void evaluateMedicationResponse()
{
    if (!trackingResponse)
    {
        return;
    }

    if (responsePausedNotWorn)
    {
        return;
    }

    float improvement = baselineTremor - bestResponseTremor;
    unsigned long elapsed = millis() - doseOnsetMillis;

    if (improvement > 0.5f &&
        tremorMagnitude > (bestResponseTremor + 0.3f) &&
        elapsed > (45UL * 60UL * 1000UL))
    {
        trackingResponse = false;
        responsePausedNotWorn = false;
    }
}

void beepShort()
{
    tone(BUZZER_PIN, 2000, 5000);
}

void beepConfirm()
{
    tone(BUZZER_PIN, 1500, 150);
}

void beepPostpone()
{
    tone(BUZZER_PIN, 1000, 150);
}
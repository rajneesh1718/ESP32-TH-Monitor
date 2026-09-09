/*
   ============================================================
   ESP32 SMART TEMPERATURE & HUMIDITY MONITOR
   ============================================================

   Board:
     ESP32 Dev Module

   Sensor:
     DHT11

   Wiring:
     DHT11 VCC  -> ESP32 3.3V
     DHT11 GND  -> ESP32 GND
     DHT11 DATA -> ESP32 GPIO 4

   Sampling:
     2 seconds

   Features:
     - DHT11 sensor
     - Sensor validation
     - Calibration
     - Moving average
     - EMA filtering
     - Outlier detection
     - Accurate rate of change
     - Statistics
     - FreeRTOS tasks
     - FreeRTOS queues
     - Mutex protection
     - WiFi reconnect
     - mDNS
     - REST API
     - WebSocket
     - Web dashboard
     - NTP
     - Alert engine
     - Hysteresis
     - NVS configuration
     - Diagnostics

*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <math.h>

// ============================================================
// USER SETTINGS
// ============================================================

#define DHT_PIN       4
#define DHT_TYPE      DHT11

#define SAMPLE_TIME   2000

const char* WIFI_SSID = "Wifi_name";
const char* WIFI_PASS = "Wifi_password";

const char* DEVICE_NAME = "esp32-th";

const char* DEFAULT_USERNAME = "admin";
const char* DEFAULT_PASSWORD = "admin123";

// ============================================================
// OBJECTS
// ============================================================

DHT dht(DHT_PIN, DHT_TYPE);

WebServer server(80);

WebSocketsServer webSocket(81);

Preferences preferences;

// ============================================================
// DATA STRUCTURES
// ============================================================

struct SensorData {

  float temperature;
  float humidity;

  uint32_t timestamp;
  uint32_t sampleNumber;

  bool valid;
};


struct ProcessedSensorData {

  uint32_t timestamp;
  uint32_t sampleNumber;

  float rawTemperature;
  float rawHumidity;

  float temperature;
  float humidity;

  float temperatureRate;
  float humidityRate;

  bool temperatureOutlier;
  bool humidityOutlier;

  bool valid;
};


struct Statistics {

  float minTemperature;
  float maxTemperature;
  float averageTemperature;

  float minHumidity;
  float maxHumidity;
  float averageHumidity;

  uint32_t sampleCount;
};


struct Config {

  float temperatureOffset;
  float humidityOffset;

  float emaAlpha;

  float temperatureHigh;
  float temperatureLow;

  float humidityHigh;
  float humidityLow;

  float temperatureHysteresis;
  float humidityHysteresis;

  uint32_t alertCooldown;
};

// ============================================================
// GLOBAL VARIABLES
// ============================================================

SensorData latestRaw;

ProcessedSensorData latestData;

Statistics statistics;

Config config;

uint32_t sampleNumber = 0;

// ============================================================
// FREERTOS
// ============================================================

QueueHandle_t rawDataQueue;

QueueHandle_t processedDataQueue;

SemaphoreHandle_t dataMutex;

// ============================================================
// FILTER
// ============================================================

#define FILTER_SIZE 5

float temperatureBuffer[FILTER_SIZE];

float humidityBuffer[FILTER_SIZE];

int bufferIndex = 0;

int bufferCount = 0;

float emaTemperature = 0;

float emaHumidity = 0;

bool emaInitialized = false;

// ============================================================
// RATE CALCULATION
// ============================================================

float previousTemperature = 0;

float previousHumidity = 0;

uint32_t previousTime = 0;

bool previousDataValid = false;

// ============================================================
// ALERTS
// ============================================================

bool temperatureHighAlert = false;

bool temperatureLowAlert = false;

bool humidityHighAlert = false;

bool humidityLowAlert = false;

unsigned long lastTemperatureAlert = 0;

unsigned long lastHumidityAlert = 0;

// ============================================================
// WIFI
// ============================================================

bool wifiConnected = false;

bool mdnsStarted = false;

// ============================================================
// HTML DASHBOARD
// ============================================================

const char INDEX_HTML[] PROGMEM = R"rawliteral(

<!DOCTYPE html>

<html>

<head>

<meta name="viewport"
content="width=device-width, initial-scale=1">

<title>ESP32 TH Monitor</title>

<style>

* {
    box-sizing: border-box;
}

body {

    margin: 0;

    font-family: Arial, Helvetica, sans-serif;

    background: #111827;

    color: white;
}

.container {

    max-width: 900px;

    margin: auto;

    padding: 20px;
}

h1 {

    text-align: center;

    margin-bottom: 5px;
}

.subtitle {

    text-align: center;

    color: #9ca3af;

    margin-bottom: 20px;
}

.status {

    text-align: center;

    padding: 12px;

    background: #1f2937;

    border-radius: 12px;

    margin-bottom: 20px;
}

.cards {

    display: grid;

    grid-template-columns:
    repeat(auto-fit, minmax(250px, 1fr));

    gap: 15px;
}

.card {

    background: #1f2937;

    border-radius: 18px;

    padding: 25px;

    text-align: center;
}

.card h2 {

    color: #9ca3af;

    font-size: 18px;
}

.value {

    font-size: 48px;

    font-weight: bold;

    margin: 20px 0;
}

.unit {

    font-size: 20px;

    color: #9ca3af;
}

.section {

    margin-top: 20px;

    background: #1f2937;

    padding: 20px;

    border-radius: 18px;
}

.row {

    display: flex;

    justify-content: space-between;

    padding: 9px 0;

    border-bottom: 1px solid #374151;
}

.ok {

    color: #34d399;

    font-weight: bold;
}

.bad {

    color: #f87171;

    font-weight: bold;
}

canvas {

    width: 100%;

    height: 250px;

    background: #111827;

    border-radius: 10px;
}

</style>

</head>

<body>

<div class="container">

<h1>ESP32 TH Monitor</h1>

<div class="subtitle">
DHT11 Temperature & Humidity Monitoring
</div>

<div id="status" class="status">
Connecting...
</div>

<div class="cards">

<div class="card">

<h2>Temperature</h2>

<div class="value">

<span id="temperature">--</span>

<span class="unit">°C</span>

</div>

<div>
Rate:
<span id="temperatureRate">--</span>
°C/s
</div>

</div>


<div class="card">

<h2>Humidity</h2>

<div class="value">

<span id="humidity">--</span>

<span class="unit">%</span>

</div>

<div>
Rate:
<span id="humidityRate">--</span>
%/s
</div>

</div>

</div>


<div class="section">

<h2>Statistics</h2>

<div class="row">

<span>Temperature Minimum</span>

<span id="tmin">--</span>

</div>

<div class="row">

<span>Temperature Maximum</span>

<span id="tmax">--</span>

</div>

<div class="row">

<span>Temperature Average</span>

<span id="tavg">--</span>

</div>

<div class="row">

<span>Humidity Minimum</span>

<span id="hmin">--</span>

</div>

<div class="row">

<span>Humidity Maximum</span>

<span id="hmax">--</span>

</div>

<div class="row">

<span>Humidity Average</span>

<span id="havg">--</span>

</div>

</div>


<div class="section">

<h2>Live Temperature</h2>

<canvas id="graph"
width="800"
height="250">
</canvas>

</div>


<div class="section">

<h2>Device Information</h2>

<div class="row">

<span>IP Address</span>

<span id="ip">--</span>

</div>

<div class="row">

<span>WiFi Signal</span>

<span id="rssi">--</span>

</div>

<div class="row">

<span>Uptime</span>

<span id="uptime">--</span>

</div>

<div class="row">

<span>Samples</span>

<span id="samples">--</span>

</div>

<div class="row">

<span>Time</span>

<span id="time">--</span>

</div>

</div>

</div>


<script>

let socket;

let temperatures = [];

const MAX_POINTS = 60;


// ==========================================================
// WEBSOCKET
// ==========================================================

function connectWebSocket() {

    socket = new WebSocket(
        "ws://" +
        window.location.hostname +
        ":81/"
    );


    socket.onopen = function() {

        document.getElementById(
            "status"
        ).innerHTML =
        "<span class='ok'>● ONLINE</span>";

    };


    socket.onclose = function() {

        document.getElementById(
            "status"
        ).innerHTML =
        "<span class='bad'>● OFFLINE</span>";

        setTimeout(
            connectWebSocket,
            3000
        );

    };


    socket.onerror = function() {

        document.getElementById(
            "status"
        ).innerHTML =
        "<span class='bad'>● CONNECTION ERROR</span>";

    };


    socket.onmessage = function(event) {

        try {

            let data =
                JSON.parse(event.data);

            updateSensor(data);

        }

        catch(error) {

            console.log(error);

        }

    };

}


// ==========================================================
// SENSOR UPDATE
// ==========================================================

function updateSensor(data) {

    if (data.type !== "sensor")
        return;


    document.getElementById(
        "temperature"
    ).innerText =
        Number(data.temperature)
        .toFixed(1);


    document.getElementById(
        "humidity"
    ).innerText =
        Number(data.humidity)
        .toFixed(1);


    document.getElementById(
        "temperatureRate"
    ).innerText =
        Number(data.temperatureRate)
        .toFixed(3);


    document.getElementById(
        "humidityRate"
    ).innerText =
        Number(data.humidityRate)
        .toFixed(3);


    temperatures.push(
        Number(data.temperature)
    );


    if (
        temperatures.length >
        MAX_POINTS
    ) {

        temperatures.shift();

    }


    drawGraph();

}


// ==========================================================
// GRAPH
// ==========================================================

function drawGraph() {

    const canvas =
        document.getElementById("graph");

    const ctx =
        canvas.getContext("2d");


    ctx.clearRect(
        0,
        0,
        canvas.width,
        canvas.height
    );


    if (
        temperatures.length < 2
    )
        return;


    let min =
        Math.min(...temperatures);

    let max =
        Math.max(...temperatures);


    if (min === max) {

        min -= 1;

        max += 1;

    }


    ctx.beginPath();


    for (
        let i = 0;
        i < temperatures.length;
        i++
    ) {

        let x =
            i *
            canvas.width /
            Math.max(
                1,
                MAX_POINTS - 1
            );


        let y =
            canvas.height -
            (
                (
                    temperatures[i] -
                    min
                ) /
                (max - min)
            ) *
            canvas.height;


        if (i === 0)
            ctx.moveTo(x, y);

        else
            ctx.lineTo(x, y);

    }


    ctx.stroke();

}


// ==========================================================
// STATUS
// ==========================================================

async function loadStatus() {

    try {

        let response =
            await fetch(
                "/api/v1/status"
            );


        let data =
            await response.json();


        document.getElementById(
            "ip"
        ).innerText =
            data.ip;


        document.getElementById(
            "rssi"
        ).innerText =
            data.rssi + " dBm";


        document.getElementById(
            "uptime"
        ).innerText =
            data.uptime;


        document.getElementById(
            "samples"
        ).innerText =
            data.samples;


        document.getElementById(
            "time"
        ).innerText =
            data.time;

    }

    catch(error) {

        console.log(error);

    }

}


// ==========================================================
// STATISTICS
// ==========================================================

async function loadStatistics() {

    try {

        let response =
            await fetch(
                "/api/v1/statistics"
            );


        let data =
            await response.json();


        document.getElementById(
            "tmin"
        ).innerText =
            Number(
                data.temperature.min
            ).toFixed(1) +
            " °C";


        document.getElementById(
            "tmax"
        ).innerText =
            Number(
                data.temperature.max
            ).toFixed(1) +
            " °C";


        document.getElementById(
            "tavg"
        ).innerText =
            Number(
                data.temperature.average
            ).toFixed(1) +
            " °C";


        document.getElementById(
            "hmin"
        ).innerText =
            Number(
                data.humidity.min
            ).toFixed(1) +
            " %";


        document.getElementById(
            "hmax"
        ).innerText =
            Number(
                data.humidity.max
            ).toFixed(1) +
            " %";


        document.getElementById(
            "havg"
        ).innerText =
            Number(
                data.humidity.average
            ).toFixed(1) +
            " %";

    }

    catch(error) {

        console.log(error);

    }

}


// ==========================================================
// START
// ==========================================================

connectWebSocket();

loadStatus();

loadStatistics();


setInterval(
    loadStatus,
    5000
);


setInterval(
    loadStatistics,
    5000
);

</script>

</body>

</html>

)rawliteral";

// ============================================================
// UPTIME
// ============================================================

String getUptime() {

    uint32_t seconds =
        millis() / 1000;


    uint32_t days =
        seconds / 86400;


    seconds %= 86400;


    uint32_t hours =
        seconds / 3600;


    seconds %= 3600;


    uint32_t minutes =
        seconds / 60;


    seconds %= 60;


    char buffer[50];


    snprintf(
        buffer,
        sizeof(buffer),
        "%lu d %02lu:%02lu:%02lu",
        (unsigned long)days,
        (unsigned long)hours,
        (unsigned long)minutes,
        (unsigned long)seconds
    );


    return String(buffer);
}

// ============================================================
// TIME
// ============================================================

String getTimeString() {

    time_t now =
        time(nullptr);


    if (now < 100000) {

        return "Not synchronized";

    }


    struct tm timeinfo;


    localtime_r(
        &now,
        &timeinfo
    );


    char buffer[40];


    strftime(
        buffer,
        sizeof(buffer),
        "%Y-%m-%d %H:%M:%S",
        &timeinfo
    );


    return String(buffer);
}

// ============================================================
// CONFIG LOAD
// ============================================================

void loadConfig() {

    preferences.begin(
        "config",
        true
    );


    config.temperatureOffset =
        preferences.getFloat(
            "toffset",
            0.0
        );


    config.humidityOffset =
        preferences.getFloat(
            "hoffset",
            0.0
        );


    config.emaAlpha =
        preferences.getFloat(
            "alpha",
            0.30
        );


    config.temperatureHigh =
        preferences.getFloat(
            "thigh",
            35.0
        );


    config.temperatureLow =
        preferences.getFloat(
            "tlow",
            10.0
        );


    config.humidityHigh =
        preferences.getFloat(
            "hhigh",
            80.0
        );


    config.humidityLow =
        preferences.getFloat(
            "hlow",
            20.0
        );


    config.temperatureHysteresis =
        preferences.getFloat(
            "thyst",
            1.0
        );


    config.humidityHysteresis =
        preferences.getFloat(
            "hhyst",
            3.0
        );


    config.alertCooldown =
        preferences.getUInt(
            "cooldown",
            60000
        );


    preferences.end();
}

// ============================================================
// CONFIG SAVE
// ============================================================

void saveConfig() {

    preferences.begin(
        "config",
        false
    );


    preferences.putFloat(
        "toffset",
        config.temperatureOffset
    );


    preferences.putFloat(
        "hoffset",
        config.humidityOffset
    );


    preferences.putFloat(
        "alpha",
        config.emaAlpha
    );


    preferences.putFloat(
        "thigh",
        config.temperatureHigh
    );


    preferences.putFloat(
        "tlow",
        config.temperatureLow
    );


    preferences.putFloat(
        "hhigh",
        config.humidityHigh
    );


    preferences.putFloat(
        "hlow",
        config.humidityLow
    );


    preferences.putFloat(
        "thyst",
        config.temperatureHysteresis
    );


    preferences.putFloat(
        "hhyst",
        config.humidityHysteresis
    );


    preferences.putUInt(
        "cooldown",
        config.alertCooldown
    );


    preferences.end();
}

// ============================================================
// SENSOR VALIDATION
// ============================================================

bool validateSensorData(
    float temperature,
    float humidity
) {

    if (
        isnan(temperature) ||
        isnan(humidity)
    ) {

        return false;

    }


    if (
        temperature < -40.0 ||
        temperature > 80.0
    ) {

        return false;

    }


    if (
        humidity < 0.0 ||
        humidity > 100.0
    ) {

        return false;

    }


    return true;
}

// ============================================================
// MOVING AVERAGE
// ============================================================

float calculateAverage(
    float* buffer,
    int count
) {

    if (count <= 0)
        return 0.0;


    float sum = 0.0;


    for (
        int i = 0;
        i < count;
        i++
    ) {

        sum += buffer[i];

    }


    return sum / count;
}

// ============================================================
// SENSOR TASK
// ============================================================

void sensorTask(
    void* parameter
) {

    while (true) {

        float temperature =
            dht.readTemperature();


        float humidity =
            dht.readHumidity();


        SensorData data;


        data.temperature =
            temperature;


        data.humidity =
            humidity;


        data.timestamp =
            millis();


        data.sampleNumber =
            ++sampleNumber;


        data.valid =
            validateSensorData(
                temperature,
                humidity
            );


        if (data.valid) {

            if (
                xQueueSend(
                    rawDataQueue,
                    &data,
                    pdMS_TO_TICKS(100)
                ) != pdPASS
            ) {

                Serial.println(
                    "WARNING: Raw queue full"
                );

            }

        }

        else {

            Serial.println(
                "ERROR: Invalid DHT11 reading"
            );

        }


        vTaskDelay(
            pdMS_TO_TICKS(
                SAMPLE_TIME
            )
        );

    }
}

// ============================================================
// PROCESS SENSOR
// ============================================================

ProcessedSensorData processSensorData(
    SensorData input
) {

    ProcessedSensorData output;


    output.timestamp =
        input.timestamp;


    output.sampleNumber =
        input.sampleNumber;


    output.rawTemperature =
        input.temperature;


    output.rawHumidity =
        input.humidity;


    output.temperatureOutlier =
        false;


    output.humidityOutlier =
        false;


    output.valid =
        input.valid;


    // --------------------------------------------------------
    // Calibration
    // --------------------------------------------------------

    float temperature =
        input.temperature +
        config.temperatureOffset;


    float humidity =
        input.humidity +
        config.humidityOffset;


    // --------------------------------------------------------
    // Outlier detection
    // --------------------------------------------------------

    if (previousDataValid) {

        float temperatureJump =
            fabs(
                temperature -
                previousTemperature
            );


        float humidityJump =
            fabs(
                humidity -
                previousHumidity
            );


        if (
            temperatureJump > 5.0
        ) {

            output.temperatureOutlier =
                true;

        }


        if (
            humidityJump > 10.0
        ) {

            output.humidityOutlier =
                true;

        }

    }


    // --------------------------------------------------------
    // Moving average buffer
    // --------------------------------------------------------

    temperatureBuffer[
        bufferIndex
    ] =
        temperature;


    humidityBuffer[
        bufferIndex
    ] =
        humidity;


    bufferIndex++;


    if (
        bufferIndex >= FILTER_SIZE
    ) {

        bufferIndex = 0;

    }


    if (
        bufferCount < FILTER_SIZE
    ) {

        bufferCount++;

    }


    float averageTemperature =
        calculateAverage(
            temperatureBuffer,
            bufferCount
        );


    float averageHumidity =
        calculateAverage(
            humidityBuffer,
            bufferCount
        );


    // --------------------------------------------------------
    // EMA
    // --------------------------------------------------------

    float alpha =
        config.emaAlpha;


    alpha =
        constrain(
            alpha,
            0.01,
            1.0
        );


    if (!emaInitialized) {

        emaTemperature =
            averageTemperature;


        emaHumidity =
            averageHumidity;


        emaInitialized =
            true;

    }

    else {

        emaTemperature =
            alpha *
            averageTemperature
            +
            (1.0 - alpha) *
            emaTemperature;


        emaHumidity =
            alpha *
            averageHumidity
            +
            (1.0 - alpha) *
            emaHumidity;

    }


    output.temperature =
        emaTemperature;


    output.humidity =
        emaHumidity;


    // --------------------------------------------------------
    // Accurate rate calculation
    // --------------------------------------------------------

    uint32_t currentTime =
        millis();


    if (previousDataValid) {

        uint32_t elapsed =
            currentTime -
            previousTime;


        if (elapsed > 0) {

            float seconds =
                elapsed / 1000.0;


            output.temperatureRate =
                (
                    output.temperature -
                    previousTemperature
                ) /
                seconds;


            output.humidityRate =
                (
                    output.humidity -
                    previousHumidity
                ) /
                seconds;

        }

        else {

            output.temperatureRate = 0;

            output.humidityRate = 0;

        }

    }

    else {

        output.temperatureRate = 0;

        output.humidityRate = 0;

    }


    previousTemperature =
        output.temperature;


    previousHumidity =
        output.humidity;


    previousTime =
        currentTime;


    previousDataValid =
        true;


    return output;
}

// ============================================================
// STATISTICS
// ============================================================

void updateStatistics(
    const ProcessedSensorData& data
) {

    if (!data.valid)
        return;


    if (
        statistics.sampleCount == 0
    ) {

        statistics.minTemperature =
            data.temperature;


        statistics.maxTemperature =
            data.temperature;


        statistics.averageTemperature =
            data.temperature;


        statistics.minHumidity =
            data.humidity;


        statistics.maxHumidity =
            data.humidity;


        statistics.averageHumidity =
            data.humidity;


        statistics.sampleCount =
            1;


        return;

    }


    if (
        data.temperature <
        statistics.minTemperature
    ) {

        statistics.minTemperature =
            data.temperature;

    }


    if (
        data.temperature >
        statistics.maxTemperature
    ) {

        statistics.maxTemperature =
            data.temperature;

    }


    if (
        data.humidity <
        statistics.minHumidity
    ) {

        statistics.minHumidity =
            data.humidity;

    }


    if (
        data.humidity >
        statistics.maxHumidity
    ) {

        statistics.maxHumidity =
            data.humidity;

    }


    uint32_t n =
        statistics.sampleCount;


    statistics.averageTemperature =
        (
            statistics.averageTemperature *
            n
            +
            data.temperature
        )
        /
        (n + 1);


    statistics.averageHumidity =
        (
            statistics.averageHumidity *
            n
            +
            data.humidity
        )
        /
        (n + 1);


    statistics.sampleCount++;
}

// ============================================================
// PROCESSING TASK
// ============================================================

void processingTask(
    void* parameter
) {

    SensorData input;


    while (true) {

        if (
            xQueueReceive(
                rawDataQueue,
                &input,
                portMAX_DELAY
            )
        ) {

            ProcessedSensorData output =
                processSensorData(
                    input
                );


            if (
                xSemaphoreTake(
                    dataMutex,
                    pdMS_TO_TICKS(100)
                )
            ) {

                latestRaw =
                    input;


                latestData =
                    output;


                updateStatistics(
                    output
                );


                xSemaphoreGive(
                    dataMutex
                );


                if (
                    xQueueSend(
                        processedDataQueue,
                        &output,
                        0
                    ) != pdPASS
                ) {

                    Serial.println(
                        "WARNING: Processed queue full"
                    );

                }

            }

        }

    }
}

// ============================================================
// ALERT ENGINE
// ============================================================

void checkAlerts(
    const ProcessedSensorData& data
) {

    if (!data.valid)
        return;


    unsigned long now =
        millis();


    // --------------------------------------------------------
    // TEMPERATURE HIGH
    // --------------------------------------------------------

    if (
        !temperatureHighAlert &&
        data.temperature >=
        config.temperatureHigh
    ) {

        if (
            now - lastTemperatureAlert >=
            config.alertCooldown
        ) {

            Serial.println(
                "[ALERT] Temperature HIGH"
            );


            temperatureHighAlert =
                true;


            lastTemperatureAlert =
                now;

        }

    }

    else if (
        temperatureHighAlert &&
        data.temperature <
        (
            config.temperatureHigh -
            config.temperatureHysteresis
        )
    ) {

        temperatureHighAlert =
            false;


        Serial.println(
            "[RECOVERY] Temperature HIGH cleared"
        );

    }


    // --------------------------------------------------------
    // TEMPERATURE LOW
    // --------------------------------------------------------

    if (
        !temperatureLowAlert &&
        data.temperature <=
        config.temperatureLow
    ) {

        if (
            now - lastTemperatureAlert >=
            config.alertCooldown
        ) {

            Serial.println(
                "[ALERT] Temperature LOW"
            );


            temperatureLowAlert =
                true;


            lastTemperatureAlert =
                now;

        }

    }

    else if (
        temperatureLowAlert &&
        data.temperature >
        (
            config.temperatureLow +
            config.temperatureHysteresis
        )
    ) {

        temperatureLowAlert =
            false;


        Serial.println(
            "[RECOVERY] Temperature LOW cleared"
        );

    }


    // --------------------------------------------------------
    // HUMIDITY HIGH
    // --------------------------------------------------------

    if (
        !humidityHighAlert &&
        data.humidity >=
        config.humidityHigh
    ) {

        if (
            now - lastHumidityAlert >=
            config.alertCooldown
        ) {

            Serial.println(
                "[ALERT] Humidity HIGH"
            );


            humidityHighAlert =
                true;


            lastHumidityAlert =
                now;

        }

    }

    else if (
        humidityHighAlert &&
        data.humidity <
        (
            config.humidityHigh -
            config.humidityHysteresis
        )
    ) {

        humidityHighAlert =
            false;


        Serial.println(
            "[RECOVERY] Humidity HIGH cleared"
        );

    }


    // --------------------------------------------------------
    // HUMIDITY LOW
    // --------------------------------------------------------

    if (
        !humidityLowAlert &&
        data.humidity <=
        config.humidityLow
    ) {

        if (
            now - lastHumidityAlert >=
            config.alertCooldown
        ) {

            Serial.println(
                "[ALERT] Humidity LOW"
            );


            humidityLowAlert =
                true;


            lastHumidityAlert =
                now;

        }

    }

    else if (
        humidityLowAlert &&
        data.humidity >
        (
            config.humidityLow +
            config.humidityHysteresis
        )
    ) {

        humidityLowAlert =
            false;


        Serial.println(
            "[RECOVERY] Humidity LOW cleared"
        );

    }

}

// ============================================================
// SEND WEBSOCKET SENSOR DATA
// ============================================================

void sendSensorWebSocket() {

    if (
        webSocket.connectedClients() == 0
    ) {

        return;

    }


    ProcessedSensorData data;


    if (
        xSemaphoreTake(
            dataMutex,
            pdMS_TO_TICKS(50)
        )
    ) {

        data =
            latestData;


        xSemaphoreGive(
            dataMutex
        );

    }

    else {

        return;

    }


    JsonDocument doc;


    doc["type"] =
        "sensor";


    doc["temperature"] =
        data.temperature;


    doc["humidity"] =
        data.humidity;


    doc["temperatureRate"] =
        data.temperatureRate;


    doc["humidityRate"] =
        data.humidityRate;


    doc["sampleNumber"] =
        data.sampleNumber;


    doc["valid"] =
        data.valid;


    doc["temperatureOutlier"] =
        data.temperatureOutlier;


    doc["humidityOutlier"] =
        data.humidityOutlier;


    String output;


    serializeJson(
        doc,
        output
    );


    webSocket.broadcastTXT(
        output
    );
}

// ============================================================
// WEBSOCKET EVENT
// ============================================================

void webSocketEvent(
    uint8_t client,
    WStype_t type,
    uint8_t* payload,
    size_t length
) {

    switch (type) {

        case WStype_CONNECTED:

            Serial.print(
                "WebSocket connected: "
            );

            Serial.println(
                client
            );

            break;


        case WStype_DISCONNECTED:

            Serial.print(
                "WebSocket disconnected: "
            );

            Serial.println(
                client
            );

            break;


        default:

            break;

    }
}

// ============================================================
// LOGIN
// ============================================================

void handleLogin() {

    JsonDocument doc;


    DeserializationError error =
        deserializeJson(
            doc,
            server.arg("plain")
        );


    if (error) {

        server.send(
            400,
            "application/json",
            "{\"error\":\"Invalid JSON\"}"
        );


        return;

    }


    String username =
        doc["username"] | "";


    String password =
        doc["password"] | "";


    if (
        username ==
        DEFAULT_USERNAME
        &&
        password ==
        DEFAULT_PASSWORD
    ) {

        JsonDocument response;


        response["success"] =
            true;


        response["token"] =
            "esp32-admin-session";


        String output;


        serializeJson(
            response,
            output
        );


        server.send(
            200,
            "application/json",
            output
        );

    }

    else {

        server.send(
            401,
            "application/json",
            "{\"success\":false,\"error\":\"Invalid credentials\"}"
        );

    }
}

// ============================================================
// AUTHENTICATION
// ============================================================

bool checkAuthentication() {

    if (
        !server.hasHeader(
            "Authorization"
        )
    ) {

        return false;

    }


    String header =
        server.header(
            "Authorization"
        );


    if (
        header ==
        "Bearer esp32-admin-session"
    ) {

        return true;

    }


    return false;
}

// ============================================================
// SENSOR API
// ============================================================

void handleSensorAPI() {

    ProcessedSensorData data;


    if (
        xSemaphoreTake(
            dataMutex,
            pdMS_TO_TICKS(100)
        )
    ) {

        data =
            latestData;


        xSemaphoreGive(
            dataMutex

        );

    }

    else {

        server.send(
            503,
            "application/json",
            "{\"error\":\"Data unavailable\"}"
        );


        return;

    }


    JsonDocument doc;


    doc["temperature"] =
        data.temperature;


    doc["humidity"] =
        data.humidity;


    doc["rawTemperature"] =
        data.rawTemperature;


    doc["rawHumidity"] =
        data.rawHumidity;


    doc["temperatureRate"] =
        data.temperatureRate;


    doc["humidityRate"] =
        data.humidityRate;


    doc["temperatureOutlier"] =
        data.temperatureOutlier;


    doc["humidityOutlier"] =
        data.humidityOutlier;


    doc["valid"] =
        data.valid;


    doc["sampleNumber"] =
        data.sampleNumber;


    String output;


    serializeJson(
        doc,
        output
    );


    server.send(
        200,
        "application/json",
        output
    );
}

// ============================================================
// STATUS API
// ============================================================

void handleStatusAPI() {

    JsonDocument doc;


    doc["device"] =
        DEVICE_NAME;


    doc["chipModel"] =
        ESP.getChipModel();


    doc["chipRevision"] =
        ESP.getChipRevision();


    doc["freeHeap"] =
        ESP.getFreeHeap();


    doc["heapSize"] =
        ESP.getHeapSize();


    doc["minimumFreeHeap"] =
        ESP.getMinFreeHeap();


    doc["uptime"] =
        getUptime();


    doc["samples"] =
        sampleNumber;


    doc["wifi"] =
        WiFi.status() ==
        WL_CONNECTED;


    doc["ip"] =
        WiFi.localIP().toString();


    doc["rssi"] =
        WiFi.RSSI();


    doc["time"] =
        getTimeString();


    String output;


    serializeJson(
        doc,
        output
    );


    server.send(
        200,
        "application/json",
        output
    );
}

// ============================================================
// STATISTICS API
// ============================================================

void handleStatisticsAPI() {

    Statistics stats;


    if (
        xSemaphoreTake(
            dataMutex,
            pdMS_TO_TICKS(100)
        )
    ) {

        stats =
            statistics;


        xSemaphoreGive(
            dataMutex

        );

    }


    JsonDocument doc;


    JsonObject temperature =
        doc["temperature"]
        .to<JsonObject>();


    temperature["min"] =
        stats.minTemperature;


    temperature["max"] =
        stats.maxTemperature;


    temperature["average"] =
        stats.averageTemperature;


    JsonObject humidity =
        doc["humidity"]
        .to<JsonObject>();


    humidity["min"] =
        stats.minHumidity;


    humidity["max"] =
        stats.maxHumidity;


    humidity["average"] =
        stats.averageHumidity;


    doc["sampleCount"] =
        stats.sampleCount;


    String output;


    serializeJson(
        doc,
        output
    );


    server.send(
        200,
        "application/json",
        output
    );
}

// ============================================================
// CONFIG GET
// ============================================================

void handleConfigGet() {

    if (
        !checkAuthentication()
    ) {

        server.send(
            401,
            "application/json",
            "{\"error\":\"Unauthorized\"}"
        );


        return;

    }


    JsonDocument doc;


    doc["temperatureOffset"] =
        config.temperatureOffset;


    doc["humidityOffset"] =
        config.humidityOffset;


    doc["emaAlpha"] =
        config.emaAlpha;


    doc["temperatureHigh"] =
        config.temperatureHigh;


    doc["temperatureLow"] =
        config.temperatureLow;


    doc["humidityHigh"] =
        config.humidityHigh;


    doc["humidityLow"] =
        config.humidityLow;


    doc["temperatureHysteresis"] =
        config.temperatureHysteresis;


    doc["humidityHysteresis"] =
        config.humidityHysteresis;


    doc["alertCooldown"] =
        config.alertCooldown;


    String output;


    serializeJson(
        doc,
        output
    );


    server.send(
        200,
        "application/json",
        output
    );
}

// ============================================================
// CONFIG PUT
// ============================================================

void handleConfigPut() {

    if (
        !checkAuthentication()
    ) {

        server.send(
            401,
            "application/json",
            "{\"error\":\"Unauthorized\"}"
        );


        return;

    }


    JsonDocument doc;


    DeserializationError error =
        deserializeJson(
            doc,
            server.arg("plain")
        );


    if (error) {

        server.send(
            400,
            "application/json",
            "{\"error\":\"Invalid JSON\"}"
        );


        return;

    }


    if (
        doc["temperatureOffset"]
        .is<float>()
    ) {

        config.temperatureOffset =
            doc["temperatureOffset"];

    }


    if (
        doc["humidityOffset"]
        .is<float>()
    ) {

        config.humidityOffset =
            doc["humidityOffset"];

    }


    if (
        doc["emaAlpha"]
        .is<float>()
    ) {

        config.emaAlpha =
            constrain(
                (float)doc["emaAlpha"],
                0.01,
                1.0
            );

    }


    if (
        doc["temperatureHigh"]
        .is<float>()
    ) {

        config.temperatureHigh =
            doc["temperatureHigh"];

    }


    if (
        doc["temperatureLow"]
        .is<float>()
    ) {

        config.temperatureLow =
            doc["temperatureLow"];

    }


    if (
        doc["humidityHigh"]
        .is<float>()
    ) {

        config.humidityHigh =
            doc["humidityHigh"];

    }


    if (
        doc["humidityLow"]
        .is<float>()
    ) {

        config.humidityLow =
            doc["humidityLow"];

    }


    if (
        doc["temperatureHysteresis"]
        .is<float>()
    ) {

        config.temperatureHysteresis =
            doc["temperatureHysteresis"];

    }


    if (
        doc["humidityHysteresis"]
        .is<float>()
    ) {

        config.humidityHysteresis =
            doc["humidityHysteresis"];

    }


    if (
        doc["alertCooldown"]
        .is<unsigned long>()
    ) {

        config.alertCooldown =
            doc["alertCooldown"];

    }


    saveConfig();


    server.send(
        200,
        "application/json",
        "{\"success\":true}"
    );
}

// ============================================================
// DIAGNOSTICS
// ============================================================

void handleDiagnosticsAPI() {

    JsonDocument doc;


    doc["freeHeap"] =
        ESP.getFreeHeap();


    doc["minimumFreeHeap"] =
        ESP.getMinFreeHeap();


    doc["heapSize"] =
        ESP.getHeapSize();


    doc["uptimeMs"] =
        millis();


    doc["wifiStatus"] =
        WiFi.status();


    doc["rssi"] =
        WiFi.RSSI();


    doc["rawQueue"] =
        uxQueueMessagesWaiting(
            rawDataQueue
        );


    doc["processedQueue"] =
        uxQueueMessagesWaiting(
            processedDataQueue
        );


    doc["webSocketClients"] =
        webSocket.connectedClients();


    doc["sampleIntervalMs"] =
        SAMPLE_TIME;


    doc["dhtPin"] =
        DHT_PIN;


    doc["mdns"] =
        mdnsStarted;


    String output;


    serializeJson(
        doc,
        output
    );


    server.send(
        200,
        "application/json",
        output
    );
}

// ============================================================
// 404
// ============================================================

void handleNotFound() {

    JsonDocument doc;


    doc["error"] =
        "Not Found";


    doc["path"] =
        server.uri();


    String output;


    serializeJson(
        doc,
        output
    );


    server.send(
        404,
        "application/json",
        output
    );
}

// ============================================================
// START MDNS
// ============================================================

void startMDNS() {

    if (
        WiFi.status() !=
        WL_CONNECTED
    ) {

        return;

    }


    if (mdnsStarted) {

        return;

    }


    if (
        MDNS.begin(
            DEVICE_NAME
        )
    ) {

        mdnsStarted =
            true;


        MDNS.addService(
            "http",
            "tcp",
            80
        );


        Serial.println();
        Serial.println(
            "================================"
        );

        Serial.println(
            "mDNS STARTED"
        );

        Serial.print(
            "Hostname: http://"
        );

        Serial.print(
            DEVICE_NAME
        );

        Serial.println(
            ".local"
        );

        Serial.println(
            "================================"
        );

    }

    else {

        Serial.println(
            "mDNS start failed"
        );

    }
}

// ============================================================
// HTTP SERVER
// ============================================================

void setupWebServer() {

    server.on(
        "/",
        HTTP_GET,
        []() {

            server.send_P(
                200,
                "text/html",
                INDEX_HTML
            );

        }
    );


    server.on(
        "/login",
        HTTP_POST,
        handleLogin
    );


    server.on(
        "/api/v1/sensor",
        HTTP_GET,
        handleSensorAPI
    );


    server.on(
        "/api/v1/status",
        HTTP_GET,
        handleStatusAPI
    );


    server.on(
        "/api/v1/statistics",
        HTTP_GET,
        handleStatisticsAPI
    );


    server.on(
        "/api/v1/config",
        HTTP_GET,
        handleConfigGet
    );


    server.on(
        "/api/v1/config",
        HTTP_PUT,
        handleConfigPut
    );


    server.on(
        "/api/v1/diagnostics",
        HTTP_GET,
        handleDiagnosticsAPI
    );


    server.onNotFound(
        handleNotFound
    );


    server.begin();


    Serial.println(
        "HTTP server started on port 80"
    );
}

// ============================================================
// WIFI CONNECTION
// ============================================================

void connectWiFi() {

    Serial.println();

    Serial.println(
        "Connecting to WiFi..."
    );


    WiFi.mode(
        WIFI_STA
    );


    WiFi.setSleep(
        false
    );


    WiFi.begin(
        WIFI_SSID,
        WIFI_PASS
    );


    unsigned long start =
        millis();


    while (
        WiFi.status() !=
        WL_CONNECTED
        &&
        millis() - start < 20000
    ) {

        delay(500);

        Serial.print(".");

    }


    Serial.println();


    if (
        WiFi.status() ==
        WL_CONNECTED
    ) {

        wifiConnected =
            true;


        Serial.println(
            "WiFi connected"
        );


        Serial.print(
            "IP Address: "
        );


        Serial.println(
            WiFi.localIP()
        );


        Serial.print(
            "RSSI: "
        );


        Serial.print(
            WiFi.RSSI()
        );


        Serial.println(
            " dBm"
        );


        startMDNS();

    }

    else {

        wifiConnected =
            false;


        Serial.println(
            "WiFi connection failed"
        );

    }
}

// ============================================================
// WIFI TASK
// ============================================================

void wifiTask(
    void* parameter
) {

    while (true) {

        if (
            WiFi.status() !=
            WL_CONNECTED
        ) {

            wifiConnected =
                false;


            if (mdnsStarted) {

                MDNS.end();

                mdnsStarted =
                    false;

            }


            Serial.println();
            Serial.println(
                "WiFi disconnected."
            );


            Serial.println(
                "Attempting reconnect..."
            );


            WiFi.disconnect();


            WiFi.begin(
                WIFI_SSID,
                WIFI_PASS
            );


            unsigned long start =
                millis();


            while (
                WiFi.status() !=
                WL_CONNECTED
                &&
                millis() - start < 10000
            ) {

                vTaskDelay(
                    pdMS_TO_TICKS(500)
                );

            }


            if (
                WiFi.status() ==
                WL_CONNECTED
            ) {

                wifiConnected =
                    true;


                Serial.println(
                    "WiFi reconnected"
                );


                Serial.print(
                    "IP Address: "
                );


                Serial.println(
                    WiFi.localIP()
                );


                Serial.print(
                    "RSSI: "
                );


                Serial.print(
                    WiFi.RSSI()
                );


                Serial.println(
                    " dBm"
                );


                startMDNS();

            }

            else {

                Serial.println(
                    "Reconnect failed"
                );

            }

        }


        vTaskDelay(
            pdMS_TO_TICKS(10000)
        );

    }
}

// ============================================================
// MONITOR TASK
// ============================================================

void monitorTask(
    void* parameter
) {

    ProcessedSensorData data;


    while (true) {

        if (
            xQueueReceive(
                processedDataQueue,
                &data,
                portMAX_DELAY
            )
        ) {

            Serial.println();
            Serial.println(
                "================================"
            );


            Serial.print(
                "Sample: "
            );


            Serial.println(
                data.sampleNumber
            );


            Serial.print(
                "Temperature: "
            );


            Serial.print(
                data.temperature,
                2
            );


            Serial.println(
                " °C"
            );


            Serial.print(
                "Humidity: "
            );


            Serial.print(
                data.humidity,
                2
            );


            Serial.println(
                " %"
            );


            Serial.print(
                "Temperature Rate: "
            );


            Serial.print(
                data.temperatureRate,
                4
            );


            Serial.println(
                " °C/s"
            );


            Serial.print(
                "Humidity Rate: "
            );


            Serial.print(
                data.humidityRate,
                4
            );


            Serial.println(
                " %/s"
            );


            if (
                data.temperatureOutlier
            ) {

                Serial.println(
                    "WARNING: Temperature outlier"
                );

            }


            if (
                data.humidityOutlier
            ) {

                Serial.println(
                    "WARNING: Humidity outlier"
                );

            }


            checkAlerts(
                data
            );


            sendSensorWebSocket();

        }

    }
}

// ============================================================
// SETUP
// ============================================================

void setup() {

    Serial.begin(
        115200
    );


    delay(1000);


    Serial.println();
    Serial.println(
        "========================================"
    );


    Serial.println(
        "ESP32 SMART TEMPERATURE & HUMIDITY"
    );


    Serial.println(
        "MONITORING SYSTEM"
    );


    Serial.println(
        "========================================"
    );


    Serial.println();


    // --------------------------------------------------------
    // DHT11
    // --------------------------------------------------------

    dht.begin();


    Serial.println(
        "DHT11 initialized"
    );


    // --------------------------------------------------------
    // Configuration
    // --------------------------------------------------------

    loadConfig();


    // --------------------------------------------------------
    // Statistics
    // --------------------------------------------------------

    memset(
        &statistics,
        0,
        sizeof(statistics)
    );


    // --------------------------------------------------------
    // Mutex
    // --------------------------------------------------------

    dataMutex =
        xSemaphoreCreateMutex();


    if (
        dataMutex == NULL
    ) {

        Serial.println(
            "ERROR: Mutex creation failed"
        );


        while (true) {

            delay(1000);

        }

    }


    // --------------------------------------------------------
    // Queues
    // --------------------------------------------------------

    rawDataQueue =
        xQueueCreate(
            5,
            sizeof(SensorData)
        );


    processedDataQueue =
        xQueueCreate(
            5,
            sizeof(ProcessedSensorData)
        );


    if (
        rawDataQueue == NULL
        ||
        processedDataQueue == NULL
    ) {

        Serial.println(
            "ERROR: Queue creation failed"
        );


        while (true) {

            delay(1000);

        }

    }


    // --------------------------------------------------------
    // WiFi
    // --------------------------------------------------------

    connectWiFi();


    // --------------------------------------------------------
    // NTP
    // --------------------------------------------------------

    configTzTime(
        "IST-5:30",
        "pool.ntp.org",
        "time.nist.gov"
    );


    Serial.println(
        "NTP synchronization started"
    );


    // --------------------------------------------------------
    // HTTP
    // --------------------------------------------------------

    setupWebServer();


    // --------------------------------------------------------
    // WebSocket
    // --------------------------------------------------------

    webSocket.begin();


    webSocket.onEvent(
        webSocketEvent
    );


    Serial.println(
        "WebSocket started on port 81"
    );


    // --------------------------------------------------------
    // FreeRTOS Sensor Task
    // --------------------------------------------------------

    xTaskCreatePinnedToCore(
        sensorTask,
        "SensorTask",
        4096,
        NULL,
        2,
        NULL,
        1
    );


    // --------------------------------------------------------
    // FreeRTOS Processing Task
    // --------------------------------------------------------

    xTaskCreatePinnedToCore(
        processingTask,
        "ProcessingTask",
        4096,
        NULL,
        2,
        NULL,
        1
    );


    // --------------------------------------------------------
    // FreeRTOS Monitor Task
    // --------------------------------------------------------

    xTaskCreatePinnedToCore(
        monitorTask,
        "MonitorTask",
        4096,
        NULL,
        1,
        NULL,
        1
    );


    // --------------------------------------------------------
    // FreeRTOS WiFi Task
    // --------------------------------------------------------

    xTaskCreatePinnedToCore(
        wifiTask,
        "WiFiTask",
        4096,
        NULL,
        1,
        NULL,
        0
    );


    Serial.println(
        "FreeRTOS tasks started"
    );


    Serial.println();
    Serial.println(
        "========================================"
    );


    Serial.println(
        "SYSTEM READY"
    );


    Serial.println(
        "========================================"
    );


    if (
        WiFi.status() ==
        WL_CONNECTED
    ) {

        Serial.println();

        Serial.print(
            "Open using IP: http://"
        );

        Serial.println(
            WiFi.localIP()
        );


        Serial.print(
            "Try mDNS: http://"
        );


        Serial.print(
            DEVICE_NAME
        );


        Serial.println(
            ".local"
        );

    }

}

// ============================================================
// LOOP
// ============================================================

void loop() {

    server.handleClient();

    webSocket.loop();

    delay(10);

}

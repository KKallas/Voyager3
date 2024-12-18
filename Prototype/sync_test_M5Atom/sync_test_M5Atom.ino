#include <M5Atom.h>
#include <WiFi.h>
#include <time.h>

// WiFi credentials
const char* ssid = "Apollo0000";
const char* password = "dsputnik";

// NTP Server settings
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.google.com";
const char* ntpServer3 = "time.cloudflare.com";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 0;

// Shared timing state protected by mutex
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t epochMillis = 0;
volatile uint32_t lastSyncMillis = 0;

// Task handles
TaskHandle_t blinkTaskHandle = NULL;
TaskHandle_t syncTaskHandle = NULL;

void updateTimeState() {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0) {
        portENTER_CRITICAL(&timerMux);
        epochMillis = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
        lastSyncMillis = millis();
        portEXIT_CRITICAL(&timerMux);
    }
}

void syncTime() {
    configTzTime("GMT0", ntpServer1, ntpServer2, ntpServer3);
    
    struct tm timeinfo;
    int retries = 0;
    while (!getLocalTime(&timeinfo) && retries < 5) {
        Serial.println("Sync retry...");
        delay(500);
        retries++;
    }
    
    if (retries < 5) {
        updateTimeState();
        Serial.println("Time sync successful");
    } else {
        Serial.println("Sync failed, continuing with current time");
    }
}

// LED control task with watchdog protection
void blinkTask(void * parameter) {
    bool ledState = false;
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 1; // 1ms
    
    // Initialize the xLastWakeTime variable with the current time
    xLastWakeTime = xTaskGetTickCount();
    
    while(true) {
        // Get current time with drift compensation
        portENTER_CRITICAL(&timerMux);
        uint32_t currentEpochMillis = epochMillis + (millis() - lastSyncMillis);
        portEXIT_CRITICAL(&timerMux);
        
        // Calculate milliseconds within current second
        uint32_t millisInSecond = currentEpochMillis % 1000;
        
        // LED control based on millisecond position
        if (millisInSecond < 2 && !ledState) {
            M5.dis.drawpix(0, 0xff0000);
            ledState = true;
        } else if (millisInSecond >= 500 && ledState) {
            M5.dis.drawpix(0, 0x000000);
            ledState = false;
        }
        
        // Properly yield to the watchdog using vTaskDelayUntil
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// NTP sync task
void syncTask(void * parameter) {
    const uint32_t RESYNC_INTERVAL = 300000; // 5 minutes
    
    while(true) {
        syncTime();
        // Use vTaskDelay instead of delay
        vTaskDelay(pdMS_TO_TICKS(RESYNC_INTERVAL));
    }
}

void setup() {
    M5.begin(true, false, true);
    setCpuFrequencyMhz(240);
    
    // Increase watchdog timeout for the setup process
    disableCore0WDT();
    
    Serial.println("Connecting to WiFi");
    WiFi.begin(ssid, password);
    WiFi.setSleep(false);
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi");
    
    // Initial time sync
    syncTime();
    Serial.println("Initial sync complete");
    
    // Create tasks on different cores
    xTaskCreatePinnedToCore(
        blinkTask,
        "BlinkTask",
        4096,
        NULL,
        2,  // Higher priority
        &blinkTaskHandle,
        0    // Core 0
    );
    
    xTaskCreatePinnedToCore(
        syncTask,
        "SyncTask",
        4096,
        NULL,
        1,  // Lower priority
        &syncTaskHandle,
        1    // Core 1
    );
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
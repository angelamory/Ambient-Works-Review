/**
 * Ambient Edu — Sensor + Local Flash Backup + Supabase Sync
 *
 * Reads the Sensirion SEN66 air quality sensor every minute and stores
 * every reading to LittleFS (internal flash). When WiFi is available,
 * unsynced readings are uploaded to Supabase in order. Readings taken
 * while offline are given accurate timestamps once NTP syncs.
 *
 * Key behaviours
 * ──────────────
 * • All readings are saved to flash first — nothing is lost if WiFi drops.
 * • A sync pointer tracks which rows have already been uploaded so no
 *   reading is sent twice.
 * • Timestamps use NTP when online. When offline, readings are marked
 *   "T+<seconds-since-boot>" and resolved to real timestamps as soon as
 *   NTP syncs (using the recorded boot epoch).
 * • HTTP 409 (duplicate) responses from Supabase are treated as already-
 *   synced and the pointer advances, so a retry after a crash stays safe.
 *
 * Enhanced for SparkFun C6 ESP32 with 18650 battery optimization,
 * fast responsive charging detection, and stable LED feedback.
 *
 * Requires:
 *   - Adafruit MAX1704X    (Library Manager)
 *   - Adafruit NeoPixel    (Library Manager)
 *   - Sensirion I2C SEN66  (Library Manager)
 *   - Sensirion Core       (auto-installed with the above)
 *   - ESP Supabase         (Library Manager)
 */

// ── Pin mapping ────────────────────────────────
#define I2C_SDA 6
#define I2C_SCL 7
#define HAS_POWER_EN false

// ── NeoPixel ───────────────────────────────────
#define NEOPIXEL_PIN        23
#define NEOPIXEL_BRIGHTNESS 50

// ── WiFi Credentials ────────────────────────────────────────
const char* WIFI_SSID = "angelina";
const char* WIFI_PASSWORD = "98765432";

// –– Student Config ––––––––––––––––––––––––––––––––––––––––––
const char* STUDENT_NAME = "Angel";
const char* STUDENT_EMAIL = "a.amory0320231@arts.ac.uk";

const float LOCATION_LATITUDE = 51.5;
const float LOCATION_LONGITUDE = -0.1;
const char* LOCATION_DESCRIPTION = "London";

// ── Supabase Config ─────────────────────────────────────────
const char* SUPABASE_URL = "https://zjfqphvemusbszoqtfvk.supabase.co";
const char* SUPABASE_ANON_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InpqZnFwaHZlbXVzYnN6b3F0ZnZrIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzE4OTc1ODUsImV4cCI6MjA4NzQ3MzU4NX0.T_nD4hs2JULYJj1tOSr2HeNmElszzRER1ygPpSh3Tf0";

// ── Device config ─────────────────────────────────────────────
static char DEVICE_ID[18];
const char* KIT_NUMBER = "amb-1337";

// ── Timing ────────────────────────────────────────────────────
#define LOG_INTERVAL_MS 60000UL  // Read, store, and sync every 60 s

// ── Storage ───────────────────────────────────────────────────
#define LOG_FILE        "/airquality.csv"
#define SYNC_PTR_FILE   "/sync_ptr.txt"
#define BOOT_COUNT_FILE "/boot_count.txt"
#define MAX_LOG_LINES 2880  // 48 h at 1-min intervals

// ── Sync reliability settings ─────────────────────────────────
// Settings to prevent sync pointer desync issues that cause data loss
#define MIN_WIFI_RSSI -85               // Minimum signal strength (warn but continue)
#define SYNC_VERIFY_INTERVAL 10         // Validate sync health every N successful syncs
#define MAX_CONSECUTIVE_FAILURES 3      // Reset pointer validation after this many failures
#define LAST_SYNC_FILE "/last_sync.txt" // Track last successful sync timestamp

// ─────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <Adafruit_MAX1704X.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPSupabase.h>
#include <SensirionI2cSen66.h>

#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

// ── Globals ──────────────────────────────────────────────────
SensirionI2cSen66 sensor;
Supabase db;
Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_MAX17048 battery;

// Enhanced 18650 battery functions - see detailed implementations below
float battPercent();   // Enhanced with 18650 validation and voltage fallback
bool  battCharging();  // Enhanced with voltage-based detection for reliability

static char errorMessage[64];
static int16_t sensorError;

static bool storageReady     = false;
static bool dbInitialized    = false;
static bool ntpSynced        = false;
static bool deviceRegistered = false;
static bool batteryAvailable = false;  // Track if battery sensor is working
static String deviceUuid     = "";
static uint32_t sessionId    = 0;  // Incremented each boot, identifies offline readings

// Sync reliability tracking - prevents pointer desync issues
static int consecutiveSyncFailures = 0;
static int successfulSyncs = 0;
static unsigned long lastSuccessfulSyncTime = 0;

// Offline time tracking:
// After NTP syncs we record the real time and the millis() value at that
// moment. From those two numbers we can calculate what real time it was
// for any reading taken since boot — including ones logged before NTP.
static time_t bootEpoch = 0;          // Real Unix time when device booted
static unsigned long bootMillis = 0;  // millis() at first NTP sync (≈ since boot)

static unsigned long lastLogTime = 0;
static unsigned long lastBatteryLog = 0;  // For enhanced battery monitoring

// ═════════════════════════════════════════════════════════════
// Enhanced Battery helpers - 18650 optimized with FAST charging detection
// ═════════════════════════════════════════════════════════════

/** 
 * Convert 18650 battery voltage to percentage estimate.
 * Provides accurate fallback when MAX17048 gives invalid readings.
 * Uses typical 18650 discharge curve with granular brackets for better accuracy.
 */
float voltageToPercentage(float voltage) {
  if (voltage >= 4.1) return 100;  // Full charge (4.2V max)
  if (voltage >= 4.0) return 95;   // Very high
  if (voltage >= 3.9) return 85;   // High
  if (voltage >= 3.8) return 75;   // Good charge
  if (voltage >= 3.7) return 65;   // Medium-high
  if (voltage >= 3.6) return 55;   // Medium
  if (voltage >= 3.5) return 45;   // Medium-low ← Your 3.57V hits here!
  if (voltage >= 3.4) return 35;   // Low charge
  if (voltage >= 3.3) return 25;   // Getting low
  if (voltage >= 3.2) return 18;   // Very low
  if (voltage >= 3.1) return 12;   // Critical range
  if (voltage >= 3.0) return 8;    // Very critical
  if (voltage >= 2.9) return 4;    // Emergency
  return 0;                        // Dead/disconnected
}


/** 
 * Enhanced battery percentage with 18650 validation.
 * MAX17048 can report >100% or invalid values — this function clamps 
 * and validates readings, falling back to voltage-based estimation
 * when the IC gives unreliable data.
 */
float battPercent() {
  if (!batteryAvailable) return 0.0f;
  
  float pct = battery.cellPercent();
  float voltage = battery.cellVoltage();
  
  // Basic clamping like original, but with enhanced validation
  pct = min(pct, 100.0f);
  
  // Enhanced validation for 18650 batteries - catch IC errors
  if (isnan(pct) || pct < -10 || pct > 110) {
    // Serial.printf("Battery IC error (pct=%.1f) - using 18650 voltage estimate\n", pct);
    pct = voltageToPercentage(voltage);
  }
  
    // Cross-check for 18650: if voltage very low but IC reports high percentage
  if (voltage < 3.2f && pct > 20.0f) {
    // Serial.printf("18650 voltage override: %.2fV reported %.1f%% -> voltage-based\n", voltage, pct);
    pct = voltageToPercentage(voltage);
  }
  
  // Cross-check for 18650: if voltage decent but IC reports very low percentage
  if (voltage > 3.4f && pct < 40.0f) {
    // Serial.printf("18650 voltage override: %.2fV reported %.1f%% -> voltage-based (IC too low)\n", voltage, pct);
    pct = voltageToPercentage(voltage);
  }
  
  return constrain(pct, 0.0f, 100.0f);
}

/** 
 * Enhanced charging detection using voltage-based method.
 * At low battery levels, charge rate can be negative even when plugged in
 * due to high system power consumption. Voltage boost from charger is
 * a more reliable indicator in these conditions.
 */
bool battCharging() {
  if (!batteryAvailable) return false;
  
  float voltage = battery.cellVoltage();
  
  // At low battery levels, use voltage as primary indicator
  // When plugged in, charger boosts voltage above normal discharge level
  // 3.47V threshold determined through testing with 18650 cells
  return (voltage >= 3.47f);
}

// forward declaration
void resolveOfflineTimestampsInLog();

// ═════════════════════════════════════════════════════════════
// Sync reliability functions - prevent pointer desync issues
// ═════════════════════════════════════════════════════════════

/** 
 * Count actual data rows in log file (excluding header).
 * Used to validate sync pointer hasn't drifted ahead of actual data.
 */
int countLogRows() {
  if (!LittleFS.exists(LOG_FILE)) return 0;
  File f = LittleFS.open(LOG_FILE, "r");
  if (!f) return 0;
  
  int rows = -1;  // Start at -1 to exclude header
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) rows++;
  }
  f.close();
  return max(0, rows);
}

/**
 * Validate sync pointer against actual log file.
 * Fixes the main issue where pointer gets ahead of actual data,
 * causing "nothing new to upload" when there is unsynced data.
 * Called on boot and after sync failures to catch drift early.
 */
void validateSyncPointer() {
  int ptr = getSyncPointer();
  int rows = countLogRows();
  
  if (ptr > rows) {
    Serial.printf("Sync pointer validation: pointer (%d) ahead of rows (%d) — resetting to %d\n", 
                  ptr, rows, rows > 0 ? rows - 1 : 0);
    setSyncPointer(rows > 0 ? rows - 1 : 0);  // Back up to re-sync last row safely
  }
}

/**
 * Check if network conditions are suitable for sync attempts.
 * Warns about weak signal but continues (recommended approach).
 */
bool isNetworkHealthy() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Sync: WiFi not connected");
    return false;
  }
  
  int rssi = WiFi.RSSI();
  if (rssi < MIN_WIFI_RSSI) {
    Serial.printf("Sync: weak signal (%d dBm), attempting anyway\n", rssi);
    // Continue despite weak signal - timeout handling will catch actual failures
  }
  
  return true;
}

/**
 * Handle sync failure - track consecutive failures and validate pointer.
 * After multiple failures, checks if pointer corruption is the cause.
 */
void handleSyncFailure(int httpCode, const char* context) {
  consecutiveSyncFailures++;
  Serial.printf("Sync failure #%d: %s (HTTP %d)\n", consecutiveSyncFailures, context, httpCode);
  
  // After too many failures, validate pointer to catch corruption
  if (consecutiveSyncFailures >= MAX_CONSECUTIVE_FAILURES) {
    Serial.println("Sync: multiple failures detected — validating pointer integrity");
    validateSyncPointer();
    consecutiveSyncFailures = 0;
  }
}

/**
 * Handle sync success - reset failure counter and perform periodic health checks.
 */
void handleSyncSuccess() {
  consecutiveSyncFailures = 0;
  successfulSyncs++;
  lastSuccessfulSyncTime = millis();
  
  // Periodic health check to catch pointer drift before it causes issues
  if (successfulSyncs % SYNC_VERIFY_INTERVAL == 0) {
    Serial.printf("Sync health check: %d successful syncs, validating pointer\n", successfulSyncs);
    validateSyncPointer();
    saveLastSyncTime();
  }
}

/** 
 * Save timestamp of last successful sync for diagnostics.
 */
void saveLastSyncTime() {
  File f = LittleFS.open(LAST_SYNC_FILE, "w");
  if (f) {
    char ts[25];
    currentTimestamp(ts, sizeof(ts));
    f.print(ts);
    f.close();
  }
}

// ═════════════════════════════════════════════════════════════
// Time helpers
// ═════════════════════════════════════════════════════════════

/**
 * Rewrite the CSV, replacing T+N offline markers with real ISO timestamps
 * using the current bootEpoch. Called once immediately after NTP first syncs.
 * 
 * Only resolves T+N markers that belong to this session. Markers from
 * previous sessions have a different session_id and must not be resolved
 * using this boot's epoch — they go to sensor_readings_offline instead.
 * 
 * This prevents incorrect timestamps if a reboot happens before sync,
 * leaving T+N strings that would be resolved with wrong epoch later.
 */
void resolveOfflineTimestampsInLog() {
  if (!storageReady || !LittleFS.exists(LOG_FILE)) return;

  File src = LittleFS.open(LOG_FILE, "r");
  File tmp = LittleFS.open("/tmp.csv", "w");
  if (!src || !tmp) { if (src) src.close(); if (tmp) tmp.close(); return; }

  int resolved = 0;
  bool header = true;
  while (src.available()) {
    String line = src.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    if (header) { tmp.println(line); header = false; continue; }

    // Only resolve T+N markers that belong to this session
    int comma = line.indexOf(',');
    String ts = (comma > 0) ? line.substring(0, comma) : line;
    if (ts.startsWith("T+")) {
      String sidStr = csvField(line, 10);
      uint32_t rowSid = sidStr.length() > 0 ? strtoul(sidStr.c_str(), nullptr, 10) : 0;
      if (rowSid == sessionId) {
        char tsResolved[25];
        if (resolveTimestamp(ts.c_str(), tsResolved, sizeof(tsResolved))) {
          tmp.println(String(tsResolved) + line.substring(comma));
          resolved++;
          continue;
        }
      }
    }
    tmp.println(line);
  }
  src.close(); tmp.close();
  LittleFS.remove(LOG_FILE);
  LittleFS.rename("/tmp.csv", LOG_FILE);
  Serial.printf("Resolved %d offline timestamp(s) in log.\n", resolved);
}

/**
 * Attempt NTP synchronisation. On first success, records the boot epoch
 * so offline timestamps can later be resolved to real times.
 * 
 * Works backwards from "now" to calculate what real time it was when
 * the device booted, allowing accurate timestamp resolution for any
 * reading taken since boot.
 */
bool tryNTPSync() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Syncing NTP");
  time_t t = time(nullptr);
  for (int i = 0; i < 20 && t < 1000000000UL; i++) {
    delay(500);
    Serial.print(".");
    t = time(nullptr);
  }
  if (t < 1000000000UL) {
    Serial.println(" failed.");
    return false;
  }
  Serial.println(" done.");

  if (!ntpSynced) {
    // Work backwards from "now" to find the real time at boot
    bootMillis = millis();
    bootEpoch = t - bootMillis / 1000;
    ntpSynced = true;
    Serial.printf("Boot epoch: %lu  (millis at sync: %lu)\n",
                  (unsigned long)bootEpoch, bootMillis);

    // Rewrite the CSV immediately, replacing any T+N offline markers from
    // this session with real ISO timestamps. If we don't do this, a reboot
    // before sync would leave T+N strings in flash that a future session
    // would resolve incorrectly using the wrong boot epoch.
    resolveOfflineTimestampsInLog();
  }
  return true;
}

/**
 * Write the current timestamp into buf.
 * Uses NTP-synced time when available; otherwise records seconds since
 * boot in the form "T+NNNNN" so the reading can be resolved later.
 */
void currentTimestamp(char* buf, size_t len) {
  if (ntpSynced) {
    time_t t = time(nullptr);
    struct tm* ti = gmtime(&t);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", ti);
  } else {
    // Offline marker: seconds since boot
    snprintf(buf, len, "T+%lu", millis() / 1000);
  }
}

/**
 * Convert a stored timestamp to a real ISO 8601 string.
 * Offline "T+NNNNN" markers are resolved using the recorded boot epoch.
 * Returns false if the timestamp cannot yet be resolved (NTP never synced).
 */
bool resolveTimestamp(const char* stored, char* out, size_t outLen) {
  if (strncmp(stored, "T+", 2) == 0) {
    if (bootEpoch == 0) return false;  // Can't resolve without NTP

    unsigned long offsetSec = strtoul(stored + 2, nullptr, 10);
    time_t realTime = bootEpoch + offsetSec;
    struct tm* ti = gmtime(&realTime);
    strftime(out, outLen, "%Y-%m-%dT%H:%M:%SZ", ti);
    return true;
  }
  // Already a real ISO timestamp — copy as-is
  strncpy(out, stored, outLen - 1);
  out[outLen - 1] = '\0';
  return true;
}

// ═════════════════════════════════════════════════════════════
// WiFi
// ═════════════════════════════════════════════════════════════

/**
 * Attempt to connect (or reconnect) to WiFi.
 * Returns true if connected, false if unavailable.
 * Device continues in offline mode when WiFi fails.
 */
bool tryConnectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  WiFi.disconnect(true);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
    return true;
  }
  Serial.println("\nWiFi unavailable — continuing in offline mode.");
  return false;
}

// ═════════════════════════════════════════════════════════════
// Storage (LittleFS)
// ═════════════════════════════════════════════════════════════

/**
 * Initialize LittleFS filesystem for local data storage.
 * Formats the filesystem if mounting fails (first run or corruption).
 */
bool initStorage() {
  if (LittleFS.begin(false)) return true;
  Serial.println("LittleFS mount failed — formatting...");
  if (LittleFS.format() && LittleFS.begin(false)) {
    Serial.println("LittleFS formatted and mounted.");
    return true;
  }
  Serial.println("LittleFS unavailable — data will not be stored.");
  return false;
}

/** Number of data rows successfully synced to Supabase. */
int getSyncPointer() {
  if (!LittleFS.exists(SYNC_PTR_FILE)) return 0;
  File f = LittleFS.open(SYNC_PTR_FILE, "r");
  if (!f) return 0;
  int n = f.parseInt();
  f.close();
  return n;
}

/**
 * Update the sync pointer to track upload progress.
 * Only incremented after successful upload to prevent duplicates.
 */
void setSyncPointer(int n) {
  File f = LittleFS.open(SYNC_PTR_FILE, "w");
  if (f) {
    f.print(n);
    f.close();
  }
}

/** Increment the persistent boot counter and return the new value. */
uint32_t loadAndIncrementBootCount() {
  uint32_t count = 0;
  if (LittleFS.exists(BOOT_COUNT_FILE)) {
    File f = LittleFS.open(BOOT_COUNT_FILE, "r");
    if (f) { count = f.parseInt(); f.close(); }
  }
  count++;
  File f = LittleFS.open(BOOT_COUNT_FILE, "w");
  if (f) { f.print(count); f.close(); }
  return count;
}

/**
 * Trim the oldest rows from the log so it stays under MAX_LOG_LINES.
 * The sync pointer is adjusted to stay consistent with the new row order.
 * This prevents flash storage from filling up on long-running deployments.
 */
void trimLogIfNeeded() {
  File f = LittleFS.open(LOG_FILE, "r");
  if (!f) return;
  int lines = 0;
  while (f.available()) {
    if (f.read() == '\n') lines++;
  }
  f.close();
  if (lines <= MAX_LOG_LINES) return;

  int toSkip = lines - MAX_LOG_LINES;
  f = LittleFS.open(LOG_FILE, "r");
  File tmp = LittleFS.open("/tmp.csv", "w");
  if (!f || !tmp) {
    if (f) f.close();
    if (tmp) tmp.close();
    return;
  }

  // Always keep the header; skip toSkip data rows
  bool headerWritten = false;
  int skipped = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (!headerWritten) {
      tmp.println(line);
      headerWritten = true;
      continue;
    }
    if (skipped < toSkip) {
      skipped++;
      continue;
    }
    tmp.println(line);
  }
  f.close();
  tmp.close();
  LittleFS.remove(LOG_FILE);
  LittleFS.rename("/tmp.csv", LOG_FILE);

  // Adjust sync pointer — some synced rows were removed from the front
  int newPtr = max(0, getSyncPointer() - toSkip);
  setSyncPointer(newPtr);
  Serial.printf("Trimmed log: dropped %d oldest row(s).\n", toSkip);
}

/** 
 * Append one sensor reading to the CSV log.
 * Creates header on first write. Includes session_id to correlate
 * offline readings and battery_percent for power monitoring.
 */
void logReading(const char* timestamp,
                float pm1p0, float pm2p5, float pm4p0, float pm10p0,
                float humidity, float temperature,
                float vocIndex, float noxIndex, uint16_t co2) {
  if (!storageReady) return;

  bool isNew = !LittleFS.exists(LOG_FILE);
  File f = LittleFS.open(LOG_FILE, "a");
  if (!f) {
    Serial.println("Log: failed to open " LOG_FILE);
    return;
  }

  if (isNew) {
    f.println("timestamp,pm1p0_ugm3,pm2p5_ugm3,pm4p0_ugm3,pm10p0_ugm3,"
              "humidity_pct,temp_c,voc_index,nox_index,co2_ppm,session_id,battery_percent");
  }
  f.printf("%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%u,%u,%.2f\n",
           timestamp, pm1p0, pm2p5, pm4p0, pm10p0,
           humidity, temperature, vocIndex, noxIndex, co2, sessionId, battPercent());
  f.close();

  trimLogIfNeeded();
  Serial.printf("Logged [%s]  PM2.5=%.1f  CO2=%u  T=%.1f  H=%.1f\n",
                timestamp, pm2p5, co2, temperature, humidity);
}

// ═════════════════════════════════════════════════════════════
// Supabase helpers
// ═════════════════════════════════════════════════════════════

/** Return the value of field fieldIdx (0-based) from a CSV line. */
String csvField(const String& line, int fieldIdx) {
  int count = 0, start = 0;
  for (int i = 0; i <= (int)line.length(); i++) {
    if (i == (int)line.length() || line[i] == ',') {
      if (count == fieldIdx) return line.substring(start, i);
      count++;
      start = i + 1;
    }
  }
  return "";
}

/** 
 * Validate sensor data to prevent HTTP 400 errors.
 * Checks for empty values and NaN/infinity which cause upload failures.
 */
bool isValidSensorValue(const String& value) {
  if (value.length() == 0) return false;
  if (value == "nan" || value == "inf" || value == "-inf") return false;
  return true;
}

/**
 * Register the device in Supabase if it is not already there.
 * Fetches the UUID assigned by Supabase and stores it in deviceUuid
 * so it can be included in every sensor_readings row for relational integrity.
 */
void ensureDeviceRegistered(const char* timestamp) {
  if (deviceRegistered && deviceUuid.length() > 10) return;

  // Try to find an existing record for this device
  String resp = db.from("devices")
                  .select("id")
                  .eq("device_id", String(DEVICE_ID))
                  .limit(1)
                  .doSelect();
  db.urlQuery_reset();

  // Response looks like [{"id":"<uuid>"}] when the device exists
  if (resp.startsWith("[{\"id\":\"")) {
    resp.replace("[{\"id\":\"", "");
    resp.replace("\"}]", "");
    deviceUuid = resp;
    deviceRegistered = true;
    Serial.println("Device UUID: " + deviceUuid);
    return;
  }

  // Not found — insert a new device record
  Serial.println("Device not registered. Registering...");
  String json = "{";
  json += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
  json += "\"student_name\":\"" + String(STUDENT_NAME) + "\",";
  json += "\"student_email\":\"" + String(STUDENT_EMAIL) + "\",";
  json += "\"kit_number\":\"" + String(KIT_NUMBER) + "\",";
  json += "\"latitude\":" + String(LOCATION_LATITUDE) + ",";
  json += "\"longitude\":" + String(LOCATION_LONGITUDE) + ",";
  json += "\"location_description\":\"" + String(LOCATION_DESCRIPTION) + "\",";
  json += "\"last_seen\":\"" + String(timestamp) + "\"";
  json += "}";

  int code = db.insert("devices", json, false);
  db.urlQuery_reset();

  if (code == 201) {
    Serial.println("Device registered.");
    delay(1000);
    ensureDeviceRegistered(timestamp);  // Re-fetch to get the UUID
  } else if (code == 409) {
    Serial.println("Device already exists (409).");
    delay(1000);
    ensureDeviceRegistered(timestamp);
  } else {
    Serial.printf("Device registration failed HTTP %d — will retry.\n", code);
  }
}

/**
 * Upload all unsynced rows from flash to Supabase with enhanced reliability.
 *
 * Uses the sync pointer stored in SYNC_PTR_FILE to know which rows
 * have already been uploaded. The pointer advances only after a
 * successful upload (or a 409 duplicate), so it is safe to call this
 * after a crash or reconnect without sending any reading twice.
 *
 * Offline "T+N" timestamps are sent to sensor_readings_offline with
 * session_id and boot_offset_sec for relative ordering. Real timestamps
 * go to the main sensor_readings table.
 *
 * RELIABILITY IMPROVEMENTS:
 * - Validates pointer before sync attempt to catch desync issues
 * - Only advances pointer on confirmed HTTP 201 success or 409 duplicate
 * - Network errors (code <= 0) don't advance pointer
 * - Tracks failure patterns for health monitoring
 * - Periodic network health checks during long sync sessions
 */
void syncToSupabase() {
  // Pre-flight network and storage checks
  if (!isNetworkHealthy()) return;
  if (!storageReady || !LittleFS.exists(LOG_FILE)) return;

  // Validate pointer before attempting sync to catch desync issues early
  validateSyncPointer();

  // Make sure the device is registered before pushing readings
  char nowTs[25];
  currentTimestamp(nowTs, sizeof(nowTs));
  ensureDeviceRegistered(nowTs);
  if (!deviceRegistered || deviceUuid.length() == 0) {
    Serial.println("Sync: device not registered — skipping.");
    return;
  }

  int syncPtr = getSyncPointer();
  int totalRows = countLogRows();
  
  // Debug output to track sync state and catch issues
  Serial.printf("Sync: pointer=%d, total rows=%d, to sync=%d\n", 
                syncPtr, totalRows, totalRows - syncPtr);
  
  if (syncPtr >= totalRows) {
    Serial.println("Sync: nothing new to upload.");
    return;
  }

  File f = LittleFS.open(LOG_FILE, "r");
  if (!f) {
    Serial.println("Sync: failed to open log file");
    return;
  }

  // Skip header
  if (f.available()) f.readStringUntil('\n');

  // Skip rows already synced
  for (int i = 0; i < syncPtr && f.available(); i++) {
    f.readStringUntil('\n');
  }

  int pushed = 0;
  int failed = 0;  // Track failures in this session
  
  while (f.available()) {
    // Periodic network health check during long sync sessions
    if (pushed > 0 && pushed % 5 == 0) {
      if (!isNetworkHealthy()) {
        Serial.println("Sync: network degraded during sync, pausing");
        break;
      }
    }
    
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    // Parse CSV fields
    String tsRaw  = csvField(line, 0);
    String pm1p0  = csvField(line, 1);
    String pm2p5  = csvField(line, 2);
    String pm4p0  = csvField(line, 3);
    String pm10p0 = csvField(line, 4);
    String hum    = csvField(line, 5);
    String temp   = csvField(line, 6);
    String voc    = csvField(line, 7);
    String nox    = csvField(line, 8);
    String co2    = csvField(line, 9);
    String sidStr = csvField(line, 10);
    String batt   = csvField(line, 11);  // Battery from CSV
    uint32_t rowSid = sidStr.length() > 0 ? strtoul(sidStr.c_str(), nullptr, 10) : 0;

    // Validation to prevent HTTP 400 errors from bad sensor data
    if (!isValidSensorValue(pm1p0) || !isValidSensorValue(pm2p5) ||
        !isValidSensorValue(pm4p0) || !isValidSensorValue(pm10p0) ||
        !isValidSensorValue(hum) || !isValidSensorValue(temp) ||
        !isValidSensorValue(voc) || !isValidSensorValue(nox) ||
        !isValidSensorValue(co2)) {
      Serial.printf("Sync: invalid sensor data in row %d - skipping\n", syncPtr + 1);
      syncPtr++;
      setSyncPointer(syncPtr);
      continue;
    }

    // T+N rows are from sessions that ended before NTP synced
    // Push to sensor_readings_offline with session_id and boot_offset_sec
    // so they can be relatively ordered even without absolute timestamps
    if (tsRaw.startsWith("T+")) {
      unsigned long offsetSec = strtoul(tsRaw.c_str() + 2, nullptr, 10);
      String json = "{";
      json += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
      json += "\"device_uuid\":\"" + deviceUuid + "\",";
      json += "\"session_id\":" + String(rowSid) + ",";
      json += "\"boot_offset_sec\":" + String(offsetSec) + ",";
      json += "\"pm1_0\":" + pm1p0 + ",";
      json += "\"pm2_5\":" + pm2p5 + ",";
      json += "\"pm4_0\":" + pm4p0 + ",";
      json += "\"pm10_0\":" + pm10p0 + ",";
      json += "\"humidity\":" + hum + ",";
      json += "\"temperature\":" + temp + ",";
      json += "\"co2\":" + co2 + ",";
      json += "\"voc_index\":" + voc + ",";
      json += "\"nox_index\":" + nox + ",";
      json += "\"battery_percent\":" + (batt.length() > 0 ? batt : String("null"));
      json += "}";
      
      int code = db.insert("sensor_readings_offline", json, false);
      db.urlQuery_reset();
      
      // Robust error handling - only advance pointer on confirmed success
      if (code == 201) {
        syncPtr++;
        setSyncPointer(syncPtr);
        pushed++;
        handleSyncSuccess();
        Serial.printf("Sync: offline row %d uploaded (session %u, T+%lu)\n",
                      syncPtr, rowSid, offsetSec);
      } else if (code == 409) {
        // Duplicate is safe to skip
        syncPtr++;
        setSyncPointer(syncPtr);
        Serial.printf("Sync: offline row %d duplicate, skipping.\n", syncPtr);
      } else if (code <= 0) {
        // Network/timeout error - DO NOT advance pointer
        handleSyncFailure(code, "offline row network error");
        failed++;
        break;  // Stop and retry next cycle
      } else {
        // Server error (4xx, 5xx) - DO NOT advance pointer
        handleSyncFailure(code, "offline row server error");
        failed++;
        break;
      }
      continue;
    }

    // Regular timestamped row
    String json = "{";
    json += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
    json += "\"device_uuid\":\"" + deviceUuid + "\",";
    json += "\"timestamp\":\"" + tsRaw + "\",";
    json += "\"pm1_0\":" + pm1p0 + ",";
    json += "\"pm2_5\":" + pm2p5 + ",";
    json += "\"pm4_0\":" + pm4p0 + ",";
    json += "\"pm10_0\":" + pm10p0 + ",";
    json += "\"humidity\":" + hum + ",";
    json += "\"temperature\":" + temp + ",";
    json += "\"co2\":" + co2 + ",";
    json += "\"voc_index\":" + voc + ",";
    json += "\"nox_index\":" + nox + ",";
    json += "\"battery_percent\":" + String(battPercent());  // Use current enhanced function
    json += "}";

    int code = db.insert("sensor_readings", json, false);
    db.urlQuery_reset();

    // Strict success verification before advancing pointer
    if (code == 201) {
      syncPtr++;
      setSyncPointer(syncPtr);
      pushed++;
      handleSyncSuccess();
      Serial.printf("Sync: row %d uploaded (%s)\n", syncPtr, tsRaw.c_str());
    } else if (code == 409) {
      // Duplicate is safe to skip
      syncPtr++;
      setSyncPointer(syncPtr);
      Serial.printf("Sync: row %d duplicate (409), skipping.\n", syncPtr);
    } else if (code <= 0) {
      // Network/timeout error - DO NOT advance pointer
      handleSyncFailure(code, "network timeout or connection lost");
      failed++;
      break;  // Stop trying, will retry next cycle
    } else if (code >= 400 && code < 500) {
      // Client error (bad request, unauthorized, etc) - DO NOT advance pointer
      handleSyncFailure(code, "client error - check data format");
      failed++;
      break;
    } else if (code >= 500) {
      // Server error - DO NOT advance pointer
      handleSyncFailure(code, "Supabase server error");
      failed++;
      break;
    } else {
      // Unknown response - be conservative, don't advance
      handleSyncFailure(code, "unknown response");
      failed++;
      break;
    }
  }

  f.close();
  
  // Enhanced completion reporting
  if (pushed > 0) {
    Serial.printf("Sync: %d row(s) uploaded successfully.\n", pushed);
    saveLastSyncTime();
  }
  if (failed > 0) {
    Serial.printf("Sync: stopped after %d failure(s), will retry next cycle.\n", failed);
  }
  if (pushed == 0 && failed == 0) {
    Serial.println("Sync: nothing new to upload.");
  }
}

// ═════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(100); }
  delay(500);

  // ── Enhanced Battery initialization ──────────────────────
  if (!battery.begin()) {
    Serial.println("MAX17048 not found — battery monitoring disabled.");
    batteryAvailable = false;
  } else {
    batteryAvailable = true;
    delay(1000);  // Allow stabilization for accurate initial reading
    Serial.printf("Battery: %.0f%%  %.2fV (18650 enhanced)\n", battPercent(), battery.cellVoltage());
  }

  // ── NeoPixel ──────────────────────────────────────────────
  pixel.begin();
  pixel.setBrightness(NEOPIXEL_BRIGHTNESS);
  pixel.clear();
  pixel.show();
  Serial.printf("NeoPixel initialized on pin %d\n", NEOPIXEL_PIN);

  // ── Config validation ─────────────────────────────────────
  if (String(STUDENT_NAME) == "Your Name") {
    Serial.println("ERROR: Please set STUDENT_NAME at the top of this file.");
    vTaskDelete(NULL);
  }

  // ── Storage ───────────────────────────────────────────────
  storageReady = initStorage();
  if (storageReady) {
    sessionId = loadAndIncrementBootCount();
    Serial.printf("Session ID: %u\n", sessionId);
    
    // Validate sync pointer on every boot to catch desync issues early
    Serial.println("Validating sync pointer integrity...");
    validateSyncPointer();
  }

  // ── I2C + SEN66 ──────────────────────────────────────────
  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.printf("I2C started (SDA=%d, SCL=%d)\n", I2C_SDA, I2C_SCL);
  sensor.begin(Wire, SEN66_I2C_ADDR_6B);

  sensorError = sensor.deviceReset();
  if (sensorError != NO_ERROR) {
    errorToString(sensorError, errorMessage, sizeof errorMessage);
    Serial.printf("SEN66 reset error: %s\n", errorMessage);
  }
  delay(1200);

  int8_t serialNum[32] = { 0 };
  if (sensor.getSerialNumber(serialNum, 32) == NO_ERROR) {
    Serial.printf("SEN66 serial: %s\n", (const char*)serialNum);
  }

  sensorError = sensor.startContinuousMeasurement();
  if (sensorError != NO_ERROR) {
    errorToString(sensorError, errorMessage, sizeof errorMessage);
    Serial.printf("SEN66 start error: %s\n", errorMessage);
  }

  // ── WiFi + NTP + Supabase ─────────────────────────────────
  // WiFi must be initialised before macAddress() returns a real value on C6
  tryConnectWiFi();

  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(DEVICE_ID, sizeof(DEVICE_ID),
           "%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.printf("Device ID (MAC): %s\n", DEVICE_ID);

  if (WiFi.status() == WL_CONNECTED) {
    db.begin(SUPABASE_URL, SUPABASE_ANON_KEY);
    dbInitialized = true;
    tryNTPSync();
  } else {
    Serial.println("Starting in offline mode. Data will sync when WiFi connects.");
  }

  // Trigger first read + sync immediately
  lastLogTime = millis() - LOG_INTERVAL_MS;
}

// ═════════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();

  // ── Enhanced Battery monitoring ───────────────────────────
  if (batteryAvailable && now - lastBatteryLog > 15000) {
    lastBatteryLog = now;
    float pct = battPercent();
    float voltage = battery.cellVoltage();
    float rate = battery.chargeRate();
    bool charging = battCharging();
    
    Serial.printf("18650 Battery: %.1f%% %.2fV rate:%.2f%% charging:%s\n", 
                  pct, voltage, rate, charging ? "YES" : "NO");
  }

  // ── Sensor read + local store ─────────────────────────────
  if (now - lastLogTime >= LOG_INTERVAL_MS) {
    lastLogTime = now;

    float pm1p0 = 0, pm2p5 = 0, pm4p0 = 0, pm10p0 = 0;
    float humidity = 0, temperature = 0;
    float vocIndex = 0, noxIndex = 0;
    uint16_t co2 = 0;

    int16_t err = sensor.readMeasuredValues(
      pm1p0, pm2p5, pm4p0, pm10p0,
      humidity, temperature, vocIndex, noxIndex, co2);

    if (err == NO_ERROR) {
      char ts[25];
      currentTimestamp(ts, sizeof(ts));
      // Discard first 2 minutes of readings to allow the sensor to stabilise
      if (millis() > 2 * 60 * 1000) {
        // Don't log if about to die; low battery makes readings less accurate through
        // empirical testing
        if (battPercent() < 5 && !battCharging()) {
          Serial.printf("Battery: %.0f%%  %.2fV\n", battPercent(), battery.cellVoltage());
          Serial.println("Battery critically low — skipping log (readings less accurate when battery is low).");
        } else {
          logReading(ts, pm1p0, pm2p5, pm4p0, pm10p0,
                     humidity, temperature, vocIndex, noxIndex, co2);
        }
      }
    } else {
      errorToString(err, errorMessage, sizeof errorMessage);
      Serial.printf("Sensor read error: %s\n", errorMessage);
    }

    // ── Supabase sync ───────────────────────────────────────
    // Re-attempt WiFi if disconnected, then sync any unuploaded rows
    if (WiFi.status() != WL_CONNECTED) {
      tryConnectWiFi();
    }
    if (WiFi.status() == WL_CONNECTED) {
      if (!dbInitialized) {
        db.begin(SUPABASE_URL, SUPABASE_ANON_KEY);
        dbInitialized = true;
      }
      if (!ntpSynced) {
        tryNTPSync();
      }
      syncToSupabase();  // Now includes all reliability improvements
    }
  }

  // ── Enhanced NeoPixel with fast, stable response ──────────
  // Color coding:
  // • WiFi connected: green
  // • WiFi disconnected but have an NTP timestamp so reading times are accurate: yellow
  // • WiFi disconnected and no NTP timestamp so reading times are unknown: orange
  // • Sensor errors: red (solid)
  // • Battery low: red (varies by level and charging state)
  uint8_t brightness = (sin(now / 10 * 3.14159 / 180) + 1) * NEOPIXEL_BRIGHTNESS / 2;
  pixel.setBrightness(brightness);
  
  // Fast battery checking for immediate LED response
  static unsigned long lastQuickBattCheck = 0;
  static float lastBattPercent = -1;
  static bool lastChargingState = false;
  
  if (batteryAvailable) {
    // Check every 500ms when battery <10% for responsiveness, every 2s otherwise
    unsigned long checkInterval = (lastBattPercent < 10) ? 500 : 2000;
    
    if (now - lastQuickBattCheck > checkInterval) {
      lastQuickBattCheck = now;
      float newPct = battPercent();
      bool newCharging = battCharging();
      
      // Update if anything changed significantly
      if (abs(newPct - lastBattPercent) > 0.1 || newCharging != lastChargingState) {
        lastBattPercent = newPct;
        lastChargingState = newCharging;
        
        // Debug output for state changes
        Serial.printf("LED Update: %.1f%% charging:%s\n", newPct, newCharging ? "YES" : "NO");
      }
    }
  }


  
  
  if (sensorError != NO_ERROR) {
    // Sensor error - solid red
    pixel.setPixelColor(0, pixel.Color(255, 0, 0));
  } else if (lastBattPercent < 4 && !lastChargingState) {
    // Critical battery and not charging - non-blocking blink
    bool blinkOn = (now / 400) % 2;
    pixel.setPixelColor(0, pixel.Color(blinkOn ? brightness : 0, 0, 0));
  } else if (lastBattPercent < 10) {
    // Low battery but charging or not critical - solid red  
    pixel.setPixelColor(0, pixel.Color(brightness, 0, 0));
  } else {
    // Battery OK - show connectivity status
    if (WiFi.status() != WL_CONNECTED) {
      if (ntpSynced) {
        // Offline with time sync - yellow
        pixel.setPixelColor(0, pixel.Color(brightness, brightness, 0));
      } else {
        // Offline without time sync - orange
        pixel.setPixelColor(0, pixel.Color(brightness, brightness / 2, 0));
      }
    } else {
      // Connected - green
      pixel.setPixelColor(0, pixel.Color(0, brightness, 0));
    }
  }
  
  pixel.show();
  delay(10);  // Keep short delay for overall loop timing
}

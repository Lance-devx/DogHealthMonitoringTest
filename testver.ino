#include <driver/i2s.h>
#include <WiFi.h>

// === HARDWARE PINS ===
#define PULSE_PIN   35      // HW-827 Pulse Sensor
#define FSR_PIN     34      // FSR (pressure indicator, NOT attachment)
#define PIEZO_PIN   32      // Piezo vibration sensor

#define I2S_WS      21      // I2S Word Select
#define I2S_SCK     22      // I2S Serial Clock  
#define I2S_SD      23      // I2S Serial Data

// === DOG HEALTH STRUCT ===
struct DogHealth {
  int heartRateBPM;          // From pulse sensor
  int pulseSignalQuality;    // 0-100% signal quality
  int pressureLevel;         // From FSR (0-4095)
  int activityLevel;         // From piezo
  bool sensorDetected;       // Is ANY signal coming through?
};

DogHealth dog;

// === SETUP ===
void setup() {
  Serial.begin(115200);
  delay(2000);
  
  WiFi.mode(WIFI_OFF);
  btStop();
  
  Serial.println("\n🐕 DOG PULSE MONITOR - TEST VERSION");
  Serial.println("===================================");
  Serial.println("NOTE: Testing mode - no attachment detection");
  Serial.println("Place pulse sensor on dog's EAR for best results");
  
  pinMode(PULSE_PIN, INPUT);
  pinMode(FSR_PIN, INPUT);
  pinMode(PIEZO_PIN, INPUT);
  
  initMicrophone();
  
  Serial.println("\nInitializing... Ready for testing.");
  Serial.println("Place sensor, then watch for pulse pattern:");
  Serial.println("Values should oscillate between ~1800-2500");
  Serial.println("===========================================\n");
}

// === MAIN LOOP ===
void loop() {
  testPulseSensor();
  readOtherSensors();
  displayRawData();
  
  delay(250); // Faster updates for testing
}

// === TEST PULSE SENSOR (CRITICAL) ===
void testPulseSensor() {
  static int pulseValues[20] = {0};
  static int pulseIndex = 0;
  static unsigned long lastBeatTime = 0;
  static int beatCount = 0;
  
  // Read raw pulse value
  int rawPulse = analogRead(PULSE_PIN);
  pulseValues[pulseIndex] = rawPulse;
  pulseIndex = (pulseIndex + 1) % 20;
  
  // Calculate signal quality (variation = good)
  int minVal = 4096, maxVal = 0;
  for(int i = 0; i < 20; i++) {
    if(pulseValues[i] < minVal) minVal = pulseValues[i];
    if(pulseValues[i] > maxVal) maxVal = pulseValues[i];
  }
  int variation = maxVal - minVal;
  
  // Detect beats (simplified for testing)
  static int lastRaw = 0;
  bool isRising = (rawPulse > lastRaw);
  static bool wasRising = false;
  
  if(wasRising && !isRising && variation > 200) {
    // Possible heartbeat detected
    unsigned long now = millis();
    
    if(lastBeatTime > 0) {
      long interval = now - lastBeatTime;
      if(interval > 300 && interval < 1500) {
        beatCount++;
        
        if(beatCount >= 4) {
          dog.heartRateBPM = 60000 / (interval / beatCount);
          beatCount = 0;
        }
      }
    }
    lastBeatTime = now;
  }
  
  wasRising = isRising;
  lastRaw = rawPulse;
  
  // Signal quality (0-100%)
  dog.pulseSignalQuality = min(variation / 10, 100);
  dog.sensorDetected = (variation > 100);
}

// === READ OTHER SENSORS ===
void readOtherSensors() {
  // FSR as pressure indicator (not attachment)
  dog.pressureLevel = analogRead(FSR_PIN);
  
  // Piezo as activity indicator
  static int activityBuffer[5] = {0};
  static int actIndex = 0;
  
  int rawPiezo = analogRead(PIEZO_PIN);
  activityBuffer[actIndex] = abs(rawPiezo - 2048);
  actIndex = (actIndex + 1) % 5;
  
  long sum = 0;
  for(int i = 0; i < 5; i++) sum += activityBuffer[i];
  dog.activityLevel = sum / 5;
}

// === DISPLAY RAW DATA FOR TESTING ===
void displayRawData() {
  static int displayCounter = 0;
  
  if(displayCounter % 4 == 0) { // Update BPM every 4 cycles
    Serial.println("\n--- TEST DATA ---");
    Serial.print("Pulse: ");
    
    if(dog.sensorDetected && dog.pulseSignalQuality > 30) {
      if(dog.heartRateBPM > 0) {
        Serial.print(dog.heartRateBPM);
        Serial.print(" BPM");
        
        // Dog-specific ranges
        if(dog.heartRateBPM < 60) {
          Serial.print(" (Slow for dog)");
        } else if(dog.heartRateBPM > 180) {
          Serial.print(" (Fast for dog)");
        } else {
          Serial.print(" (Normal dog range)");
        }
      } else {
        Serial.print("Detecting...");
      }
    } else {
      Serial.print("NO SIGNAL - Check placement");
    }
    
    Serial.print(" | Quality: ");
    Serial.print(dog.pulseSignalQuality);
    Serial.print("%");
    
    Serial.print(" | Pressure: ");
    Serial.print(dog.pressureLevel);
    
    Serial.print(" | Activity: ");
    Serial.print(dog.activityLevel);
    
    Serial.println();
    Serial.println("Raw pulse values (should oscillate):");
  }
  
  // Show raw pulse value (shows heartbeat pattern)
  Serial.print(analogRead(PULSE_PIN));
  Serial.print(" ");
  
  displayCounter++;
}

// === MICROPHONE INIT (simplified) ===
void initMicrophone() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 8000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 2,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };
  
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

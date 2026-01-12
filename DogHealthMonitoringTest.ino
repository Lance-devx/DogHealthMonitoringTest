#include <driver/i2s.h>
#include <WiFi.h>

// === HARDWARE PINS ===
#define PULSE_PIN   35      // Analog - Primary heart rate (HW-827)
#define FSR_PIN     34      // Analog - Contact detection (attached to dog?)
#define PIEZO_PIN   32      // Analog - Movement/Vibration

#define I2S_WS      21      // I2S Word Select
#define I2S_SCK     22      // I2S Serial Clock
#define I2S_SD      23      // I2S Serial Data

// === HEALTH PARAMETERS ===
struct DogHealth {
  int heartRateBPM;          // From pulse sensor
  int acousticHeartRateBPM;  // From microphone (optional)
  bool isProperlyAttached;   // From FSR (contact detection)
  int activityLevel;         // From piezo (vibration)
  bool abnormalRhythm;       // From microphone analysis
  float confidence;          // Data quality confidence
};

DogHealth dog;

// === SETUP ===
void setup() {
  Serial.begin(115200);
  delay(2000);
  
  // Disable WiFi for stable ADC
  WiFi.mode(WIFI_OFF);
  btStop();
  
  Serial.println("\n🐕 DOG HEARTBEAT DETECTOR v1.0");
  Serial.println("==============================");
  Serial.println("Pulse: GPIO35 (Heart rate)");
  Serial.println("FSR: GPIO34 (Contact detection)");
  Serial.println("Piezo: GPIO32 (Activity)");
  Serial.println("MEMS: GPIO21,22,23 (Heart sounds)");
  
  // Initialize sensors
  pinMode(PULSE_PIN, INPUT);
  pinMode(FSR_PIN, INPUT);
  pinMode(PIEZO_PIN, INPUT);
  
  // Initialize I2S microphone
  initMicrophone();
  
  Serial.println("\nCalibrating sensors...");
  calibrateSensors();
}

// === I2S MICROPHONE ===
void initMicrophone() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 8000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 2,
    .dma_buf_len = 128,
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

// === SENSOR CALIBRATION ===
void calibrateSensors() {
  Serial.println("Place sensor on dog for calibration...");
  Serial.println("(Or press sensor with finger for testing)");
  delay(3000);
  
  // Calibrate FSR for contact detection
  long fsrBaseline = 0;
  for(int i = 0; i < 50; i++) {
    fsrBaseline += analogRead(FSR_PIN);
    delay(20);
  }
  int contactThreshold = (fsrBaseline / 50) + 500;
  
  Serial.print("Contact threshold: ");
  Serial.println(contactThreshold);
  Serial.println("When FSR > threshold = Sensor attached");
  
  dog.heartRateBPM = 0;
  dog.acousticHeartRateBPM = 0;
  dog.isProperlyAttached = false;
  dog.activityLevel = 0;
  dog.abnormalRhythm = false;
  dog.confidence = 0.0;
  
  Serial.println("Calibration Complete!");
  Serial.println("=====================\n");
}

// === MAIN LOOP ===
void loop() {
  // Check if sensor is properly attached to dog
  checkAttachment();
  
  // Only collect data if properly attached
  if(dog.isProperlyAttached) {
    readHeartRate();
    readActivity();
    analyzeHeartSounds();
    calculateConfidence();
    
    displayDashboard();
    checkAlerts();
  } else {
    Serial.println("⚠️  Sensor not properly attached to dog!");
    Serial.println("Press FSR or attach to dog to begin monitoring.");
    delay(1000);
  }
  
  delay(500); // Update every 0.5 seconds
}

// === CHECK SENSOR ATTACHMENT (FSR) ===
void checkAttachment() {
  int fsrValue = analogRead(FSR_PIN);
  
  // Simple threshold detection
  // Higher FSR value = more pressure = better contact
  dog.isProperlyAttached = (fsrValue > 1000); // Adjust threshold as needed
  
  // Optional: Add debouncing
  static int attachmentCount = 0;
  if(dog.isProperlyAttached) {
    attachmentCount = min(attachmentCount + 1, 5);
  } else {
    attachmentCount = max(attachmentCount - 1, 0);
  }
  
  dog.isProperlyAttached = (attachmentCount >= 3);
}

// === HEART RATE MONITOR (Pulse Sensor) ===
void readHeartRate() {
  static unsigned long lastBeat = 0;
  static int beatCount = 0;
  static int pulseBuffer[10] = {0};
  static int bufferIndex = 0;
  
  int pulse = analogRead(PULSE_PIN);
  
  // Store in buffer for smoothing
  pulseBuffer[bufferIndex] = pulse;
  bufferIndex = (bufferIndex + 1) % 10;
  
  // Calculate moving average
  int pulseAvg = 0;
  for(int i = 0; i < 10; i++) {
    pulseAvg += pulseBuffer[i];
  }
  pulseAvg /= 10;
  
  // Detect heartbeat peaks
  static int lastPulse = 0;
  static bool wasRising = false;
  
  bool isRising = (pulseAvg > lastPulse);
  
  // Peak detection: rising → falling transition
  if(wasRising && !isRising && pulseAvg > 2000) {
    unsigned long currentTime = millis();
    
    if(lastBeat > 0) {
      long interval = currentTime - lastBeat;
      if(interval > 300 && interval < 1500) { // 40-200 BPM range
        beatCount++;
        
        if(beatCount >= 5) { // Calculate BPM every 5 beats
          dog.heartRateBPM = 60000 / (interval / beatCount);
          beatCount = 0;
        }
      }
    }
    lastBeat = currentTime;
  }
  
  wasRising = isRising;
  lastPulse = pulseAvg;
}

// === ACTIVITY MONITOR (Piezo) ===
void readActivity() {
  int piezoValue = analogRead(PIEZO_PIN);
  
  // Simple activity level calculation
  static int activityBuffer[5] = {0};
  static int actIndex = 0;
  
  activityBuffer[actIndex] = abs(piezoValue - 2048); // Remove DC offset
  actIndex = (actIndex + 1) % 5;
  
  long activitySum = 0;
  for(int i = 0; i < 5; i++) {
    activitySum += activityBuffer[i];
  }
  
  dog.activityLevel = activitySum / 5;
}

// === HEART SOUND ANALYSIS (Microphone) ===
void analyzeHeartSounds() {
  int16_t buffer[128];
  size_t bytesRead = 0;
  
  i2s_read(I2S_NUM_0, buffer, sizeof(buffer), &bytesRead, 10);
  
  if(bytesRead > 0) {
    int samples = bytesRead / 2;
    long energy = 0;
    int peaks = 0;
    
    // Simple analysis
    for(int i = 1; i < samples - 1; i++) {
      energy += abs(buffer[i]);
      
      // Count peaks (crude heart sound detection)
      if(buffer[i] > buffer[i-1] && buffer[i] > buffer[i+1] && buffer[i] > 1000) {
        peaks++;
      }
    }
    energy /= samples;
    
    // Detect potential abnormalities
    dog.abnormalRhythm = (energy > 4000);
    
    // Optional: Calculate acoustic heart rate
    if(peaks > 2) {
      dog.acousticHeartRateBPM = peaks * 6; // Rough estimate
    }
  }
}

// === DATA QUALITY CONFIDENCE ===
void calculateConfidence() {
  float confidence = 0.0;
  
  // Factor 1: Good contact (FSR)
  if(dog.isProperlyAttached) confidence += 0.3;
  
  // Factor 2: Stable heart rate reading
  if(dog.heartRateBPM > 60 && dog.heartRateBPM < 140) confidence += 0.4;
  
  // Factor 3: Low activity (better readings when dog is calm)
  if(dog.activityLevel < 500) confidence += 0.2;
  
  // Factor 4: Acoustic correlation
  if(abs(dog.heartRateBPM - dog.acousticHeartRateBPM) < 20) confidence += 0.1;
  
  // FIXED: Use 1.0f instead of 1.0 to make it a float
  dog.confidence = confidence;
  if(dog.confidence > 1.0f) {
    dog.confidence = 1.0f;
  }
}

// === DISPLAY ===
void displayDashboard() {
  Serial.println("\n--- DOG HEARTBEAT MONITOR ---");
  Serial.print("❤️  Heart Rate: ");
  if(dog.heartRateBPM > 0) {
    Serial.print(dog.heartRateBPM);
    Serial.print(" BPM");
  } else {
    Serial.print("---");
  }
  
  Serial.print(" | 🎵 Sound Rate: ");
  if(dog.acousticHeartRateBPM > 0) {
    Serial.print(dog.acousticHeartRateBPM);
    Serial.print(" BPM");
  } else {
    Serial.print("---");
  }
  
  Serial.print(" | 📊 Activity: ");
  Serial.print(dog.activityLevel);
  if(dog.activityLevel > 1000) {
    Serial.print(" (Active)");
  } else {
    Serial.print(" (Calm)");
  }
  
  Serial.print(" | 🔗 Attachment: ");
  Serial.print(dog.isProperlyAttached ? "Good" : "Poor");
  
  Serial.print(" | ✅ Confidence: ");
  Serial.print(int(dog.confidence * 100));
  Serial.print("%");
  
  if(dog.abnormalRhythm) {
    Serial.print(" | ⚠️  Abnormal rhythm");
  }
  
  Serial.println();
}

// === ALERT SYSTEM ===
void checkAlerts() {
  bool alert = false;
  
  if(dog.heartRateBPM > 0) {
    if(dog.heartRateBPM < 60 || dog.heartRateBPM > 140) {
      Serial.println("🚨 ALERT: Abnormal heart rate detected!");
      alert = true;
    }
  }
  
  if(dog.abnormalRhythm) {
    Serial.println("🚨 ALERT: Irregular heart rhythm detected!");
    alert = true;
  }
  
  if(dog.confidence < 0.5) {
    Serial.println("⚠️  Warning: Low confidence in readings");
    Serial.println("   Check sensor placement and dog's activity level");
  }
  
  if(alert && dog.confidence > 0.6) {
    Serial.println("📞 Consider consulting a veterinarian");
    Serial.println("-----------------------------");
  }
}

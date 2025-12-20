
#include <Arduino.h>
#include <driver/i2s.h>

/* PINS */
#define PIN_PULSE   8
#define PIN_PIEZO   12
#define PIN_FSR     9
#define PIN_LED     21

#define I2S_SCK     7
#define I2S_WS      15
#define I2S_SD      6

/*  CONSTANTS  */
#define BPM_MIN 40
#define BPM_MAX 250

#define FSR_ATTACHED_THRESHOLD   400
#define PIEZO_ACTIVITY_THRESHOLD 1500
#define AUDIO_ACTIVITY_THRESHOLD 500

#define HEARTBEAT_TIMEOUT 3000
#define REQUIRED_VALID_BEATS 3

/*  SYSTEM STATES  */
enum SystemState {
  NO_DOG,
  WEAK_SIGNAL,
  MONITORING
};

SystemState systemState = NO_DOG;

/*  GLOBAL VARIABLES  */
int pulseRaw = 0;
int piezoRaw = 0;
int fsrRaw   = 0;
int audioEnergy = 0;

int bpm = 0;
int lastStableBPM = 0;

unsigned long lastBeatTime = 0;
unsigned long lastValidBeatTime = 0;

int validBeatCount = 0;
bool beatDetected = false;

int pulseBaseline = 2000;
int pulseThreshold = 3000;

/*  FUNCTION DECLARATIONS  */
void setupMicrophone();
int readMicrophoneEnergy();

void calibratePulseSensor();
bool isDogReallyPresent();
bool isSystemQuiet();

bool detectHeartbeat();
void updateBPM();

void updateSystemState();
void updateLED();

void printDashboard();
void checkHealthStatus();

/*  SETUP  */
void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_PULSE, INPUT);

  setupMicrophone();
  calibratePulseSensor();

  Serial.println("\nDOG HEARTBEAT TRACKER READY\n");
}

/*  LOOP  */
void loop() {

  /*  READ & SMOOTH SENSORS  */
  pulseRaw = (pulseRaw * 3 + analogRead(PIN_PULSE)) / 4;
  piezoRaw = (piezoRaw * 3 + analogRead(PIN_PIEZO)) / 4;
  fsrRaw   = (fsrRaw   * 3 + analogRead(PIN_FSR))   / 4;
  audioEnergy = readMicrophoneEnergy();

  /*  IDLE / QUIET RESET  */
  if (isSystemQuiet()) {
    bpm = 0;
    validBeatCount = 0;
  }

  /*  HEARTBEAT DETECTION  */
  if (detectHeartbeat()) {
    updateBPM();
  }

  /*  HEARTBEAT TIMEOUT  */
  if (millis() - lastValidBeatTime > HEARTBEAT_TIMEOUT) {
    bpm = 0;
    validBeatCount = 0;
  }

  /*  SYSTEM MANAGEMENT  */
  updateSystemState();
  updateLED();

  static unsigned long lastUI = 0;
  if (millis() - lastUI > 2000) {
    printDashboard();
    lastUI = millis();
  }

  if (systemState == MONITORING) {
    checkHealthStatus();
  }

  delay(10);
}

/*  DOG PRESENCE  */
bool isDogReallyPresent() {
  return (fsrRaw > FSR_ATTACHED_THRESHOLD) &&
         (piezoRaw > 800 || audioEnergy > 300);
}

/*  QUIET STATE  */
bool isSystemQuiet() {
  return (fsrRaw < FSR_ATTACHED_THRESHOLD) &&
         (piezoRaw < 500) &&
         (audioEnergy < 200);
}

/*  HEARTBEAT DETECTION  */
bool detectHeartbeat() {

  if (!isDogReallyPresent()) return false;

  bool pulsePeak = pulseRaw > pulseThreshold;
  bool piezoValid = piezoRaw > PIEZO_ACTIVITY_THRESHOLD;
  bool audioValid = audioEnergy > AUDIO_ACTIVITY_THRESHOLD;

  if (pulsePeak && (piezoValid || audioValid)) {
    beatDetected = true;
    return true;
  }

  return false;
}

/*  BPM CALCULATION  */
void updateBPM() {
  unsigned long now = millis();

  if (lastBeatTime > 0) {
    unsigned long interval = now - lastBeatTime;

    if (interval > 300 && interval < 1800) {
      int newBPM = 60000 / interval;

      if (newBPM >= BPM_MIN && newBPM <= BPM_MAX) {

        if (bpm == 0 || abs(newBPM - lastStableBPM) < 20) {
          validBeatCount++;
        } else {
          validBeatCount = 0;
        }

        if (validBeatCount >= REQUIRED_VALID_BEATS) {
          lastStableBPM = newBPM;
          bpm = (bpm * 2 + newBPM) / 3;
          lastValidBeatTime = now;
        }
      }
    }
  }

  lastBeatTime = now;
}

/*  SYSTEM STATE  */
void updateSystemState() {

  if (!isDogReallyPresent()) {
    systemState = NO_DOG;
    bpm = 0;
    validBeatCount = 0;
    return;
  }

  if (bpm == 0) {
    systemState = WEAK_SIGNAL;
  } else {
    systemState = MONITORING;
  }
}

/*  LED FEEDBACK  */
void updateLED() {
  static unsigned long lastBlink = 0;
  static bool ledState = false;

  switch (systemState) {
    case NO_DOG:
      if (millis() - lastBlink > 1000) {
        ledState = !ledState;
        digitalWrite(PIN_LED, ledState);
        lastBlink = millis();
      }
      break;

    case WEAK_SIGNAL:
      if (millis() - lastBlink > 250) {
        ledState = !ledState;
        digitalWrite(PIN_LED, ledState);
        lastBlink = millis();
      }
      break;

    case MONITORING:
      if (beatDetected) {
        digitalWrite(PIN_LED, HIGH);
        delay(40);
        digitalWrite(PIN_LED, LOW);
        beatDetected = false;
      }
      break;
  }
}

/*  HEALTH STATUS  */
void checkHealthStatus() {
  if (bpm < 60) {
    Serial.println("⚠️ LOW HEART RATE");
  } else if (bpm > 160) {
    Serial.println("🚨 HIGH HEART RATE");
  }
}

/*  DASHBOARD  */
void printDashboard() {
  Serial.println("\n==============================");
  Serial.println("DOG HEALTH TRACKER - LIVE");
  Serial.println("==============================");

  Serial.print("Pulse: "); Serial.println(pulseRaw);
  Serial.print("Piezo: "); Serial.println(piezoRaw);
  Serial.print("Audio Energy: "); Serial.println(audioEnergy);
  Serial.print("FSR: "); Serial.println(fsrRaw);

  Serial.print("Heart Rate: ");
  bpm > 0 ? Serial.print(bpm) : Serial.print("--");
  Serial.println(" BPM");

  Serial.print("State: ");
  switch (systemState) {
    case NO_DOG: Serial.println("No Dog"); break;
    case WEAK_SIGNAL: Serial.println("Weak Signal"); break;
    case MONITORING: Serial.println("Monitoring"); break;
  }
}

/* = CALIBRATION  */
void calibratePulseSensor() {
  long sum = 0;

  for (int i = 0; i < 50; i++) {
    sum += analogRead(PIN_PULSE);
    delay(20);
  }

  int ambient = sum / 50;
  pulseBaseline = ambient + 400;
  pulseThreshold = ambient + 800;

  Serial.println("Pulse sensor calibrated");
}

/*  MICROPHONE */
void setupMicrophone() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 128,
    .use_apll = false
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
}

int readMicrophoneEnergy() {
  int16_t samples[128];
  size_t bytesRead;

  i2s_read(I2S_NUM_0, samples, sizeof(samples), &bytesRead, 0);

  long sum = 0;
  int count = bytesRead / 2;

  for (int i = 0; i < count; i++) {
    sum += abs(samples[i]);
  }

  if (count == 0) return 0;
  return sum / count;
}

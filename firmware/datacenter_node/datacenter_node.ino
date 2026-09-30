#define TINY_GSM_MODEM_SIM800 

#include <WiFi.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include "DHT.h"
#include <PZEM004Tv30.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define MODEM_RST            5      
#define MODEM_PWKEY          4      
#define MODEM_POWER_ON       23     
#define MODEM_TX             27     
#define MODEM_RX             26  
#define DHTPIN               13
#define DHTTYPE              DHT11 
#define PZEM_RX_PIN          19
#define PZEM_TX_PIN          18
#define WaterLeakSensor      14
#define VibrationSensor      15
#define GAZPIN               34
#define FLAMEPIN             2
#define DOORPIN              33

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

/*********************************/
const float tempLimitMAX = 40.0;
const float tempLimitMIN = 10.0;
const float humidityLimit = 70.0;
const float voltageLimit = 245.0;
const float currentLimit = 15.0;
const int gazThreshold   = 100;
/*********************************/

const char *ssid = " ... ";
const char *password = " ... ";

const char apn[]      = "insert apn"; 
const char gprsUser[] = "";
const char gprsPass[] = "";

const char *mqtt_server = " ... ";
const int mqtt_port = 1883;

const char target_phone_number[] = " ... "; 

enum NetworkState {
  STATE_INIT_WIFI,
  STATE_RUNNING_WIFI,
  STATE_START_CELLULAR,
  STATE_WAITING_MODEM_BOOT,
  STATE_CONNECTING_GPRS,
  STATE_RUNNING_CELLULAR
};

enum SensorsState {
  READ_AND_VERIFY,
  SEND_ROUTINE,
  SEND_ALERT,
  CLEAR_ALERT
};

enum AlertType {
  None,
  DoorOpened, 
  GazFound, 
  FlameFound, 
  VoltageHigh, 
  CurrentHigh, 
  CurrentCut, 
  TemperatureHigh, 
  TemperatureLow, 
  HumidityHigh, 
  VibrationFound,
  WaterFound,
  ALERT_TYPE_COUNT
};

NetworkState currentState = STATE_INIT_WIFI;
SensorsState currentState2 = READ_AND_VERIFY;

WiFiClient bstWiFiClient;
HardwareSerial modemSerial(1); 
TinyGsm modem(modemSerial);
TinyGsmClient bstGPRSClient(modem);
PubSubClient client;
DHT dht(DHTPIN, DHTTYPE);

PZEM004Tv30* pzem = nullptr;

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

unsigned long stateTimer = 0;
unsigned long lastMsg = 0;
unsigned long lastWiFiCheck = 0;
unsigned long lastMQTTRetry = 0;
unsigned long lastSlowReading = 0;
unsigned long lastFastReading = 0;
unsigned long lastVerification = 0;
unsigned long lastMQTTPub = 0;
unsigned long lastDisplayUpdate = 0;
bool oledReady = false;
bool SMSCurrentSent = false;
bool firstValidReadingDone = false;
int invalidVoltageCount = 0;
const int CURRENT_CUT_THRESHOLD = 3;
bool modemPoweredOn = false;
bool modemReady = false;
unsigned long lastSMSRetry = 0;
int smsRetryCount = 0;
const int MAX_SMS_RETRIES = 3;
uint16_t activeAlertBits = 0;
uint16_t reportedAlertBits = 0;
AlertType pendingAlertType = None;
bool pendingAlertActive = false;
float temperature = 20;
float humidity = 70;
int WaterDetected = 0;
int VibrationDetected = 0;
int GazDetected = 0;
int FlameDetected = 0;
int DoorOpen = 0;
float voltage = 0;
float current = 0;
float powerFactor = 0;

const unsigned char PROGMEM icon_flame[] = {
  0x0c,0x00, 0x1e,0x00, 0x3f,0x00, 0x37,0x80, 0x73,0x80, 0x61,0xc0,
  0x70,0xc0, 0x38,0xc0, 0x1d,0xc0, 0x0f,0x80, 0x07,0x00, 0x02,0x00
};

const unsigned char PROGMEM icon_water_drop[] = {
  0x0c,0x00, 0x0c,0x00, 0x1e,0x00, 0x1e,0x00, 0x3f,0x00, 0x3f,0x00,
  0x7f,0x80, 0x7f,0x80, 0x7f,0x80, 0x3f,0x00, 0x3f,0x00, 0x1e,0x00
};

const unsigned char PROGMEM icon_gas_warning[] = {
  0x06,0x00, 0x0f,0x00, 0x0f,0x00, 0x1f,0x80, 0x16,0x80, 0x36,0xc0,
  0x36,0xc0, 0x6f,0x60, 0x60,0x60, 0xcf,0x30, 0xff,0xf0, 0x00,0x00
};

const unsigned char PROGMEM icon_door_open[] = {
  0xff,0xf0, 0x80,0x00, 0xa0,0x00, 0x90,0x00, 0x88,0x00, 0x84,0x00,
  0x86,0x00, 0x81,0x00, 0x80,0x80, 0x80,0x40, 0x80,0x20, 0xff,0xf0
};

const unsigned char PROGMEM icon_lightning[] = {
  0x03,0x00, 0x06,0x00, 0x0c,0x00, 0x18,0x00, 0x3f,0xe0, 0x7f,0xc0,
  0x01,0x80, 0x03,0x00, 0x06,0x00, 0x0c,0x00, 0x18,0x00, 0x30,0x00
};

const unsigned char PROGMEM icon_power_cut[] = {
  0x83,0x00, 0x46,0x00, 0x2c,0x00, 0x18,0x00, 0x3f,0xe0, 0x7f,0xc0,
  0x03,0x80, 0x03,0x00, 0x06,0x80, 0x0c,0x40, 0x18,0x20, 0x30,0x10
};

const unsigned char PROGMEM icon_temp_high[] = {
  0x0c,0x00, 0x1e,0x00, 0x12,0x00, 0x1e,0x00, 0x12,0x00, 0x1e,0x00,
  0x12,0x00, 0x3f,0x00, 0x7f,0x80, 0x7f,0x80, 0x7f,0x80, 0x3f,0x00
};

const unsigned char PROGMEM icon_snowflake[] = {
  0x24,0x80, 0x15,0x00, 0x4e,0x40, 0x2e,0x80, 0xf5,0xe0, 0x04,0x00,
  0xf5,0xe0, 0x2e,0x80, 0x4e,0x40, 0x15,0x00, 0x24,0x80, 0x00,0x00
};

const unsigned char PROGMEM icon_humidity_percent[] = {
  0x0c,0x00, 0x1e,0x00, 0x3f,0x00, 0x7f,0x80, 0x69,0x40, 0x52,0x80,
  0x4a,0x80, 0x52,0x80, 0x24,0x80, 0x3f,0xc0, 0x1f,0x80, 0x0f,0x00
};

const unsigned char PROGMEM icon_vibration[] = {
  0x21,0x00, 0x4f,0x20, 0x90,0x90, 0x16,0x80, 0x16,0x80, 0x10,0x80,
  0x16,0x80, 0x16,0x80, 0x90,0x90, 0x4f,0x20, 0x21,0x00, 0x00,0x00
};

const unsigned char PROGMEM icon_current_high[] = {
  0x0c,0x00, 0x1e,0x00, 0x33,0x00, 0x0c,0x00, 0x18,0x00, 0x3f,0x80,
  0x7f,0x00, 0x06,0x00, 0x0c,0x00, 0x18,0x00, 0x30,0x00, 0x00,0x00
};

const unsigned char PROGMEM icon_wifi[] = {
  0x00,0x00, 0x7f,0xe0, 0xc0,0x00, 0x3f,0xc0, 0x60,0x60, 0x0f,0x00,
  0x10,0x80, 0x0f,0x00, 0x06,0x00, 0x06,0x00, 0x00,0x00, 0x00,0x00
};

const unsigned char PROGMEM icon_cellular[] = {
  0x00,0x00, 0x00,0x30, 0x00,0x70, 0x00,0xf0, 0x03,0xf0, 0x03,0xf0,
  0x0f,0xf0, 0x0f,0xf0, 0x3f,0xf0, 0x3f,0xf0, 0x00,0x00, 0x00,0x00
};

const unsigned char* const alertIcons[ALERT_TYPE_COUNT] = {
  nullptr,
  icon_door_open,
  icon_gas_warning,
  icon_flame,
  icon_lightning,
  icon_current_high,
  icon_power_cut,
  icon_temp_high,
  icon_snowflake,
  icon_humidity_percent,
  icon_vibration,
  icon_water_drop
};

void drawCenteredText(String text, int y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, y);
  display.print(text);
}

void updateDisplay() {
  if (!oledReady) return;
  unsigned long now = millis();
  if (now - lastDisplayUpdate < 200) return;
  lastDisplayUpdate = now;

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);

  String line1 = String((int)voltage) + "V  " + String(current, 1) + "A  PF:" + String(powerFactor, 2);
  drawCenteredText(line1, 0);

  String line2 = "T:" + String(temperature, 1) + "C  H:" + String((int)humidity) + "%";
  drawCenteredText(line2, 14);

  display.drawFastHLine(0, 26, 128, SH110X_WHITE);

  int x = 0;
  int y = 32;
  for (int i = 1; i < ALERT_TYPE_COUNT; i++) {
    if (activeAlertBits & (1 << i)) {
      display.drawBitmap(x, y, alertIcons[i], 12, 12, SH110X_WHITE);
      x += 18;
      if (x > 108) {
        x = 0;
        y += 16;
      }
    }
  }

  bool onWiFi = (currentState == STATE_INIT_WIFI) || (currentState == STATE_RUNNING_WIFI);
  display.drawBitmap(SCREEN_WIDTH - 14, 0, onWiFi ? icon_wifi : icon_cellular, 12, 12, SH110X_WHITE);

  display.display();
}

void Network_FSM (unsigned long now){
  switch (currentState) {

    case STATE_INIT_WIFI:
      WiFi.begin(ssid, password);
      stateTimer = now;
      currentState = STATE_RUNNING_WIFI;
      break;

    case STATE_RUNNING_WIFI:
       if (WiFi.status() == WL_CONNECTED) {
        client.setClient(bstWiFiClient);
       
        if (!client.connected() && (now - lastMQTTRetry > 5000)) {
          lastMQTTRetry = now;
          String clientId = "ESP32Client-";
          clientId += String(random(0, 0xffff), HEX);
          client.connect(clientId.c_str());
        }
      } 
      else if (WiFi.status() != WL_CONNECTED && (now - stateTimer > 10000)) {
        currentState = STATE_START_CELLULAR;
      }
      break;

    case STATE_START_CELLULAR:
      modemReady = false;
      pinMode(MODEM_PWKEY, OUTPUT);
      pinMode(MODEM_RST, OUTPUT);
      pinMode(MODEM_POWER_ON, OUTPUT);
      
      digitalWrite(MODEM_POWER_ON, HIGH); 
      modemPoweredOn = true;
      digitalWrite(MODEM_RST, HIGH);
      delay(100);
      digitalWrite(MODEM_RST, LOW);
      delay(100);
      digitalWrite(MODEM_RST, HIGH);
      
      digitalWrite(MODEM_PWKEY, LOW);
      delay(1000);
      digitalWrite(MODEM_PWKEY, HIGH);

      stateTimer = now;
      currentState = STATE_WAITING_MODEM_BOOT;
      break;

    case STATE_WAITING_MODEM_BOOT:
      if (now - stateTimer >= 4000) {
          modemSerial.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
          currentState = STATE_CONNECTING_GPRS;
          stateTimer = now;
      }
      break;

    case STATE_CONNECTING_GPRS:
      if (!modem.restart()) {
        currentState = STATE_START_CELLULAR;
        break;
      }
      if (!modem.waitForNetwork(5000L)) {
        currentState = STATE_START_CELLULAR;
        break;
      }
      modemReady = true;
      modem.sendSMS(target_phone_number, "ALERTE : Le WiFi est coupe!");

      if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
        currentState = STATE_START_CELLULAR;
        break;
      }
      client.setClient(bstGPRSClient); 
      currentState = STATE_RUNNING_CELLULAR;
      break;

    case STATE_RUNNING_CELLULAR :
      if (!client.connected() && (now - lastMQTTRetry > 5000)) {
        lastMQTTRetry = now;
        String clientId = "ESP32Client-";
        clientId += String(random(0, 0xffff), HEX);
        client.connect(clientId.c_str());
      }

      if (now - lastWiFiCheck > 15000) { 
        lastWiFiCheck = now;
        if (WiFi.status() == WL_CONNECTED ) {
          modem.gprsDisconnect();
          digitalWrite(MODEM_POWER_ON, LOW);
          modemPoweredOn = false;
          modemReady = false;
          currentState = STATE_INIT_WIFI;
        }
      } 
      break;
  }
  if (client.connected()) {
    client.loop();
  }
}

void Sensors_FSM(unsigned long now){
  switch (currentState2){

    case READ_AND_VERIFY : {
      read_sensors(now);
      activeAlertBits = verifyAlerts();
      uint16_t changed = activeAlertBits ^ reportedAlertBits;

      if (changed != 0) {
        for (int i = 1; i < ALERT_TYPE_COUNT; i++) {
          uint16_t mask = (1 << i);
          if (changed & mask) {
            pendingAlertType = (AlertType)i;
            pendingAlertActive = (activeAlertBits & mask) != 0;
            currentState2 = pendingAlertActive ? SEND_ALERT : CLEAR_ALERT;
            break;
          }
        }
      }
      else if ((now - lastMQTTPub) >= 5000){
        currentState2 = SEND_ROUTINE;
      }
      else if ((activeAlertBits & (1 << CurrentCut)) && !SMSCurrentSent
                && (now - lastSMSRetry >= 10000) && smsRetryCount < MAX_SMS_RETRIES) {
        lastSMSRetry = now;
        smsRetryCount++;
        SMSCurrentSent = sendUrgentSMS("URGENT : Coupure d'electricite secteur !");
      }
      break;
    }

    case SEND_ALERT :
      publish_alerts(pendingAlertType, true);
      if (pendingAlertType == CurrentCut && !SMSCurrentSent) {
        SMSCurrentSent = sendUrgentSMS("URGENT : Coupure d'electricite secteur !");
      }
      reportedAlertBits |= (1 << pendingAlertType);
      currentState2 = READ_AND_VERIFY;
      break;

    case CLEAR_ALERT :
      publish_alerts(pendingAlertType, false);
      reportedAlertBits &= ~(1 << pendingAlertType);
      if (pendingAlertType == CurrentCut) {
        SMSCurrentSent = false;
        smsRetryCount = 0;
      }
      currentState2 = READ_AND_VERIFY;
      break;

    case SEND_ROUTINE :
      publish_infos();
      lastMQTTPub = now;
      currentState2 = READ_AND_VERIFY;
      break;
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, PZEM_RX_PIN, PZEM_TX_PIN);
  pzem = new PZEM004Tv30(Serial2, PZEM_RX_PIN, PZEM_TX_PIN);

  client.setServer(mqtt_server, mqtt_port);
  dht.begin();
  
  pinMode(WaterLeakSensor, INPUT_PULLDOWN);
  pinMode(VibrationSensor, INPUT_PULLDOWN);
  analogReadResolution(12);
  analogSetPinAttenuation(GAZPIN, ADC_11db);
  pinMode(GAZPIN, INPUT);
  pinMode(FLAMEPIN, INPUT_PULLDOWN);
  pinMode(DOORPIN, INPUT_PULLUP);  

  Wire.begin(21, 22);
  oledReady = display.begin(SCREEN_ADDRESS, true);
}

void loop() {
  unsigned long now = millis();
  Network_FSM (now);
  Sensors_FSM(now);
  updateDisplay();
}

void read_sensors(unsigned long now){
  if ((now - lastSlowReading) >= 2000){
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t)) temperature = t;
    if (!isnan(h)) humidity = h;

    if (pzem != nullptr) {
      float v = pzem->voltage();
      float a = pzem->current();
      float pf = pzem->pf();

      bool voltageValid = !isnan(v) && v > 100.0 && v < 265.0;
      bool currentValid  = !isnan(a) && a >= 0.0 && a < 30.0;

      if (voltageValid) {
        voltage = v;
        firstValidReadingDone = true;
        invalidVoltageCount = 0;
      } else {
        voltage = 0.0;
        invalidVoltageCount++;
      }
      current = (voltageValid && currentValid) ? a : 0.0;
      powerFactor = (voltageValid && !isnan(pf)) ? pf : 0.0;
    }
    lastSlowReading = now;

  }
  if ((now - lastFastReading) >= 200){
    WaterDetected = digitalRead(WaterLeakSensor);
    VibrationDetected = digitalRead(VibrationSensor);
    GazDetected = analogRead(GAZPIN);
    Serial.println(analogRead(34));    
    FlameDetected = digitalRead(FLAMEPIN);
    DoorOpen = digitalRead(DOORPIN);
    lastFastReading = now;

  }
    
}

void publish_infos() {
  if (!client.connected()) return;
  String payload = "{\"V\":"  + String(voltage, 2) + 
                   ",\"A\":"  + String(current, 2) + 
                   ",\"PF\":" + String(powerFactor, 2) + 
                   ",\"T\":"  + String(temperature, 2) + 
                   ",\"H\":"  + String(humidity, 2) + "}";
  client.publish("CCNS2026/telemetry/routine", payload.c_str());
}

void publish_alerts(AlertType type, bool active){
  if (!client.connected()) return;
  String message = "";
  if (active) {
    switch (type) {
      case FlameFound:       message = "FLAMME DETECTEE"; break;
      case GazFound:         message = "FUITE DE GAZ"; break;
      case WaterFound:       message = "INONDATION / EAU"; break;
      case TemperatureHigh:  message = "SURCHAUFFE TEMPERATURE"; break;
      case TemperatureLow:   message = "TEMPERATURE TROP BASSE"; break;
      case HumidityHigh:     message = "HUMIDITE CRITIQUE"; break;
      case VibrationFound:   message = "VIBRATION ANORMALE"; break;
      case DoorOpened:       message = "PORTE OUVERTE"; break;
      case VoltageHigh:      message = "SURTENSION ELECTRIQUE"; break;
      case CurrentHigh:      message = "SURINTENSITE ELECTRIQUE"; break;
      case CurrentCut:       message = "COUPURE DE COURANT SECTEUR"; break;
      default:               message = "ALERTE INCONNUE"; break;
    }
  } else {
    switch (type) {
      case FlameFound:       message = "FIN ALERTE : FLAMME CORRIGEE"; break;
      case GazFound:         message = "FIN ALERTE : GAZ DISSIPE"; break;
      case WaterFound:       message = "FIN ALERTE : PLUS D'EAU"; break;
      case TemperatureHigh:  message = "FIN ALERTE : TEMPERATURE NORMALE"; break;
      case TemperatureLow:   message = "FIN ALERTE : TEMPERATURE NORMALE"; break;
      case HumidityHigh:     message = "FIN ALERTE : HUMIDITE NORMALE"; break;
      case VibrationFound:   message = "FIN ALERTE : VIBRATION CORRIGEE"; break;
      case DoorOpened:       message = "FIN ALERTE : PORTE FERMEE"; break;
      case VoltageHigh:      message = "FIN ALERTE : TENSION NORMALE"; break;
      case CurrentHigh:      message = "FIN ALERTE : COURANT NORMAL"; break;
      case CurrentCut:       message = "FIN ALERTE : COURANT SECTEUR RETABLI"; break;
      default:               message = "FIN ALERTE : RETOUR A LA NORMALE"; break;
    }
  }
  client.publish("CCNS2026/alerts/critical", message.c_str());
}

bool sendUrgentSMS(const char* message) {
  if (modemPoweredOn && !modemReady) {
    return false;
  }

  bool openedSpecificallyForSMS = false;

  if (!modemPoweredOn) {
    openedSpecificallyForSMS = true;

    pinMode(MODEM_PWKEY, OUTPUT);
    pinMode(MODEM_RST, OUTPUT);
    pinMode(MODEM_POWER_ON, OUTPUT);

    digitalWrite(MODEM_POWER_ON, HIGH);
    digitalWrite(MODEM_RST, HIGH);
    delay(100);
    digitalWrite(MODEM_RST, LOW);
    delay(100);
    digitalWrite(MODEM_RST, HIGH);

    digitalWrite(MODEM_PWKEY, LOW);
    delay(1000);
    digitalWrite(MODEM_PWKEY, HIGH);
    delay(4000); 

    modemSerial.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
    modemPoweredOn = true;

    if (!modem.init() || !modem.waitForNetwork(10000L)) {
      digitalWrite(MODEM_POWER_ON, LOW);
      modemPoweredOn = false;
      return false;
    }
  }

  bool sent = modem.sendSMS(target_phone_number, message);

  if (openedSpecificallyForSMS) {
    digitalWrite(MODEM_POWER_ON, LOW);
    modemPoweredOn = false;
    modemReady = false;
  }

  return sent;
}
uint16_t verifyAlerts() {
  uint16_t bits = 0;
  if (FlameDetected == HIGH)      bits |= (1 << FlameFound);
  if (GazDetected > gazThreshold) bits |= (1 << GazFound);
  if (WaterDetected == HIGH)      bits |= (1 << WaterFound);
  if (temperature > tempLimitMAX) bits |= (1 << TemperatureHigh);
  if (temperature < tempLimitMIN) bits |= (1 << TemperatureLow);
  if (humidity > humidityLimit)   bits |= (1 << HumidityHigh);
  if (VibrationDetected == HIGH)  bits |= (1 << VibrationFound);
  if (DoorOpen == HIGH)           bits |= (1 << DoorOpened);
  if (pzem != nullptr) {
  if (firstValidReadingDone) {
    if (voltage > voltageLimit) bits |= (1 << VoltageHigh);
    if (current > currentLimit) bits |= (1 << CurrentHigh);
  }
  if (invalidVoltageCount >= CURRENT_CUT_THRESHOLD) bits |= (1 << CurrentCut);
  }
  return bits;
}

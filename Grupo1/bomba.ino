#include <WiFi.h>
#include <PubSubClient.h>

constexpr char* WIFI_SSID {"iot2026"};
constexpr char* WIFI_PASS {"Senhab0@"};
constexpr char* MQTT_BROKER {"10.150.207.154"};
constexpr int MQTT_PORT {1883};

constexpr int TEMPO_TOTAL {300}; // 5 minutos
constexpr int MAX_ERROS {3};
constexpr int NUM_MODULOS {6}; // 6 modulos

#define BTN_START 35
#define BTN_RESET 0

enum GameState {LOBBY, RUNNING, VICTORY, DEFEAT};
GameState state{LOBBY};

int tempoFaltante{TEMPO_TOTAL};
int erros{};
int acertos{};
char bombSerial[6] = "-----";

unsigned long lastTick{};

WiFiClient espClient;
PubSubClient mqttClient(espClient);

void
generateSerial(void)
{
  bombSerial[0] = 'A' + random(26);
  bombSerial[1] = 'A' + random(26);
  bombSerial[2] = '-';
  bombSerial[3] = '0' + random(10);
  bombSerial[4] = '0' + random(10);
  bombSerial[5] = '\0';
}

void
changeState(GameState newState)
{
  state = newState;

  if (state == LOBBY) {
    tempoFaltante = TEMPO_TOTAL;
    erros = 0;
    acertos = 0;
    strcpy(bombSerial, "-----");

    mqttClient.publish("jogo/serial", bombSerial, true);
    mqttClient.publish("jogo/estado", "RESET", true);
  } else if (state == RUNNING) {
    tempoFaltante = TEMPO_TOTAL;
    erros = 0;
    acertos = 0;
    lastTick = millis();

    generateSerial();
    mqttClient.publish("jogo/serial", bombSerial, true);
    mqttClient.publish("jogo/estado", "START", true);

    char buf[8];
    itoa(tempoFaltante, buf, 10);
    mqttClient.publish("jogo/tempo", buf, false);
  } else if (state == VICTORY) {
    mqttClient.publish("jogo/estado", "VICTORY", true);
  } else if (state == DEFEAT) {
    mqttClient.publish("jogo/estado", "DEFEAT", true);
  }
}

void
callbackMqtt(char* topic, byte* payload, unsigned int length)
{
  char msg[length+1];
  memcpy(msg, payload, length);
  msg[length] = '\0';

  if (state != RUNNING) return;

  if (strcmp(msg, "SOLVED") == 0) {
    acertos++;

    if (acertos >= NUM_MODULOS) {
      changeState(VICTORY);
    }
  } else if (strcmp(msg, "ERROR") == 0) {
    erros++;

    if (erros >= MAX_ERROS) {
      changeState(DEFEAT);
    }
  }
}

void
connectWifi(void)
{
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("Conectando wifi...");
    delay(200);
  }
  Serial.println("Conectado ao wifi");
}

void
connectMQTT(void)
{
  while (!mqttClient.connected()) {
    if (mqttClient.connect("Bomba")) {
      mqttClient.subscribe("jogo/modulo/+/evento");
    } else {
      delay(1000);
    }
  }
  Serial.println("Conectado ao broker");
}

void
setup(void)
{
  Serial.begin(9600);
  pinMode(BTN_START, INPUT);
  pinMode(BTN_RESET, INPUT_PULLUP);

  randomSeed(esp_random());

  connectWifi();

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(callbackMqtt);
  connectMQTT();

  changeState(LOBBY);
}

void
loop(void)
{
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();

  if (digitalRead(BTN_START) == LOW) {
    if (state == LOBBY) {
      changeState(RUNNING);
      delay(300);
    }
  }

  if (digitalRead(BTN_RESET) == LOW) {
    changeState(LOBBY);
    delay(300);
  }

  if (state == RUNNING) {
    if (millis() - lastTick >= 1000) {
      lastTick = millis();
      tempoFaltante--;

      char bufTempo[8];
      itoa(tempoFaltante, bufTempo, 10);
      mqttClient.publish("jogo/tempo", bufTempo, false);

      if (tempoFaltante <= 0) {
        changeState(DEFEAT);
      }
    }
  }
}


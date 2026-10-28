#include <WiFi.h>
#include <PubSubClient.h>
#include <TFT_eSPI.h>
#include <SPI.h>

constexpr char* WIFI_SSID {"POCOF5_7877"};
constexpr char* WIFI_PASS {"12345678"};
constexpr char* MQTT_BROKER {"10.186.162.138"};

// constexpr char* WIFI_SSID {"iot2026"};
// constexpr char* WIFI_PASS {"Senhab0@"};
// constexpr char* MQTT_BROKER {"10.150.207.154"};

constexpr int MQTT_PORT {1883};

constexpr int TEMPO_TOTAL {300}; // 5 minutos
constexpr int MAX_ERROS {3};
constexpr int NUM_MODULOS {6}; // 6 modulos

#define BTN_START 35
#define BTN_RESET 0
#define TFT_BL_PIN 4

enum GameState {NONE, LOBBY, RUNNING, VICTORY, DEFEAT};
GameState state{NONE};

int tempoFaltante{TEMPO_TOTAL};
int erros{};
int acertos{};
bool solvedModules[NUM_MODULOS + 2]{};
char bombSerial[6] = "-----";

unsigned long lastTick{};

WiFiClient espClient;
PubSubClient mqttClient(espClient);
TFT_eSPI tft = TFT_eSPI();

void
drawLobby(void)
{
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString("Bomba", 120, 10, 4);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawCentreString("AGUARDANDO INICIO", 120, 45, 2);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString("SN: -----", 120, 70, 2);
}

void
drawRunning(void)
{
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(10, 6, 2);
  tft.printf("SN: %s", bombSerial);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("ERROS:", 10, 108, 2);
}

void
updateRunningData(void)
{
  int min{tempoFaltante / 60};
  int seg{tempoFaltante % 60};

  char bufTempo[10];
  snprintf(bufTempo, sizeof(bufTempo), "%02d:%02d", min, seg);

  tft.setTextColor(tempoFaltante <= 60 ? TFT_RED : TFT_WHITE, TFT_BLACK);
  tft.drawCentreString(bufTempo, 120, 30, 7);

  tft.setCursor(65, 108, 2);
  for (int i = 0; i < MAX_ERROS; i++) {
    if (i < erros) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.print("X ");
    } else {
      tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft.print("_ ");
    }
  }

  char bufProg[10];
  snprintf(bufProg, sizeof(bufProg), "%d/%d", acertos, NUM_MODULOS);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawRightString(bufProg, 230, 108, 2);
}

void
drawFinish(bool vitoria)
{
  if (vitoria) {
    tft.fillScreen(TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.drawCentreString("DESARMADA!", 120, 20, 4);
    tft.drawCentreString("VITORIA GERAL", 120, 60, 4);
    tft.setTextColor(TFT_LIGHTGREY, TFT_DARKGREEN);
    tft.drawCentreString("Pressione GPIO 0 p/ Reset", 120, 105, 2);
  } else {
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.drawCentreString("BOOM!", 120, 15, 6);
    tft.drawCentreString("BOMBA DETONADA", 120, 70, 4);
    tft.setTextColor(TFT_BLACK, TFT_RED);
    tft.drawCentreString("Pressione GPIO 0 p/ Reset", 120, 110, 2);
  }
}

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
  if (newState == state) return;

  state = newState;

  if (state == LOBBY) {
    tempoFaltante = TEMPO_TOTAL;
    erros = 0;
    acertos = 0;
    memset(solvedModules, 0, sizeof(solvedModules));
    strcpy(bombSerial, "-----");

    mqttClient.publish("jogo/serial", bombSerial, true);
    mqttClient.publish("jogo/estado", "RESET", true);
    drawLobby();
  } else if (state == RUNNING) {
    tempoFaltante = TEMPO_TOTAL;
    erros = 0;
    acertos = 0;
    memset(solvedModules, 0, sizeof(solvedModules));
    lastTick = millis();

    generateSerial();
    mqttClient.publish("jogo/serial", bombSerial, true);
    mqttClient.publish("jogo/estado", "START", true);

    char buf[8];
    itoa(tempoFaltante, buf, 10);
    mqttClient.publish("jogo/tempo", buf, false);

    drawRunning();
    updateRunningData();
  } else if (state == VICTORY) {
    mqttClient.publish("jogo/estado", "VICTORY", true);
    drawFinish(1);
  } else if (state == DEFEAT) {
    mqttClient.publish("jogo/estado", "DEFEAT", true);
    drawFinish(0);
  }
}

void
callbackMqtt(char* topic, byte* payload, unsigned int length)
{
  char msg[length+1];
  memcpy(msg, payload, length);
  msg[length] = '\0';

  if (state != RUNNING) return;

  int modId = 0;
  if (sscanf(topic, "jogo/modulo/%d/evento", &modId) != 1) return;

  if (modId < 2 || modId > 7) return;

  if (solvedModules[modId]) {
    return;
  }

  if (strcmp(msg, "SOLVED") == 0) {
    acertos++;
    updateRunningData();
    solvedModules[modId] = 1;

    if (acertos >= NUM_MODULOS) {
      changeState(VICTORY);
    }
  } else if (strcmp(msg, "ERROR") == 0) {
    erros++;
    updateRunningData();

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
    if (mqttClient.connect("bomba")) {
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

  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_WHITE);

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

      updateRunningData();

      if (tempoFaltante <= 0) {
        changeState(DEFEAT);
      }
    }
  }
}

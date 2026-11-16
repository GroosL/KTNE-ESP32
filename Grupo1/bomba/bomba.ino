#include <WiFi.h>
#include <PubSubClient.h>
#include <TFT_eSPI.h>
#include <SPI.h>

constexpr const char* WIFI_SSID {"POCOF5_7877"};
constexpr const char* WIFI_PASS {"12345678"};
constexpr const char* MQTT_BROKER {"10.86.232.138"};

//constexpr const char* WIFI_SSID {"iot2022"};
//constexpr const char* WIFI_PASS {"S3nhab0@"};
//constexpr const char* MQTT_BROKER {"192.168.2.100"};

constexpr int MQTT_PORT {1883};

constexpr int TEMPO_TOTAL {300}; // 5 minutos
constexpr int MAX_ERROS {3};
constexpr int MAX_MODULOS {10};   // Módulos de 2 a 7

#define BTN_START 35
#define BTN_RESET 0
#define TFT_BL_PIN 4

enum GameState {NONE, LOBBY, RUNNING, VICTORY, DEFEAT};
GameState state{NONE};

int tempoFaltante{TEMPO_TOTAL};
int erros{};
int acertos{};
char bombSerial[6] = "-----";

int modulosDisponiveis[MAX_MODULOS]{};
int qtdModulos{0};
int indiceAtivo{0};

unsigned long lastTick{};

WiFiClient espClient;
PubSubClient mqttClient(espClient);
TFT_eSPI tft = TFT_eSPI();

void changeState(GameState newState);
void updateRunningData(void);

int
getModuloAtual(void)
{
  if (indiceAtivo >= 0 && indiceAtivo < qtdModulos) {
    return modulosDisponiveis[indiceAtivo];
  }
  return 0;
}

void
publicarModuloAtivo(int id)
{
  char buf[4];
  itoa(id, buf, 10);
  mqttClient.publish("jogo/modulo/ativo", buf, true);
}

void
updateLobbyModules(void)
{
  if (state != LOBBY) return;

  // Limpa a faixa inferior antes de redesenhar a lista
  tft.fillRect(0, 95, 240, 35, TFT_BLACK);

  if (qtdModulos == 0) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawCentreString("NENHUM MODULO ONLINE", 120, 102, 2);
    return;
  }

  char lista[48] = "ONLINE:";
  for (int i = 0; i < qtdModulos; i++) {
    char item[8];
    snprintf(item, sizeof(item), " G%d", modulosDisponiveis[i]);
    strncat(lista, item, sizeof(lista) - strlen(lista) - 1);
  }

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawCentreString(lista, 120, 102, 2);
}

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

  updateLobbyModules();
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
  char bufAtivo[16];
  int atual = getModuloAtual();
  if (atual > 0) {
    snprintf(bufAtivo, sizeof(bufAtivo), "ATUAL: G%d", atual);
  } else {
    snprintf(bufAtivo, sizeof(bufAtivo), "ATUAL: --");
  }
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawRightString(bufAtivo, 230, 6, 2);

  int min{tempoFaltante / 60};
  int seg{tempoFaltante % 60};

  char bufTempo[10];
  snprintf(bufTempo, sizeof(bufTempo), "%02d:%02d", min, seg);

  tft.setTextColor(tempoFaltante <= 60 ? TFT_RED : TFT_WHITE, TFT_BLACK);
  tft.drawCentreString(bufTempo, 120, 30, 7);

  // Strikes
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
  snprintf(bufProg, sizeof(bufProg), "%d/%d", acertos, qtdModulos);
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
adicionarModulo(int id)
{
  if (id < 2 || id > MAX_MODULOS + 1) return;

  for (int i = 0; i < qtdModulos; i++) {
    if (modulosDisponiveis[i] == id) return;
  }

  if (qtdModulos >= MAX_MODULOS) return;

  int pos = qtdModulos;
  while (pos > 0 && modulosDisponiveis[pos - 1] > id) {
    modulosDisponiveis[pos] = modulosDisponiveis[pos - 1];
    pos--;
  }
  modulosDisponiveis[pos] = id;
  qtdModulos++;

  Serial.printf("Modulo G%d conectado (Total: %d)\n", id, qtdModulos);

  if (state == LOBBY) {
    updateLobbyModules();
  } else if (state == RUNNING) {
    if (pos <= indiceAtivo) {
      indiceAtivo++;
    }
    updateRunningData();
  }
}

void
removerModulo(int id)
{
  for (int i = 0; i < qtdModulos; i++) {
    if (modulosDisponiveis[i] == id) {
      for (int j = i; j < qtdModulos - 1; j++) {
        modulosDisponiveis[j] = modulosDisponiveis[j + 1];
      }
      qtdModulos--;

      Serial.printf("Modulo G%d desconectado (Total: %d)\n", id, qtdModulos);

      if (state == LOBBY) {
        updateLobbyModules();
      } else if (state == RUNNING) {
        if (i < indiceAtivo) {
          indiceAtivo--;
        } else if (i == indiceAtivo) {
          if (indiceAtivo >= qtdModulos && acertos > 0) {
            changeState(VICTORY);
            return;
          }
          publicarModuloAtivo(getModuloAtual());
        }
        updateRunningData();
      }
      return;
    }
  }
}

void
encaminharDicaAleatoria(int origem, const char* conteudo)
{
  int candidatos[MAX_MODULOS]{};
  int qtdCandidatos{0};

  for (int i = 0; i < qtdModulos; i++) {
    if (modulosDisponiveis[i] != origem) {
      candidatos[qtdCandidatos++] = modulosDisponiveis[i];
    }
  }

  if (qtdCandidatos == 0) {
    Serial.printf("Dica de G%d descartada: nenhum outro modulo online.\n", origem);
    return;
  }

  int destino = candidatos[random(0, qtdCandidatos)];

  char topicoDestino[36];
  snprintf(topicoDestino, sizeof(topicoDestino), "jogo/modulo/%d/dica/%d", destino, origem);

  mqttClient.publish(topicoDestino, conteudo, false);
  Serial.printf("Dica roteada: G%d -> G%d [%s]\n", origem, destino, conteudo);
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
    indiceAtivo = 0;
    strcpy(bombSerial, "-----");

    publicarModuloAtivo(0);
    mqttClient.publish("jogo/serial", bombSerial, true);
    mqttClient.publish("jogo/estado", "RESET", true);
    drawLobby();
  } else if (state == RUNNING) {
    tempoFaltante = TEMPO_TOTAL;
    erros = 0;
    acertos = 0;
    indiceAtivo = 0;
    lastTick = millis();

    generateSerial();
    mqttClient.publish("jogo/serial", bombSerial, true);
    mqttClient.publish("jogo/estado", "START", true);
    publicarModuloAtivo(getModuloAtual());

    char buf[8];
    itoa(tempoFaltante, buf, 10);
    mqttClient.publish("jogo/tempo", buf, false);

    drawRunning();
    updateRunningData();
  } else if (state == VICTORY) {
    publicarModuloAtivo(0);
    mqttClient.publish("jogo/estado", "VICTORY", true);
    drawFinish(true);
  } else if (state == DEFEAT) {
    publicarModuloAtivo(0);
    mqttClient.publish("jogo/estado", "DEFEAT", true);
    drawFinish(false);
  }
}

void
callbackMqtt(char* topic, byte* payload, unsigned int length)
{
  char msg[length + 1];
  memcpy(msg, payload, length);
  msg[length] = '\0';

  while (length > 0 && (msg[length - 1] == '\r' || msg[length - 1] == '\n' || msg[length - 1] == ' ')) {
    msg[--length] = '\0';
  }

  int modId{0};
  int pos{0};

  if (sscanf(topic, "jogo/modulo/%d/status%n", &modId, &pos) == 1 && topic[pos] == '\0') {
    if (strcmp(msg, "ONLINE") == 0) {
      adicionarModulo(modId);
    } else if (strcmp(msg, "OFFLINE") == 0) {
      removerModulo(modId);
    }
    return;
  }

  if (state != RUNNING) return;

  if (sscanf(topic, "jogo/modulo/%d/dica%n", &modId, &pos) == 1 && topic[pos] == '\0') {
    encaminharDicaAleatoria(modId, msg);
    return;
  }

  if (sscanf(topic, "jogo/modulo/%d/evento%n", &modId, &pos) == 1 && topic[pos] == '\0') {
    if (modId != getModuloAtual()) return;

    if (strcmp(msg, "SOLVED") == 0) {
      acertos++;
      indiceAtivo++;

      if (indiceAtivo >= qtdModulos) {
        changeState(VICTORY);
      } else {
        publicarModuloAtivo(getModuloAtual());
        updateRunningData();
      }
    } else if (strcmp(msg, "ERROR") == 0) {
      erros++;
      updateRunningData();

      if (erros >= MAX_ERROS) {
        changeState(DEFEAT);
      }
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
      mqttClient.subscribe("jogo/modulo/+/status");
      mqttClient.subscribe("jogo/modulo/+/dica");
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
  Serial.begin(115200);
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
    if (state == LOBBY && qtdModulos > 0) {
      changeState(RUNNING);
      delay(300);
    }
  }

  if (digitalRead(BTN_RESET) == LOW) {
    if (state != LOBBY) {
      changeState(LOBBY);
      delay(300);
    }
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

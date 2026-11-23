#include <WiFi.h>
#include <PubSubClient.h>
#include <TFT_eSPI.h>
#include <SPI.h>

// Defina o numero do seu grupo aqui (2 a 11)
#define MODULO_ID "2"
#define MODULO_NUM 2

#define TFT_BL_PIN 4
#define MAX_ERROS 3
constexpr unsigned long TEMPO_DICA_MS = 20000;

constexpr const char* WIFI_SSID     = "iot2022";
constexpr const char* WIFI_PASSWORD = "S3nhab0@";
constexpr const char* MQTT_BROKER   = "192.168.2.100";
constexpr int         MQTT_PORT     = 1883;

WiFiClient espClient;
PubSubClient client(espClient);
TFT_eSPI tft = TFT_eSPI();

enum EstadoJogo { LOBBY, RUNNING, VICTORY, DEFEAT };
EstadoJogo estadoJogo = LOBBY;

// Variaveis de estado sincronizadas pela rede
char serialBomba[8]    = "-----";
char listaModulos[64]  = "NENHUM MODULO ONLINE";
int  tempoRestante     = 300;
int  erros             = 0;
int  acertos           = 0;
int  totalModulos      = 0;
int  moduloAtivo       = 0;

bool minhaVez          = false;
bool jaResolvido       = false;
bool entrouNaMinhaVez  = false;

// Variaveis para deteccao de dicas recebidas
bool temDicaNova          = false;
bool temDicaAtiva         = false;
unsigned long inicioDicaMs = 0;
int  grupoOrigemDica      = 0;
char conteudoDica[64]     = "";

// Flags para redesenhar o display no loop()
bool precisaDesenharTela   = false;
bool precisaAtualizarDados = false;

void updateLobbyModules() {
  if (estadoJogo != LOBBY) return;

  tft.fillRect(0, 95, 240, 35, TFT_BLACK);

  if (strcmp(listaModulos, "NENHUM MODULO ONLINE") == 0) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawCentreString(listaModulos, 120, 102, 2);
    return;
  }

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  uint8_t fonte = (tft.textWidth(listaModulos, 2) > 234) ? 1 : 2;
  int y = (fonte == 1) ? 106 : 102;
  tft.drawCentreString(listaModulos, 120, y, fonte);
}

void drawLobby() {
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString("Bomba", 120, 10, 4);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawRightString("G" MODULO_ID, 232, 6, 2);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawCentreString("AGUARDANDO INICIO", 120, 45, 2);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString("SN: -----", 120, 70, 2);

  updateLobbyModules();
}

void drawRunning() {
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("ERROS:", 10, 108, 2);
}

void updateRunningData() {
  tft.fillRect(10, 4, 115, 20, TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(10, 6, 2);
  tft.printf("SN: %s", serialBomba);

  char bufAtivo[16];
  if (moduloAtivo > 0) {
    snprintf(bufAtivo, sizeof(bufAtivo), "ATUAL: G%d", moduloAtivo);
  } else {
    snprintf(bufAtivo, sizeof(bufAtivo), "ATUAL: --");
  }
  tft.fillRect(130, 4, 105, 20, TFT_BLACK);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawRightString(bufAtivo, 230, 6, 2);

  int tempo = (tempoRestante < 0) ? 0 : tempoRestante;
  int min = tempo / 60;
  int seg = tempo % 60;

  char bufTempo[10];
  snprintf(bufTempo, sizeof(bufTempo), "%02d:%02d", min, seg);
  tft.setTextColor(tempo <= 60 ? TFT_RED : TFT_WHITE, TFT_BLACK);
  tft.drawCentreString(bufTempo, 120, 30, 7);

  tft.fillRect(65, 106, 90, 20, TFT_BLACK);
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

  char bufProg[16];
  snprintf(bufProg, sizeof(bufProg), "%d/%d", acertos, totalModulos);
  tft.fillRect(160, 106, 75, 20, TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawRightString(bufProg, 230, 108, 2);
}

void drawFinish(bool vitoria) {
  if (vitoria) {
    tft.fillScreen(TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.drawCentreString("DESARMADA!", 120, 20, 4);
    tft.drawCentreString("VITORIA GERAL", 120, 60, 4);
    tft.setTextColor(TFT_LIGHTGREY, TFT_DARKGREEN);
    tft.drawCentreString("Aguardando Reset", 120, 105, 2);
  } else {
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.drawCentreString("BOOM!", 120, 15, 6);
    tft.drawCentreString("BOMBA DETONADA", 120, 70, 4);
    tft.setTextColor(TFT_BLACK, TFT_RED);
    tft.drawCentreString("Aguardando Reset", 120, 110, 2);
  }
}

void drawDica() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawCentreString("DICA RECEBIDA", 120, 10, 2);

  char origem[20];
  snprintf(origem, sizeof(origem), "De: Grupo %d", grupoOrigemDica);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawCentreString(origem, 120, 35, 2);

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawCentreString(conteudoDica, 120, 75, 4);
}

void callbackMqtt(char* topic, byte* payload, unsigned int length) {
  char msg[64];
  unsigned int copyLen = length < (sizeof(msg) - 1) ? length : (sizeof(msg) - 1);
  memcpy(msg, payload, copyLen);
  msg[copyLen] = '\0';

  while (copyLen > 0 && (msg[copyLen - 1] == '\r' || msg[copyLen - 1] == '\n' || msg[copyLen - 1] == ' ')) {
    msg[--copyLen] = '\0';
  }

  // 1. Estados globais da partida
  if (strcmp(topic, "jogo/estado") == 0) {
    if (strcmp(msg, "START") == 0) {
      estadoJogo = RUNNING;
      jaResolvido = false;
      temDicaNova = false;
      temDicaAtiva = false;
      minhaVez = (moduloAtivo == MODULO_NUM);
      if (minhaVez) {
        entrouNaMinhaVez = true;
      } else {
        precisaDesenharTela = true;
      }
      // Resetar variaveis do desafio para nova partida
    } else if (strcmp(msg, "RESET") == 0) {
      estadoJogo = LOBBY;
      minhaVez = false;
      jaResolvido = false;
      temDicaNova = false;
      temDicaAtiva = false;
      entrouNaMinhaVez = false;
      erros = 0;
      acertos = 0;
      tempoRestante = 300;
      precisaDesenharTela = true;
    } else if (strcmp(msg, "VICTORY") == 0 || strcmp(msg, "DEFEAT") == 0) {
      estadoJogo = (strcmp(msg, "VICTORY") == 0) ? VICTORY : DEFEAT;
      minhaVez = false;
      temDicaNova = false;
      temDicaAtiva = false;
      entrouNaMinhaVez = false;
      precisaDesenharTela = true;
    }
  }

  // 2. Controle de turno (verifica se e a vez deste modulo)
  else if (strcmp(topic, "jogo/modulo/ativo") == 0) {
    moduloAtivo = atoi(msg);
    bool novaVez = (estadoJogo == RUNNING && moduloAtivo == MODULO_NUM);
    if (novaVez && !minhaVez) {
      entrouNaMinhaVez = true;
      temDicaAtiva = false;
    } else if (!novaVez && minhaVez && estadoJogo == RUNNING) {
      precisaDesenharTela = true;
    }
    minhaVez = novaVez;
    precisaAtualizarDados = true;
  }

  // 3. Serial, tempo, erros, acertos, total e modulos online
  else if (strcmp(topic, "jogo/serial") == 0) {
    strncpy(serialBomba, msg, sizeof(serialBomba) - 1);
    serialBomba[sizeof(serialBomba) - 1] = '\0';
    precisaAtualizarDados = true;
  } else if (strcmp(topic, "jogo/tempo") == 0) {
    tempoRestante = atoi(msg);
    precisaAtualizarDados = true;
  } else if (strcmp(topic, "jogo/erros") == 0) {
    erros = atoi(msg);
    precisaAtualizarDados = true;
  } else if (strcmp(topic, "jogo/acertos") == 0) {
    acertos = atoi(msg);
    precisaAtualizarDados = true;
  } else if (strcmp(topic, "jogo/total") == 0) {
    totalModulos = atoi(msg);
    precisaAtualizarDados = true;
  } else if (strcmp(topic, "jogo/modulos") == 0) {
    if (strcmp(msg, "0") == 0 || msg[0] == '\0') {
      strcpy(listaModulos, "NENHUM MODULO ONLINE");
    } else {
      strcpy(listaModulos, "ONLINE:");
      char copia[64];
      strncpy(copia, msg, sizeof(copia) - 1);
      copia[sizeof(copia) - 1] = '\0';
      char* tok = strtok(copia, " ");
      while (tok != nullptr) {
        char item[8];
        snprintf(item, sizeof(item), " G%s", tok);
        strncat(listaModulos, item, sizeof(listaModulos) - strlen(listaModulos) - 1);
        tok = strtok(nullptr, " ");
      }
    }
    precisaAtualizarDados = true;
  }

  // 4. Pistas recebidas (diretas ou roteadas pela Central)
  else {
    int idOrigem = 0;
    int pos = 0;
    if (sscanf(topic, "jogo/modulo/" MODULO_ID "/dica/%d%n", &idOrigem, &pos) == 1 && topic[pos] == '\0') {
      grupoOrigemDica = idOrigem;
      strncpy(conteudoDica, msg, sizeof(conteudoDica) - 1);
      conteudoDica[sizeof(conteudoDica) - 1] = '\0';
      temDicaNova = true;
      temDicaAtiva = true;
    }
  }
}

void reconectarMQTT() {
  while (!client.connected()) {
    if (client.connect("Modulo_" MODULO_ID,
                       "jogo/modulo/" MODULO_ID "/status",
                       0,
                       true,
                       "OFFLINE")) {
      client.publish("jogo/modulo/" MODULO_ID "/status", "ONLINE", true);

      client.subscribe("jogo/estado");
      client.subscribe("jogo/serial");
      client.subscribe("jogo/modulo/ativo");
      client.subscribe("jogo/tempo");
      client.subscribe("jogo/erros");
      client.subscribe("jogo/acertos");
      client.subscribe("jogo/total");
      client.subscribe("jogo/modulos");
      client.subscribe("jogo/modulo/" MODULO_ID "/dica/+");
    } else {
      delay(1000);
    }
  }
}

void registrarSucesso() {
  if (estadoJogo != RUNNING || !minhaVez || jaResolvido) return;
  jaResolvido = true;
  client.publish("jogo/modulo/" MODULO_ID "/evento", "SOLVED");
}

void registrarErro() {
  if (estadoJogo != RUNNING || !minhaVez || jaResolvido) return;
  client.publish("jogo/modulo/" MODULO_ID "/evento", "ERROR");
}

void enviarDicaDireta(int destino, const char* conteudo) {
  if (estadoJogo != RUNNING) return;
  char topico[36];
  snprintf(topico, sizeof(topico), "jogo/modulo/%d/dica/" MODULO_ID, destino);
  client.publish(topico, conteudo);
}

void enviarDicaAleatoria(const char* conteudo) {
  if (estadoJogo != RUNNING) return;
  client.publish("jogo/modulo/" MODULO_ID "/dica", conteudo);
}

void setup() {
  Serial.begin(115200);

  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);

  tft.init();
  tft.setRotation(1);
  drawLobby();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  client.setServer(MQTT_BROKER, MQTT_PORT);
  client.setCallback(callbackMqtt);
}

void loop() {
  if (!client.connected()) reconectarMQTT();
  client.loop();

  // Exibe a dica recebida na tela por 20 segundos
  if (temDicaNova) {
    temDicaNova = false;
    inicioDicaMs = millis();
    drawDica();
  }

  if (temDicaAtiva && (millis() - inicioDicaMs >= TEMPO_DICA_MS)) {
    temDicaAtiva = false;
    if (!minhaVez) {
      precisaDesenharTela = true;
    }
  }

  // Quando nao for a vez do modulo e nao houver dica ativa, mostra a tela compartilhada
  if (!minhaVez && !temDicaAtiva) {
    if (precisaDesenharTela) {
      precisaDesenharTela = false;
      precisaAtualizarDados = false;
      if (estadoJogo == LOBBY)        drawLobby();
      else if (estadoJogo == RUNNING) { drawRunning(); updateRunningData(); }
      else if (estadoJogo == VICTORY) drawFinish(true);
      else if (estadoJogo == DEFEAT)  drawFinish(false);
    } else if (precisaAtualizarDados) {
      precisaAtualizarDados = false;
      if (estadoJogo == LOBBY)        updateLobbyModules();
      else if (estadoJogo == RUNNING) updateRunningData();
    }
  }

  // Quando for a vez deste modulo, executa o desafio do grupo
  if (estadoJogo == RUNNING && minhaVez) {
    if (entrouNaMinhaVez) {
      entrouNaMinhaVez = false;
      // Limpe e desenhe a tela do seu desafio aqui!
    }

    // Exemplo:
    // if (jogadorAcertou()) registrarSucesso();
    // if (jogadorErrou())   registrarErro();
  }
}

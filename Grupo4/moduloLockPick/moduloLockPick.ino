#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <math.h>
#include <WiFi.h>
#include <PubSubClient.h>


#define MODULO_ID  "4"   
#define MODULO_NUM 4     
#define pinoBotao  35

const char* BROKER_MQTT = "192.168.2.100";
const int   BROKER_PORT = 1883;
const char* SSID        = "iot2022";
const char* PASSWORD    = "S3nhab0@";
WiFiClient espClient;
PubSubClient client(espClient);


#define TFT_MOSI 19
#define TFT_SCLK 18
#define TFT_CS    5
#define TFT_DC   16
#define TFT_RST  23
#define TFT_BL    4
#define ADC_EN   14

#define LED_VERDE    26
#define LED_VERMELHO 25
const int POTENCIOMETRO_GAZUA_PIN   = 32;
const int POTENCIOMETRO_DIREITO_PIN = 33;

int g_maxAdcGazua   = 3200;
int g_maxAdcDireito = 3200;
const int LOCK_W    = 70;
const int LOCK_H    = 70;
const int LOCK_CX   = 35;
const int LOCK_CY   = 35;
const int LOCK_R    = 28;
const int DISPLAY_X = 85;
const int DISPLAY_Y = 32;

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
GFXcanvas16 canvas(LOCK_W, LOCK_H);


enum EventoRodada {
  EVENTO_NENHUM,
  EVENTO_ERRO,
  EVENTO_VITORIA
};

bool g_modoOnlineMQTT = false;

// Estado local do desafio
int  anguloResposta         = 0;
int  g_anguloGazuaAnterior  = -1;
int  g_giroCilindroAnterior = -1;
bool g_emErroAnterior       = false;
bool g_jogoVencido          = false;
bool g_gameOver             = false;

int g_vidas = 3;
unsigned long g_tempoInicioErro    = 0;
unsigned long g_tempoAcumuladoErro = 0;

const int TEMPO_TOTAL_SEGUNDOS = 60;
int g_tempoRestante = TEMPO_TOTAL_SEGUNDOS;
unsigned long g_ultimoMillisTimer = 0;

char g_serialBomba[8]    = "-----";
bool g_jogoRodandoMQTT   = false;
bool g_minhaVezMQTT      = false;
bool g_jaResolvidoMQTT   = false;
bool g_atualizarTelaMQTT = true;

// Dicas recebidas de outros grupos
bool g_temDicaNova      = false;
int  g_grupoOrigemDica  = 0;
char g_conteudoDica[64] = "";
bool g_dicaJaEnviada = false;


int lerAnguloGazua(int pinPotenciometro, int &maxAdcRef) {
  long soma = 0;
  for (int i = 0; i < 16; i++) {
    soma += analogRead(pinPotenciometro);
    delayMicroseconds(50);
  }
  int rawValue = soma / 16;

  if (rawValue > maxAdcRef) maxAdcRef = rawValue;

  int tetoComMargem = maxAdcRef - 100;
  if (tetoComMargem < 1000) tetoComMargem = 1000;

  int angulo = map(rawValue, 50, tetoComMargem, 0, 180);
  return constrain(angulo, 0, 180);
}

void resetarEstadoFechadura(bool sortearNovoSegredo) {
  g_anguloGazuaAnterior  = -1;
  g_giroCilindroAnterior = -1;
  g_emErroAnterior       = false;
  g_tempoInicioErro      = 0;
  g_tempoAcumuladoErro   = 0;

  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, LOW);

  if (sortearNovoSegredo) {
    anguloResposta = random(15, 165);
  }
}

void desenharCoracao(int x, int y, uint16_t cor) {
  tft.fillRect(x + 1, y,     2, 1, cor);
  tft.fillRect(x + 5, y,     2, 1, cor);
  tft.fillRect(x,     y + 1, 4, 1, cor);
  tft.fillRect(x + 4, y + 1, 4, 1, cor);
  tft.fillRect(x,     y + 2, 8, 2, cor);
  tft.fillRect(x + 1, y + 4, 6, 1, cor);
  tft.fillRect(x + 2, y + 5, 4, 1, cor);
  tft.fillRect(x + 3, y + 6, 2, 1, cor);
}

void desenharVidasHUD() {
  tft.fillRect(5, 5, 65, 18, ST77XX_BLACK);
  if (!g_modoOnlineMQTT) {
    tft.setTextColor(ST77XX_RED);
    tft.setTextSize(2);
    tft.setCursor(5, 5);
    tft.print(g_vidas);
    desenharCoracao(22, 9, ST77XX_RED);
  } else {
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(5, 8);
    tft.print("SN:");
    tft.setTextColor(ST77XX_YELLOW);
    tft.print(g_serialBomba);
  }
}

void desenharTimerHUD() {
  tft.fillRect(175, 5, 60, 18, ST77XX_BLACK);
  if (g_tempoRestante <= 10) tft.setTextColor(ST77XX_RED);
  else if (g_tempoRestante <= 25) tft.setTextColor(ST77XX_YELLOW);
  else tft.setTextColor(ST77XX_WHITE);

  tft.setTextSize(2);
  tft.setCursor(175, 5);
  if (g_tempoRestante < 10) tft.print("0");
  tft.print(g_tempoRestante);
  tft.setTextSize(1);
  tft.print("s");
}

void exibirTelaMensagem(const char* titulo, const char* subtitulo, uint16_t corBorda, bool mostrarReiniciar) {
  tft.fillRect(15, 25, 210, 85, ST77XX_BLACK);
  tft.drawRect(15, 25, 210, 85, corBorda);
  tft.drawRect(17, 27, 206, 81, ST77XX_WHITE);

  tft.setTextColor(corBorda);
  tft.setTextSize(2);
  tft.setCursor(35, 40);
  tft.print(titulo);

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(35, 65);
  tft.print(subtitulo);

  if (mostrarReiniciar) {
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(30, 85);
    tft.print("Solte o giro p/ reiniciar");
  } else if (g_conteudoDica[0] != '\0') {
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(25, 85);
    tft.printf("Dica G%d: %s", g_grupoOrigemDica, g_conteudoDica);
  }
}

void atualizarRodapeHUD(bool emErro, int anguloGazua) {
  tft.fillRect(10, 118, 220, 15, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setCursor(10, 120);

  if (emErro) {
    tft.setTextColor(ST77XX_RED);
    tft.print("STATUS: TRAVADO! SOLTE O GIRO");
  } else if (g_modoOnlineMQTT && g_conteudoDica[0] != '\0') {
    tft.setTextColor(ST77XX_YELLOW);
    tft.printf("DICA G%d: %s", g_grupoOrigemDica, g_conteudoDica);
  } else {
    tft.setTextColor(ST77XX_CYAN);
    tft.print("POSICAO: ");
    tft.print(anguloGazua);
    tft.print(" DEG");
  }
}

void renderizarFechadura(int anguloGazua, int giroCilindro, bool emErro, uint16_t corGazua) {
  canvas.fillScreen(ST77XX_BLACK);

  canvas.drawCircle(LOCK_CX, LOCK_CY, LOCK_R + 2, ST77XX_WHITE);
  canvas.drawCircle(LOCK_CX, LOCK_CY, LOCK_R + 1, ST77XX_RED);
  canvas.drawCircle(LOCK_CX, LOCK_CY, LOCK_R, ST77XX_WHITE);
  canvas.fillCircle(LOCK_CX, LOCK_CY, LOCK_R - 2, 0x31A6);

  float radCilindro = giroCilindro * M_PI / 180.0;
  canvas.fillCircle(LOCK_CX, LOCK_CY, LOCK_R - 4, 0x4208);

  int x1 = LOCK_CX + cos(radCilindro + M_PI_2) * 15;
  int y1 = LOCK_CY + sin(radCilindro + M_PI_2) * 15;
  int x2 = LOCK_CX - cos(radCilindro + M_PI_2) * 15;
  int y2 = LOCK_CY - sin(radCilindro + M_PI_2) * 15;

  canvas.drawLine(x1, y1, x2, y2, ST77XX_BLACK);
  canvas.fillCircle(x1, y1, 2, ST77XX_BLACK);
  canvas.fillCircle(x2, y2, 2, ST77XX_BLACK);
  canvas.fillCircle(LOCK_CX, LOCK_CY, 5, ST77XX_BLACK);

  int jitter = emErro ? random(-3, 4) : 0;
  float radGazua = (anguloGazua + jitter) * M_PI / 180.0;
  int xEnd = LOCK_CX - cos(radGazua) * (LOCK_R + 6);
  int yEnd = LOCK_CY - sin(radGazua) * (LOCK_R + 6);

  canvas.drawLine(LOCK_CX, LOCK_CY, xEnd, yEnd, corGazua);
  canvas.fillCircle(xEnd, yEnd, 2, corGazua);

  tft.drawRGBBitmap(DISPLAY_X, DISPLAY_Y, canvas.getBuffer(), LOCK_W, LOCK_H);
}

void setupDisplay() {
  pinMode(ADC_EN, OUTPUT);
  digitalWrite(ADC_EN, HIGH);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  delay(50);

  tft.init(135, 240);
  SPI.setFrequency(80000000);

  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  desenharVidasHUD();
  desenharTimerHUD();

  tft.setTextSize(1);
  tft.setCursor(92, 8);
  tft.setTextColor(g_modoOnlineMQTT ? ST77XX_GREEN : ST77XX_ORANGE);
  tft.print(g_modoOnlineMQTT ? "[M" MODULO_ID "]" : "[LOCAL]");

  if (!g_modoOnlineMQTT) {
    renderizarFechadura(90, 0, false, ST77XX_YELLOW);
    atualizarRodapeHUD(false, 90);
  }
}


EventoRodada processarMecanicaLockpicking(int potenciaDireita) {
  bool forcandoFechadura = (potenciaDireita >= 30);
  int anguloLido = lerAnguloGazua(POTENCIOMETRO_GAZUA_PIN, g_maxAdcGazua);

  bool anguloCerto = (anguloLido >= anguloResposta - 10 && anguloLido <= anguloResposta + 10);
  bool emErro = forcandoFechadura && !anguloCerto;

  int anguloGazua = emErro ? (g_anguloGazuaAnterior >= 0 ? g_anguloGazuaAnterior : anguloLido) : anguloLido;

  int giroCilindro = 0;
  if (anguloCerto && forcandoFechadura) {
    giroCilindro = constrain(map(potenciaDireita, 30, 180, 0, 90), 0, 90);
  } else if (emErro) {
    giroCilindro = random(-2, 3);
  }

  // Contagem de tempo acumulado em erro (1000 ms)
  EventoRodada evento = EVENTO_NENHUM;
  if (emErro) {
    if (g_tempoInicioErro == 0) {
      g_tempoInicioErro = millis();
    } else {
      g_tempoAcumuladoErro += millis() - g_tempoInicioErro;
      g_tempoInicioErro = millis();

      if (g_tempoAcumuladoErro >= 1000) {
        g_tempoAcumuladoErro = 0;
        evento = EVENTO_ERRO;
      }
    }
  } else {
    g_tempoInicioErro = 0;
  }

  // Atuadores (LEDs) e detecção de abertura
  if (emErro) {
    digitalWrite(LED_VERMELHO, HIGH);
    digitalWrite(LED_VERDE, LOW);
  } else if (anguloCerto && potenciaDireita > 150) {
    digitalWrite(LED_VERMELHO, LOW);
    digitalWrite(LED_VERDE, HIGH);
    return EVENTO_VITORIA;
  } else {
    digitalWrite(LED_VERMELHO, LOW);
    digitalWrite(LED_VERDE, LOW);
  }

  bool saiuDoErro = (g_emErroAnterior && !emErro);
  if (saiuDoErro) g_anguloGazuaAnterior = -1;

  // Renderização visual
  if (anguloGazua != g_anguloGazuaAnterior || giroCilindro != g_giroCilindroAnterior || emErro || saiuDoErro) {
    uint16_t corGazua = emErro ? ST77XX_RED : ST77XX_YELLOW;
    renderizarFechadura(anguloGazua, giroCilindro, emErro, corGazua);
    atualizarRodapeHUD(emErro, anguloGazua);

    g_anguloGazuaAnterior  = anguloGazua;
    g_giroCilindroAnterior = giroCilindro;
    g_emErroAnterior       = emErro;
  }

  return evento;
}

void callbackMqtt(char* topic, byte* payload, unsigned int length) {
  char msg[length + 1];
  memcpy(msg, payload, length);
  msg[length] = '\0';

  while (length > 0 && (msg[length - 1] == '\r' || msg[length - 1] == '\n' || msg[length - 1] == ' ')) {
    msg[--length] = '\0';
  }

  if (strcmp(topic, "jogo/estado") == 0) {
    if (strcmp(msg, "START") == 0) {
      g_jogoRodandoMQTT = true;
      g_jaResolvidoMQTT = false;
      g_temDicaNova     = false;
      g_dicaJaEnviada   = false;
      g_conteudoDica[0] = '\0';
      resetarEstadoFechadura(true);
      g_atualizarTelaMQTT = true;
    }
    else if (strcmp(msg, "RESET") == 0) {
      g_jogoRodandoMQTT = false;
      g_minhaVezMQTT    = false;
      g_jaResolvidoMQTT = false;
      g_temDicaNova     = false;
      g_dicaJaEnviada   = false;
      g_conteudoDica[0] = '\0';
      resetarEstadoFechadura(false);
      g_atualizarTelaMQTT = true;
    }
    else if (strcmp(msg, "VICTORY") == 0) {
      g_jogoRodandoMQTT = false;
      g_minhaVezMQTT    = false;
      digitalWrite(LED_VERDE, HIGH);
      digitalWrite(LED_VERMELHO, LOW);
      exibirTelaMensagem("LOCK OPENED!", "BOMBA DESARMADA!", ST77XX_GREEN, false);
    }
    else if (strcmp(msg, "DEFEAT") == 0) {
      g_jogoRodandoMQTT = false;
      g_minhaVezMQTT    = false;
      digitalWrite(LED_VERDE, LOW);
      digitalWrite(LED_VERMELHO, HIGH);
      exibirTelaMensagem("GAME OVER", "BOMBA DETONADA!", ST77XX_RED, false);
    }
  }
  else if (strcmp(topic, "jogo/modulo/ativo") == 0) {
    bool eraMinhaVez = g_minhaVezMQTT;
    g_minhaVezMQTT = (atoi(msg) == MODULO_NUM);
    if (g_minhaVezMQTT != eraMinhaVez) {
      resetarEstadoFechadura(false);
      g_atualizarTelaMQTT = true;
    }
  }
  else if (strcmp(topic, "jogo/serial") == 0) {
    strncpy(g_serialBomba, msg, sizeof(g_serialBomba) - 1);
    g_serialBomba[sizeof(g_serialBomba) - 1] = '\0';
    desenharVidasHUD();
  }
  else if (strcmp(topic, "jogo/tempo") == 0) {
    g_tempoRestante = atoi(msg);
    desenharTimerHUD();
  }
  else {
    int idOrigem = 0, pos = 0;
    if (sscanf(topic, "jogo/modulo/" MODULO_ID "/dica/%d%n", &idOrigem, &pos) == 1 && topic[pos] == '\0') {
      g_grupoOrigemDica = idOrigem;
      strncpy(g_conteudoDica, msg, sizeof(g_conteudoDica) - 1);
      g_conteudoDica[sizeof(g_conteudoDica) - 1] = '\0';
      g_temDicaNova = true;
    }
  }
}

bool inicializarConexaoMQTT() {
  if (WiFi.status() != WL_CONNECTED) return false;

  client.setServer(BROKER_MQTT, BROKER_PORT);
  client.setCallback(callbackMqtt);

  int contador = 0;
  while (!client.connected() && contador < 3) {
    if (client.connect("Modulo_" MODULO_ID, "jogo/modulo/" MODULO_ID "/status", 0, true, "OFFLINE")) {
      client.publish("jogo/modulo/" MODULO_ID "/status", "ONLINE", true);
      client.subscribe("jogo/estado");
      client.subscribe("jogo/serial");
      client.subscribe("jogo/modulo/ativo");
      client.subscribe("jogo/modulo/" MODULO_ID "/dica/+");
      client.subscribe("jogo/tempo");
      return true;
    }
    contador++;
    if (contador < 3) delay(1500);
  }
  return false;
}


void executarModoMQTT() {
  if (!client.connected() || WiFi.status() != WL_CONNECTED) {
      g_modoOnlineMQTT = false;
      setupDisplay();
      return;
  }
  client.loop();

  if (g_temDicaNova) {
    g_temDicaNova = false;
    if (!g_jogoRodandoMQTT || !g_minhaVezMQTT || g_jaResolvidoMQTT) {
      g_atualizarTelaMQTT = true;
    } else {
      atualizarRodapeHUD(false, g_anguloGazuaAnterior);
    }
  }

  if(g_jogoRodandoMQTT && !g_dicaJaEnviada && digitalRead(pinoBotao) == LOW && g_minhaVezMQTT){
    g_dicaJaEnviada = true;

    int limiteInferior = (anguloResposta - 40 < 0) ? 0: anguloResposta - 40;
    int limiteSuperior = (anguloResposta + 40 > 180) ? 180: anguloResposta + 40;

    char mensagem[64];
    snprintf(mensagem, sizeof(mensagem), "Angulo da Resposta do modulo " MODULO_ID ": %d a %d", limiteInferior, limiteSuperior);

    client.publish("jogo/modulo/" MODULO_ID "/dica", mensagem);
  }

  // Bloqueia interação fora do turno
  if (!g_jogoRodandoMQTT || !g_minhaVezMQTT || g_jaResolvidoMQTT) {
    if (g_atualizarTelaMQTT) {
      g_atualizarTelaMQTT = false;
      tft.fillRect(0, 24, 240, 111, ST77XX_BLACK);

      if (g_jaResolvidoMQTT) {
        exibirTelaMensagem("LOCK OPENED!", "AGUARDANDO EQUIPE...", ST77XX_GREEN, false);
      } else if (!g_jogoRodandoMQTT) {
        exibirTelaMensagem("AGUARDANDO", "Aguardando START da Central", ST77XX_CYAN, false);
      } else {
        exibirTelaMensagem("BLOQUEADO", "Aguarde o turno do Modulo " MODULO_ID, ST77XX_ORANGE, false);
      }
    }
    return;
  }

  if (g_atualizarTelaMQTT) {
    g_atualizarTelaMQTT = false;
    tft.fillRect(0, 24, 240, 111, ST77XX_BLACK);
    resetarEstadoFechadura(false);
  }

  int potenciaDireita = lerAnguloGazua(POTENCIOMETRO_DIREITO_PIN, g_maxAdcDireito);
  EventoRodada evento = processarMecanicaLockpicking(potenciaDireita);

  if (evento == EVENTO_ERRO) {
    client.publish("jogo/modulo/" MODULO_ID "/evento", "ERROR");
  } else if (evento == EVENTO_VITORIA) {
    g_jaResolvidoMQTT   = true;
    g_atualizarTelaMQTT = true;
    client.publish("jogo/modulo/" MODULO_ID "/evento", "SOLVED");
  }
}


void executarModoStandalone() {
  int potenciaDireita = lerAnguloGazua(POTENCIOMETRO_DIREITO_PIN, g_maxAdcDireito);

  // Reinício ao soltar o potenciômetro após vitória ou derrota
  if (g_jogoVencido || g_gameOver) {
    if (potenciaDireita < 30) {
      g_jogoVencido       = false;
      g_gameOver          = false;
      g_vidas             = 3;
      g_tempoRestante     = TEMPO_TOTAL_SEGUNDOS;
      g_ultimoMillisTimer = millis();

      resetarEstadoFechadura(true);
      setupDisplay();
    }
    return;
  }

  // Cronômetro regressivo local
  if (millis() - g_ultimoMillisTimer >= 1000) {
    g_ultimoMillisTimer = millis();
    if (g_tempoRestante > 0) {
      g_tempoRestante--;
      desenharTimerHUD();
    }
    if (g_tempoRestante == 0) {
      g_gameOver = true;
      digitalWrite(LED_VERMELHO, HIGH);
      exibirTelaMensagem("GAME OVER", "TEMPO ESGOTADO!", ST77XX_RED, true);
      return;
    }
  }

  // Executa o núcleo compartilhado do minigame
  EventoRodada evento = processarMecanicaLockpicking(potenciaDireita);

  if (evento == EVENTO_ERRO) {
    g_vidas--;
    desenharVidasHUD();
    if (g_vidas <= 0) {
      g_gameOver = true;
      digitalWrite(LED_VERMELHO, HIGH);
      exibirTelaMensagem("GAME OVER", "GAZUA QUEBRADA!", ST77XX_RED, true);
    }
  } else if (evento == EVENTO_VITORIA) {
    delay(200);
    g_jogoVencido = true;
    exibirTelaMensagem("LOCK OPENED!", "FECHADURA DESARMADA", ST77XX_GREEN, true);
  }
}


void setup() {
  Serial.begin(115200);

  analogReadResolution(12);
  analogSetPinAttenuation(POTENCIOMETRO_GAZUA_PIN, ADC_11db);
  analogSetPinAttenuation(POTENCIOMETRO_DIREITO_PIN, ADC_11db);

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(POTENCIOMETRO_GAZUA_PIN, INPUT);
  pinMode(POTENCIOMETRO_DIREITO_PIN, INPUT);
  pinMode(pinoBotao, INPUT);


  randomSeed(analogRead(34));
  resetarEstadoFechadura(true);
  g_ultimoMillisTimer = millis();

  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(100);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_15dBm);
  WiFi.begin(SSID, PASSWORD);


  unsigned long inicioWifi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicioWifi < 8000) {
    delay(250);
  }

  g_modoOnlineMQTT = inicializarConexaoMQTT();



  setupDisplay();
}

void loop() {
  if (g_modoOnlineMQTT) {
    executarModoMQTT();
  } else {
    executarModoStandalone();
  }

  delay(5);
}
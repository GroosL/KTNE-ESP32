/*
  Modulo Labirinto (Grupo 6) - jogo cooperativo estilo Keep Talking and Nobody Explodes

  Plataforma: ESP32 TTGO T-Display (TFT 240x135) com acelerometro MPU6050.
  Dependencias: WiFi, PubSubClient (MQTT), TFT_eSPI, Adafruit_MPU6050, Adafruit_Sensor.

  Funcionamento:
  - Gera um labirinto aleatorio 8x7 com 3 bombas invisiveis e exatamente um caminho seguro.
  - O jogador inclina a placa para levar a bola de A1 ate a celula verde (G8).
  - Encostar na parede volta ao inicio; pisar em bomba envia ERROR (strike) para a central.
  - Chegar ao objetivo acende o LED de status e envia SOLVED.
  - Comunica com a central via MQTT (protocolo RFC 001): presenca, estado da partida,
    modulo ativo e envio das posicoes das bombas como dicas para outros modulos.
*/

// bibliotecas de rede, display, sensor e matematica
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <math.h>
#include <string.h>

// instancias do display e do sensor de movimento
TFT_eSPI tela = TFT_eSPI();
Adafruit_MPU6050 mpu;

// dimensoes da tela e pino do backlight (GPIO4 na TTGO T-Display)
const int W = 240;
const int H = 135;
const int LED_PIN = 4;

// LED verde de status, aceso quando o modulo e resolvido
const int LED_VITORIA_PIN = 13;

// estado local do modulo
bool moduloResolvido = false;
int strikes = 0;

// modo de teste: ignora a central e permite gerar labirinto novo pelo botao GPIO35
const bool MODO_TESTE = false;
const int BTN_NOVO_LABIRINTO = 35;

// identificacao do grupo usada nos topicos MQTT
#define MODULO_ID  "6"
#define MODULO_NUM 6

// credenciais da rede e endereco do broker
const char* WIFI_SSID     = "iot2022";
const char* WIFI_PASSWORD = "S3nhab0@";
const char* MQTT_BROKER   = "192.168.2.100";
const int   MQTT_PORT     = 1883;

// cliente MQTT sobre a conexao Wi-Fi
WiFiClient espClient;
PubSubClient client(espClient);

// estado da partida recebido da central (resultadoFinal: 0 nenhum, 1 vitoria, 2 derrota)
bool jogoRodando = false;
bool minhaVez    = false;
int  resultadoFinal = 0;
char serialBomba[8] = "-----";

// ultima dica recebida de outro grupo
bool temDicaNova = false;
int  grupoOrigemDica = 0;
char conteudoDica[64] = "";

// controle para enviar as dicas das bombas apenas uma vez por partida
bool dicasEnviadas = false;

// sinalizacoes geradas no callback e tratadas no loop
bool novaPartida = false;
bool precisaRedesenhar = true;
unsigned long ultimaTentativaMqtt = 0;

// exibe as bombas na tela para depuracao
const bool DEBUG_BOMBAS = false;

// grade do labirinto: 8 colunas (1-8) e 7 linhas (A-G)
const int COLUNAS = 8;
const int LINHAS = 7;

// limites em pixels da area do labirinto
const int ESQ = 13;
const int DIR = 239;
const int TOPO = 9;
const int BASE = 134;

// tamanho de cada celula em pixels
const float CEL_W = (float)(DIR - ESQ) / COLUNAS;
const float CEL_H = (float)(BASE - TOPO) / LINHAS;

// raio da bola em pixels
const int R = 3;

// celula objetivo fixa no canto inferior direito
const int OBJ_COL = COLUNAS - 1;
const int OBJ_LINHA = LINHAS - 1;

// paleta de cores
const uint16_t COR_FUNDO  = TFT_BLACK;
const uint16_t COR_GRADE  = 0x1082;
const uint16_t COR_PAREDE = 0xBDF7;
const uint16_t COR_BOLA   = TFT_CYAN;
const uint16_t COR_LABEL  = TFT_RED;

// estado fisico da bola e filtros do acelerometro
float bolaX = 0, bolaY = 0;
float velX = 0, velY = 0;
float refX = 0, refY = 0;
float filtroX = 0, filtroY = 0;

// parametros de controle: a inclinacao define a velocidade alvo da bola
const float GANHO = 25.0;
const float RESPOSTA = 8.0;
const float ZONA_MORTA = 0.3;
const float VELOCIDADE_MAX = 90.0;
const float SINAL_X = -1.0;
const float SINAL_Y = -1.0;

// encostar na parede faz a bola voltar ao inicio
const bool PAREDE_REINICIA = true;

// velocidade da assistencia de curva em px/s
const float ASSISTENCIA = 60.0;

// inclinacao atual, usada pela assistencia de curva
float intencaoX = 0, intencaoY = 0;

// controle de tempo, calibracao e ultima posicao desenhada da bola
unsigned long tempoAnterior = 0;
unsigned long inicioCalibracao = 0;
bool calibrado = false;
float somaCalX = 0, somaCalY = 0;
int amostrasCal = 0;
int ultimaX = -1, ultimaY = -1;

// posicao inicial da bola: centro da celula A1
const float INICIO_X = ESQ + CEL_W * 0.5f;
const float INICIO_Y = TOPO + CEL_H * 0.5f;

// segmento de parede em coordenadas da grade
struct Parede {
  int x1, y1, x2, y2;
};

// limite de segmentos (labirinto perfeito 8x7 tem no maximo 42 paredes)
const int MAX_PAREDES = 60;

Parede paredes[MAX_PAREDES];
int numParedes = 0;

// mapa de paredes: hOcupada entre linhas, vOcupada entre colunas
bool hOcupada[LINHAS + 1][COLUNAS] = {};
bool vOcupada[LINHAS][COLUNAS + 1] = {};

// publica SOLVED/ERROR; no modo de teste usa um topico ignorado pela central
void publicarEvento(const char* evento) {
  if (MODO_TESTE) client.publish("jogo/modulo/" MODULO_ID "/teste", evento);
  else            client.publish("jogo/modulo/" MODULO_ID "/evento", evento);
}

// posicao de uma bomba invisivel
struct Bomba {
  int coluna, linha;
};

// lista de bombas e matriz de consulta rapida por celula
const int NUM_BOMBAS = 3;
Bomba bombas[NUM_BOMBAS];
bool matrizBombas[LINHAS][COLUNAS] = {false};

// conversao de coordenada da grade para pixel
float px(int x) { return ESQ + x * CEL_W; }
float py(int y) { return TOPO + y * CEL_H; }

// reconstroi a matriz de bombas a partir da lista
void atualizarMatrizBombas() {
  memset(matrizBombas, 0, sizeof(matrizBombas));
  for (int i = 0; i < NUM_BOMBAS; i++) {
    if (bombas[i].coluna != -1) {
      matrizBombas[bombas[i].linha][bombas[i].coluna] = true;
    }
  }
}

// consulta se a celula possui bomba
bool celulaTemBomba(int c, int l) {
  return matrizBombas[l][c];
}

// deslocamentos das 4 direcoes (cima, baixo, esquerda, direita)
const int DX[4] = {0, 0, -1, 1};
const int DY[4] = {-1, 1, 0, 0};
const int NUM_CELULAS = LINHAS * COLUNAS;

// verifica se nao ha parede entre duas celulas vizinhas
bool passagemLivre(int cx, int cy, int nx, int ny) {
  if (nx > cx) return !vOcupada[cy][nx];
  if (nx < cx) return !vOcupada[cy][cx];
  if (ny > cy) return !hOcupada[ny][cx];
  return !hOcupada[cy][cx];
}

// coloca ou remove a parede entre duas celulas vizinhas
void definirParede(int ax, int ay, int bx, int by, bool valor) {
  if (ax != bx) vOcupada[ay][max(ax, bx)] = valor;
  else          hOcupada[max(ay, by)][ax] = valor;
}

// busca em largura (BFS) entre duas celulas, com opcao de evitar bombas e proibir uma passagem
bool buscarCaminho(int oX, int oY, int dX, int dY, bool evitarBombas,
                   int eAx, int eAy, int eBx, int eBy,
                   int outX[], int outY[], int *outLen) {
  // estruturas da busca: visitados, pais para reconstruir o caminho e fila
  bool visitado[LINHAS][COLUNAS] = {};
  int8_t paiX[LINHAS][COLUNAS], paiY[LINHAS][COLUNAS];
  int8_t filaX[NUM_CELULAS], filaY[NUM_CELULAS];
  int ini = 0, fim = 0;

  filaX[fim] = oX; filaY[fim] = oY; fim++;
  visitado[oY][oX] = true;
  bool achou = false;

  // expande a fila ate encontrar o destino
  while (ini < fim) {
    int cx = filaX[ini], cy = filaY[ini]; ini++;
    if (cx == dX && cy == dY) { achou = true; break; }

    // avalia os 4 vizinhos e enfileira os que sao acessiveis
    for (int i = 0; i < 4; i++) {
      int nx = cx + DX[i], ny = cy + DY[i];
      if (nx < 0 || nx >= COLUNAS || ny < 0 || ny >= LINHAS) continue;
      if (visitado[ny][nx]) continue;
      if (!passagemLivre(cx, cy, nx, ny)) continue;
      if ((cx == eAx && cy == eAy && nx == eBx && ny == eBy) ||
          (cx == eBx && cy == eBy && nx == eAx && ny == eAy)) continue;
      if (evitarBombas && matrizBombas[ny][nx]) continue;

      visitado[ny][nx] = true;
      paiX[ny][nx] = cx; paiY[ny][nx] = cy;
      filaX[fim] = nx; filaY[fim] = ny; fim++;
    }
  }

  // reconstroi o caminho do destino ate a origem usando os pais
  if (achou && outX != nullptr) {
    int len = 0, cx = dX, cy = dY;
    while (true) {
      outX[len] = cx; outY[len] = cy; len++;
      if (cx == oX && cy == oY) break;
      int pxa = paiX[cy][cx];
      cy = paiY[cy][cx]; cx = pxa;
    }
    // inverte o vetor para ficar na ordem origem -> destino
    for (int i = 0; i < len / 2; i++) {
      int tx = outX[i]; outX[i] = outX[len - 1 - i]; outX[len - 1 - i] = tx;
      int ty = outY[i]; outY[i] = outY[len - 1 - i]; outY[len - 1 - i] = ty;
    }
    *outLen = len;
  }
  return achou;
}

// gera um labirinto perfeito (um unico caminho entre quaisquer celulas) por DFS iterativo
void gerarArvore() {
  // comeca com todas as paredes internas fechadas
  memset(hOcupada, 0, sizeof(hOcupada));
  memset(vOcupada, 0, sizeof(vOcupada));
  for (int y = 1; y < LINHAS; y++)
    for (int x = 0; x < COLUNAS; x++) hOcupada[y][x] = true;
  for (int y = 0; y < LINHAS; y++)
    for (int x = 1; x < COLUNAS; x++) vOcupada[y][x] = true;

  // pilha da DFS com as celulas em visita
  bool visitado[LINHAS][COLUNAS] = {};
  int8_t pilhaX[NUM_CELULAS], pilhaY[NUM_CELULAS];
  int sp = 0;

  visitado[0][0] = true;
  pilhaX[sp] = 0; pilhaY[sp] = 0; sp++;

  // mantem aberta a passagem logo abaixo da celula inicial
  definirParede(0, 0, 0, 1, false);
  visitado[1][0] = true;
  pilhaX[sp] = 0; pilhaY[sp] = 1; sp++;

  // avanca para um vizinho nao visitado aleatorio ou retrocede quando nao ha opcoes
  while (sp > 0) {
    int cx = pilhaX[sp - 1], cy = pilhaY[sp - 1];
    int opcoes[4], n = 0;
    for (int i = 0; i < 4; i++) {
      int nx = cx + DX[i], ny = cy + DY[i];
      if (nx >= 0 && nx < COLUNAS && ny >= 0 && ny < LINHAS && !visitado[ny][nx]) opcoes[n++] = i;
    }
    if (n == 0) { sp--; continue; }

    int d = opcoes[random(0, n)];
    int nx = cx + DX[d], ny = cy + DY[d];
    definirParede(cx, cy, nx, ny, false);
    visitado[ny][nx] = true;
    pilhaX[sp] = nx; pilhaY[sp] = ny; sp++;
  }
}

// abre desvios alternativos ate o objetivo e coloca uma bomba em cada um
bool criarDesviosComBombas() {
  // rota unica atual do inicio ate o objetivo
  int rotaX[NUM_CELULAS], rotaY[NUM_CELULAS], rotaLen = 0;
  buscarCaminho(0, 0, OBJ_COL, OBJ_LINHA, false, -1, -1, -1, -1, rotaX, rotaY, &rotaLen);

  bool naRota[LINHAS][COLUNAS] = {};
  for (int i = 0; i < rotaLen; i++) naRota[rotaY[i]][rotaX[i]] = true;

  // lista todas as paredes internas como pares de celulas candidatas
  int8_t cAx[100], cAy[100], cBx[100], cBy[100];
  int nc = 0;
  for (int y = 1; y < LINHAS; y++)
    for (int x = 0; x < COLUNAS; x++)
      if (hOcupada[y][x]) { cAx[nc] = x; cAy[nc] = y - 1; cBx[nc] = x; cBy[nc] = y; nc++; }
  for (int y = 0; y < LINHAS; y++)
    for (int x = 1; x < COLUNAS; x++)
      if (vOcupada[y][x]) { cAx[nc] = x - 1; cAy[nc] = y; cBx[nc] = x; cBy[nc] = y; nc++; }

  // embaralha as candidatas (Fisher-Yates) para variar o resultado
  for (int i = 0; i < nc; i++) {
    int r = random(i, nc);
    int8_t t;
    t = cAx[i]; cAx[i] = cAx[r]; cAx[r] = t;
    t = cAy[i]; cAy[i] = cAy[r]; cAy[r] = t;
    t = cBx[i]; cBx[i] = cBx[r]; cBx[r] = t;
    t = cBy[i]; cBy[i] = cBy[r]; cBy[r] = t;
  }

  // celulas fora da rota ja usadas por outro desvio
  bool usada[LINHAS][COLUNAS] = {};
  int escolhidas[NUM_BOMBAS];
  int nb = 0;

  // testa cada parede candidata ate posicionar todas as bombas
  for (int k = 0; k < nc && nb < NUM_BOMBAS; k++) {
    // caminho na arvore entre as duas celulas: forma o ciclo criado ao abrir a parede
    int cicX[NUM_CELULAS], cicY[NUM_CELULAS], cicLen = 0;
    buscarCaminho(cAx[k], cAy[k], cBx[k], cBy[k], false, -1, -1, -1, -1, cicX, cicY, &cicLen);

    int celulasNaRota = 0, fora[NUM_CELULAS], nf = 0;
    bool conflito = false;
    // separa celulas do ciclo que estao na rota e as que ficam fora dela
    for (int i = 0; i < cicLen; i++) {
      if (naRota[cicY[i]][cicX[i]]) celulasNaRota++;
      else {
        if (usada[cicY[i]][cicX[i]]) { conflito = true; break; }
        fora[nf++] = i;
      }
    }
    // exige trecho em comum com a rota, celula livre para a bomba e nenhuma sobreposicao com outro desvio
    if (conflito || celulasNaRota < 2 || nf == 0) continue;

    // reserva o desvio e sorteia a celula da bomba fora da rota
    for (int i = 0; i < nf; i++) usada[cicY[fora[i]]][cicX[fora[i]]] = true;
    int e = fora[random(0, nf)];
    bombas[nb] = {cicX[e], cicY[e]};
    escolhidas[nb] = k;
    nb++;
  }

  if (nb < NUM_BOMBAS) return false;

  // abre as paredes escolhidas, criando os caminhos alternativos
  for (int i = 0; i < NUM_BOMBAS; i++) {
    int k = escolhidas[i];
    definirParede(cAx[k], cAy[k], cBx[k], cBy[k], false);
  }
  atualizarMatrizBombas();
  return true;
}

// confirma que existe exatamente um caminho sem bombas ate o objetivo
bool validarMapaRigoroso() {
  int rotaX[NUM_CELULAS], rotaY[NUM_CELULAS], rotaLen = 0;
  if (!buscarCaminho(0, 0, OBJ_COL, OBJ_LINHA, true, -1, -1, -1, -1, rotaX, rotaY, &rotaLen)) return false;

  // se bloquear qualquer passo da rota ainda permitir chegar, existe um segundo caminho seguro
  for (int i = 0; i < rotaLen - 1; i++) {
    if (buscarCaminho(0, 0, OBJ_COL, OBJ_LINHA, true,
                      rotaX[i], rotaY[i], rotaX[i + 1], rotaY[i + 1],
                      nullptr, nullptr, nullptr)) return false;
  }
  return true;
}

// converte o mapa de paredes em segmentos, unindo paredes alinhadas consecutivas
void montarListaParedes() {
  numParedes = 0;
  // segmentos horizontais
  for (int y = 1; y < LINHAS; y++) {
    int x = 0;
    while (x < COLUNAS) {
      if (!hOcupada[y][x]) { x++; continue; }
      int ini = x;
      while (x < COLUNAS && hOcupada[y][x]) x++;
      if (numParedes < MAX_PAREDES) paredes[numParedes++] = {ini, y, x, y};
    }
  }
  // segmentos verticais
  for (int x = 1; x < COLUNAS; x++) {
    int y = 0;
    while (y < LINHAS) {
      if (!vOcupada[y][x]) { y++; continue; }
      int ini = y;
      while (y < LINHAS && vOcupada[y][x]) y++;
      if (numParedes < MAX_PAREDES) paredes[numParedes++] = {x, ini, x, y};
    }
  }
}

// gera labirinto e bombas ate obter um mapa valido
void gerarMapaValido() {
  unsigned long t0 = micros();
  int tentativas = 0;

  while (true) {
    tentativas++;
    gerarArvore();
    if (criarDesviosComBombas() && validarMapaRigoroso()) break;
    if (tentativas % 20 == 0) yield();
  }

  montarListaParedes();
  Serial.printf("Mapa gerado em %lu us (%d tentativa(s))\n", micros() - t0, tentativas);
}

// pinta a celula objetivo de verde
void desenharObjetivo() {
  int x = roundf(px(OBJ_COL));
  int y = roundf(py(OBJ_LINHA));
  tela.fillRect(x + 1, y + 1, CEL_W - 1, CEL_H - 1, TFT_GREEN);
}

// desenha as bombas apenas no modo de depuracao
void desenharBombasVisualmente() {
  if (!DEBUG_BOMBAS) return;
  for (int i = 0; i < NUM_BOMBAS; i++) {
    int cx = roundf(px(bombas[i].coluna) + CEL_W * 0.5f);
    int cy = roundf(py(bombas[i].linha) + CEL_H * 0.5f);
    tela.fillCircle(cx, cy, R + 2, TFT_RED);
  }
}

// desenha as linhas da grade e a borda
void desenharGrade() {
  for (int x = 1; x < COLUNAS; x++) tela.drawLine(roundf(px(x)), TOPO, roundf(px(x)), BASE, COR_GRADE);
  for (int y = 1; y < LINHAS; y++) tela.drawLine(ESQ, roundf(py(y)), DIR, roundf(py(y)), COR_GRADE);
  tela.drawRect(ESQ, TOPO, DIR - ESQ + 1, BASE - TOPO + 1, COR_GRADE);
}

// desenha os numeros das colunas e as letras das linhas
void desenharLabels() {
  tela.setTextColor(COR_LABEL, COR_FUNDO);
  tela.setTextDatum(MC_DATUM);
  tela.setTextSize(1);
  for (int c = 0; c < COLUNAS; c++) {
    char texto[3]; snprintf(texto, sizeof(texto), "%d", c + 1);
    tela.drawString(texto, roundf(ESQ + (c + 0.5f) * CEL_W), 4);
  }
  for (int l = 0; l < LINHAS; l++) {
    char texto[2] = {(char)('A' + l), '\0'};
    tela.drawString(texto, 5, roundf(TOPO + (l + 0.5f) * CEL_H));
  }
}

// desenha cada parede com 2 px de espessura
void desenharParedes() {
  for (int i = 0; i < numParedes; i++) {
    Parede& p = paredes[i];
    int x1 = roundf(px(p.x1)), y1 = roundf(py(p.y1));
    int x2 = roundf(px(p.x2)), y2 = roundf(py(p.y2));
    tela.drawLine(x1, y1, x2, y2, COR_PAREDE);
    if (p.x1 == p.x2) tela.drawLine(x1 + 1, y1, x2 + 1, y2, COR_PAREDE);
    else tela.drawLine(x1, y1 + 1, x2, y2 + 1, COR_PAREDE);
  }
}

// desenha os elementos fixos do mapa
void desenharEstaticos() {
  desenharGrade();
  desenharObjetivo();
  desenharBombasVisualmente();
  desenharParedes();
}

// redesenha a tela inteira do labirinto
void desenharMapa() {
  tela.fillScreen(COR_FUNDO);
  desenharEstaticos();
  desenharLabels();
}

// desenha a bola e guarda a posicao para apagar depois
void desenharBola() {
  int x = roundf(bolaX), y = roundf(bolaY);
  tela.fillCircle(x, y, R, COR_BOLA);
  tela.drawCircle(x, y, R, TFT_WHITE);
  ultimaX = x; ultimaY = y;
}

// apaga apenas a bola antiga e redesenha por cima, evitando limpar a tela inteira
void atualizarBola() {
  if (ultimaX >= 0) tela.fillCircle(ultimaX, ultimaY, R, COR_FUNDO);
  desenharEstaticos();
  desenharBola();
}

// volta a bola para o inicio e redesenha o mapa
void reiniciarBola() {
  bolaX = INICIO_X; bolaY = INICIO_Y;
  velX = 0; velY = 0;
  desenharMapa();
  desenharBola();
}

// pisca a tela em vermelho por 500 ms ao pisar em bomba
void animarExplosao() {
  unsigned long inicio = millis();
  while (millis() - inicio < 500) {
    tela.fillScreen(TFT_RED); delay(100);
    tela.fillScreen(TFT_BLACK); delay(100);
  }
}

// marca o modulo como resolvido, acende o LED e avisa a central
void resolverModulo() {
  moduloResolvido = true;
  velX = 0; velY = 0;
  digitalWrite(LED_VITORIA_PIN, HIGH);
  atualizarBola();
  publicarEvento("SOLVED");
  Serial.printf("MODULO RESOLVIDO (strikes: %d) -> SOLVED enviado\n", strikes);
}

// colisao entre a bola e um segmento de parede
bool colidiuComParede(float x, float y, const Parede& p) {
  float x1 = px(p.x1), y1 = py(p.y1), x2 = px(p.x2), y2 = py(p.y2);
  // projeta o centro da bola no segmento para achar o ponto mais proximo
  float dx = x2 - x1, dy = y2 - y1;
  float comprimento2 = dx * dx + dy * dy;
  if (comprimento2 <= 0.0f) return false;
  float t = constrain(((x - x1) * dx + (y - y1) * dy) / comprimento2, 0.0f, 1.0f);
  // compara a distancia ate esse ponto com o raio da bola mais 1 px de folga
  float distX = x - (x1 + t * dx), distY = y - (y1 + t * dy);
  float raio = R + 1.0f;
  return (distX * distX + distY * distY <= raio * raio);
}

// verifica colisao com as bordas da area e com todas as paredes
bool existeColisaoParede(float x, float y) {
  if (x - R < ESQ || x + R > DIR || y - R < TOPO || y + R > BASE) return true;
  for (int i = 0; i < numParedes; i++) if (colidiuComParede(x, y, paredes[i])) return true;
  return false;
}

// verifica se a bola esta na celula objetivo
bool verificarVitoria() {
  return ((int)((bolaX - ESQ) / CEL_W) == OBJ_COL && (int)((bolaY - TOPO) / CEL_H) == OBJ_LINHA);
}

// verifica se a bola esta em uma celula com bomba
bool verificarBomba() {
  int coluna = (int)((bolaX - ESQ) / CEL_W);
  int linha = (int)((bolaY - TOPO) / CEL_H);
  if (coluna < 0 || coluna >= COLUNAS || linha < 0 || linha >= LINHAS) return false;
  return celulaTemBomba(coluna, linha);
}

// verifica se existe passagem da celula para a vizinha indicada
bool temSaida(int c, int l, int dc, int dl) {
  int nc = c + dc, nl = l + dl;
  if (c < 0 || c >= COLUNAS || l < 0 || l >= LINHAS) return false;
  if (nc < 0 || nc >= COLUNAS || nl < 0 || nl >= LINHAS) return false;
  return passagemLivre(c, l, nc, nl);
}

// assistencia de curva: alinha a bola ao corredor que tem saida na direcao desejada
void assistirCurva(bool barradaEmX, int dir, float maximo) {
  // celula atual da bola
  int c = constrain((int)((bolaX - ESQ) / CEL_W), 0, COLUNAS - 1);
  int l = constrain((int)((bolaY - TOPO) / CEL_H), 0, LINHAS - 1);

  // barrada no eixo X: ajusta o Y para o centro da linha atual ou da vizinha com saida
  if (barradaEmX) {
    float centro = TOPO + (l + 0.5f) * CEL_H;
    int viz = (bolaY < centro) ? l - 1 : l + 1;
    int alvo = l;
    if (!temSaida(c, l, dir, 0) && temSaida(c, viz, dir, 0) && temSaida(c, l, 0, viz - l)) alvo = viz;
    // move ate o centro do alvo limitado ao passo maximo desta iteracao
    float p = constrain(TOPO + (alvo + 0.5f) * CEL_H - bolaY, -maximo, maximo);
    if (!existeColisaoParede(bolaX, bolaY + p)) bolaY += p;
  // barrada no eixo Y: ajusta o X para o centro da coluna atual ou da vizinha com saida
  } else {
    float centro = ESQ + (c + 0.5f) * CEL_W;
    int viz = (bolaX < centro) ? c - 1 : c + 1;
    int alvo = c;
    if (!temSaida(c, l, 0, dir) && temSaida(viz, l, 0, dir) && temSaida(c, l, viz - c, 0)) alvo = viz;
    float p = constrain(ESQ + (alvo + 0.5f) * CEL_W - bolaX, -maximo, maximo);
    if (!existeColisaoParede(bolaX + p, bolaY)) bolaX += p;
  }
}

// move a bola em sub-passos de ate 1.5 px para nao atravessar paredes
bool moverBola(float deslocX, float deslocY, float dt) {
  float distancia = sqrtf(deslocX * deslocX + deslocY * deslocY);
  int passos = max(1, (int)ceilf(distancia / 1.5f));
  float passoX = deslocX / passos;
  float passoY = deslocY / passos;
  float assist = ASSISTENCIA * dt / passos;

  // aplica cada sub-passo separando os eixos X e Y
  for (int i = 0; i < passos; i++) {
    // movimento no eixo X
    float novoX = bolaX + passoX;
    if (!existeColisaoParede(novoX, bolaY)) bolaX = novoX;
    else if (PAREDE_REINICIA) { reiniciarBola(); return true; }
    else {
      if (fabs(intencaoX) > fabs(intencaoY) && intencaoX != 0)
        assistirCurva(true, passoX > 0 ? 1 : -1, assist);
      velX = 0; passoX = 0;
    }

    // movimento no eixo Y
    float novoY = bolaY + passoY;
    if (!existeColisaoParede(bolaX, novoY)) bolaY = novoY;
    else if (PAREDE_REINICIA) { reiniciarBola(); return true; }
    else {
      if (fabs(intencaoY) > fabs(intencaoX) && intencaoY != 0)
        assistirCurva(false, passoY > 0 ? 1 : -1, assist);
      velY = 0; passoY = 0;
    }

    // chegou ao objetivo
    if (verificarVitoria()) {
      resolverModulo();
      return true;
    }

    // pisou em bomba: conta strike, avisa a central e volta ao inicio
    if (verificarBomba()) {
      strikes++;
      publicarEvento("ERROR");
      Serial.printf("STRIKE! Bomba pisada (total: %d) -> ERROR enviado\n", strikes);
      velX = 0; velY = 0;
      animarExplosao(); reiniciarBola();
      return true;
    }
  }
  return false;
}

// telas possiveis do modulo
enum Tela { T_NENHUMA, T_ESPERA, T_LABIRINTO, T_VITORIA, T_DERROTA };
Tela telaAtual = T_NENHUMA;

// tela de espera com titulo, mensagem, serial, conexao e ultima dica
void desenharEspera(const char* mensagem) {
  tela.fillScreen(COR_FUNDO);
  tela.setTextDatum(MC_DATUM);

  tela.setTextSize(2);
  tela.setTextColor(TFT_WHITE, COR_FUNDO);
  tela.drawString("LABIRINTO G" MODULO_ID, W / 2, 16);

  tela.setTextColor(TFT_YELLOW, COR_FUNDO);
  tela.drawString(mensagem, W / 2, 50);

  char sn[16];
  snprintf(sn, sizeof(sn), "SN: %s", serialBomba);
  tela.setTextColor(TFT_DARKGREY, COR_FUNDO);
  tela.drawString(sn, W / 2, 80);

  // indicador do estado da conexao
  tela.setTextSize(1);
  if (WiFi.status() != WL_CONNECTED)      { tela.setTextColor(TFT_RED, COR_FUNDO);   tela.drawString("Wi-Fi desconectado", W / 2, 104); }
  else if (!client.connected())           { tela.setTextColor(TFT_RED, COR_FUNDO);   tela.drawString("Sem conexao com a central", W / 2, 104); }
  else                                    { tela.setTextColor(TFT_GREEN, COR_FUNDO); tela.drawString("Conectado a central", W / 2, 104); }

  // ultima dica recebida, se houver
  if (grupoOrigemDica != 0) {
    char linha[48];
    snprintf(linha, sizeof(linha), "Dica de G%d: %.30s", grupoOrigemDica, conteudoDica);
    tela.setTextColor(TFT_CYAN, COR_FUNDO);
    tela.drawString(linha, W / 2, 124);
  }
}

// tela final da partida (vitoria ou derrota)
void desenharFim(bool vitoria) {
  uint16_t fundo = vitoria ? TFT_DARKGREEN : TFT_RED;
  tela.fillScreen(fundo);
  tela.setTextDatum(MC_DATUM);
  tela.setTextColor(TFT_WHITE, fundo);
  tela.setTextSize(3);
  tela.drawString(vitoria ? "DESARMADA!" : "BOOM!", W / 2, H / 2 - 10);
  tela.setTextSize(1);
  tela.drawString(vitoria ? "Bomba desarmada" : "Bomba detonada", W / 2, H / 2 + 25);
}

// mostra a ultima dica na faixa superior, no lugar dos numeros das colunas
void desenharDicaNoTopo() {
  if (grupoOrigemDica == 0) return;
  char linha[48];
  snprintf(linha, sizeof(linha), "Dica G%d: %.30s", grupoOrigemDica, conteudoDica);
  tela.fillRect(0, 0, W, TOPO - 1, COR_FUNDO);
  tela.setTextDatum(TL_DATUM);
  tela.setTextSize(1);
  tela.setTextColor(TFT_CYAN, COR_FUNDO);
  tela.drawString(linha, 2, 0);
}

// recebe mensagens MQTT; apenas atualiza variaveis, a tela e tratada no loop
void callbackMqtt(char* topic, byte* payload, unsigned int length) {
  // copia o payload para uma string e remove quebras de linha e espacos finais
  char msg[length + 1];
  memcpy(msg, payload, length);
  msg[length] = '\0';
  while (length > 0 && (msg[length - 1] == '\r' || msg[length - 1] == '\n' || msg[length - 1] == ' ')) {
    msg[--length] = '\0';
  }
  Serial.printf("[MQTT] %s = %s\n", topic, msg);

  // no modo de teste ignora o controle de partida da central
  if (MODO_TESTE && (strcmp(topic, "jogo/estado") == 0 || strcmp(topic, "jogo/modulo/ativo") == 0)) {
    Serial.println("        (ignorado: MODO_TESTE)");
    return;
  }

  // estado global da partida
  if (strcmp(topic, "jogo/estado") == 0) {
    if (strcmp(msg, "START") == 0) {
      // START retido chega de novo ao reconectar; so inicia partida nova se nao estava rodando
      if (!jogoRodando) { jogoRodando = true; resultadoFinal = 0; novaPartida = true; }
    } else if (strcmp(msg, "RESET") == 0) {
      jogoRodando = false; minhaVez = false; resultadoFinal = 0;
      moduloResolvido = false;
      digitalWrite(LED_VITORIA_PIN, LOW);
    } else if (strcmp(msg, "VICTORY") == 0) {
      jogoRodando = false; minhaVez = false; resultadoFinal = 1;
    } else if (strcmp(msg, "DEFEAT") == 0) {
      jogoRodando = false; minhaVez = false; resultadoFinal = 2;
    }
    precisaRedesenhar = true;
  }
  // modulo ativo: libera o jogo apenas na vez deste grupo
  else if (strcmp(topic, "jogo/modulo/ativo") == 0) {
    minhaVez = (atoi(msg) == MODULO_NUM);
    precisaRedesenhar = true;
  }
  // numero de serie da bomba
  else if (strcmp(topic, "jogo/serial") == 0) {
    strncpy(serialBomba, msg, sizeof(serialBomba) - 1);
    serialBomba[sizeof(serialBomba) - 1] = '\0';
    precisaRedesenhar = true;
  }
  // dica enviada por outro grupo: extrai o grupo de origem do topico
  else {
    int origem = 0, pos = 0;
    if (sscanf(topic, "jogo/modulo/" MODULO_ID "/dica/%d%n", &origem, &pos) == 1 && topic[pos] == '\0') {
      grupoOrigemDica = origem;
      strncpy(conteudoDica, msg, sizeof(conteudoDica) - 1);
      conteudoDica[sizeof(conteudoDica) - 1] = '\0';
      temDicaNova = true;
    }
  }
}

// conecta ao broker com LWT (OFFLINE automatico se cair) e assina os topicos
bool conectarMqtt() {
  if (!client.connect("Modulo_" MODULO_ID, "jogo/modulo/" MODULO_ID "/status", 0, true, "OFFLINE")) {
    Serial.printf("[MQTT] falha ao conectar (rc=%d)\n", client.state());
    return false;
  }
  client.publish("jogo/modulo/" MODULO_ID "/status", "ONLINE", true);
  client.subscribe("jogo/estado");
  client.subscribe("jogo/serial");
  client.subscribe("jogo/modulo/ativo");
  client.subscribe("jogo/modulo/" MODULO_ID "/dica/+");
  Serial.println("[MQTT] conectado e ONLINE");
  return true;
}

// mantem Wi-Fi e MQTT ativos, tentando reconectar a cada 3 s sem travar o jogo
void manterConexao() {
  static bool estavaConectado = false;
  bool conectado = (WiFi.status() == WL_CONNECTED) && client.connected();

  if (!conectado && WiFi.status() == WL_CONNECTED && millis() - ultimaTentativaMqtt > 3000) {
    ultimaTentativaMqtt = millis();
    conectado = conectarMqtt();
  }
  if (conectado) client.loop();

  // atualiza o indicador de conexao na tela de espera
  if (conectado != estavaConectado) {
    estavaConectado = conectado;
    if (telaAtual == T_ESPERA) precisaRedesenhar = true;
  }
}

// converte coluna/linha no nome da celula (ex.: B4)
void nomeCelula(int coluna, int linha, char* out) {
  out[0] = 'A' + linha;
  snprintf(out + 1, 3, "%d", coluna + 1);
}

// envia as 3 posicoes das bombas para a central distribuir como dicas
void enviarDicasBombas() {
  dicasEnviadas = true;
  for (int i = 0; i < NUM_BOMBAS; i++) {
    char cel[4];
    nomeCelula(bombas[i].coluna, bombas[i].linha, cel);
    if (MODO_TESTE) {
      char m[24];
      snprintf(m, sizeof(m), "dica: %s", cel);
      client.publish("jogo/modulo/" MODULO_ID "/teste", m);
    } else {
      client.publish("jogo/modulo/" MODULO_ID "/dica", cel);
    }
    Serial.printf("[DICA] bomba %s%s\n", cel, MODO_TESTE ? " (teste, nao enviada a central)" : " -> central");
  }
}

// reinicia a media de calibracao do sensor
void iniciarCalibracao() {
  calibrado = false;
  somaCalX = 0; somaCalY = 0; amostrasCal = 0;
  inicioCalibracao = millis();
}

// maquina de estados: aplica os pedidos do callback e define a tela atual
void atualizarEstado() {
  // nova partida: reseta o estado e gera um novo labirinto
  if (novaPartida) {
    novaPartida = false;
    moduloResolvido = false;
    strikes = 0;
    dicasEnviadas = false;
    digitalWrite(LED_VITORIA_PIN, LOW);
    gerarMapaValido();
    bolaX = INICIO_X; bolaY = INICIO_Y; velX = 0; velY = 0;
    telaAtual = T_NENHUMA;
  }

  // escolhe a tela de acordo com o estado da partida
  Tela desejada;
  const char* mensagem = "";
  if (resultadoFinal == 1)      desejada = T_VITORIA;
  else if (resultadoFinal == 2) desejada = T_DERROTA;
  else if (!jogoRodando)        { desejada = T_ESPERA; mensagem = "AGUARDANDO INICIO"; }
  else if (moduloResolvido)     desejada = T_LABIRINTO;
  else if (minhaVez)            desejada = T_LABIRINTO;
  else                          { desejada = T_ESPERA; mensagem = "AGUARDE SUA VEZ"; }

  // redesenha apenas quando a tela muda ou a tela de espera precisa ser atualizada
  if (desejada != telaAtual || (precisaRedesenhar && desejada == T_ESPERA)) {
    if (desejada == T_ESPERA) desenharEspera(mensagem);
    else if (desejada == T_VITORIA) desenharFim(true);
    else if (desejada == T_DERROTA) desenharFim(false);
    // ao entrar no labirinto: inicia a calibracao e envia as dicas, ou exibe a dica se ja resolvido
    else if (desejada == T_LABIRINTO && telaAtual != T_LABIRINTO) {
      desenharMapa();
      desenharBola();
      if (moduloResolvido) desenharDicaNoTopo();
      else {
        iniciarCalibracao();
        if (!dicasEnviadas) enviarDicasBombas();
      }
    }
    telaAtual = desejada;
  }
  precisaRedesenhar = false;

  // exibe uma dica recem-chegada fora da jogada ativa
  if (temDicaNova) {
    temDicaNova = false;
    Serial.printf("Dica recebida de G%d: %s\n", grupoOrigemDica, conteudoDica);
    if (telaAtual == T_ESPERA) desenharEspera(mensagem);
    else if (telaAtual == T_LABIRINTO && moduloResolvido) desenharDicaNoTopo();
  }
}

// inicializacao do hardware, da rede e do modo de jogo
void setup() {
  Serial.begin(115200);
  // liga o backlight e deixa o LED de status apagado
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH);
  pinMode(LED_VITORIA_PIN, OUTPUT); digitalWrite(LED_VITORIA_PIN, LOW);
  tela.init();
  tela.setRotation(3);

  // tela de carregamento
  tela.fillScreen(COR_FUNDO);
  tela.setTextColor(TFT_WHITE, COR_FUNDO);
  tela.setTextDatum(MC_DATUM);
  tela.setTextSize(2);
  tela.drawString("Conectando...", W / 2, H / 2);

  // inicializa o MPU6050 no I2C (SDA 21, SCL 22) e trava com aviso se nao encontrar
  Wire.begin(21, 22);
  if (!mpu.begin()) {
    Serial.println("MPU6050 nao encontrado!");
    tela.fillScreen(COR_FUNDO);
    tela.setTextColor(TFT_RED, COR_FUNDO);
    tela.drawString("MPU6050 nao", W / 2, H / 2 - 12);
    tela.drawString("encontrado!", W / 2, H / 2 + 12);
    while (true) delay(500);
  }
  // faixas do acelerometro e giroscopio e filtro passa-baixa interno
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  // semente aleatoria a partir do gerador de hardware do ESP32
  randomSeed(esp_random());

  // conecta ao Wi-Fi aguardando ate 10 s
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) delay(250);
  Serial.println(WiFi.status() == WL_CONNECTED ? "Wi-Fi conectado" : "Wi-Fi ainda nao conectou");

  // configura o broker e tenta a primeira conexao MQTT
  client.setServer(MQTT_BROKER, MQTT_PORT);
  client.setCallback(callbackMqtt);
  if (WiFi.status() == WL_CONNECTED) conectarMqtt();
  ultimaTentativaMqtt = millis();

  // no modo de teste comeca a jogar sem esperar a central
  pinMode(BTN_NOVO_LABIRINTO, INPUT);
  if (MODO_TESTE) {
    Serial.println("*** MODO_TESTE ativo: central ignorada ***");
    jogoRodando = true; minhaVez = true; novaPartida = true;
  }
}

// laco principal: rede, estado da partida, leitura do sensor e fisica da bola
void loop() {
  manterConexao();

  // modo de teste: botao GPIO35 gera labirinto novo (com intervalo minimo de 500 ms)
  static unsigned long ultimoBotao = 0;
  if (MODO_TESTE && digitalRead(BTN_NOVO_LABIRINTO) == LOW && millis() - ultimoBotao > 500) {
    ultimoBotao = millis();
    novaPartida = true;
  }

  atualizarEstado();

  // so processa o jogo durante a vez deste modulo e antes de resolver
  if (!(jogoRodando && minhaVez && !moduloResolvido)) {
    delay(10);
    return;
  }

  // leitura do acelerometro
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // intervalo desde o ultimo quadro, limitado para evitar saltos
  unsigned long agora = millis();
  float dt = (agora - tempoAnterior) / 1000.0f;
  tempoAnterior = agora;

  if (dt > 0.05f) dt = 0.05f;
  if (dt <= 0.0f) dt = 0.001f;

  // mapeia os eixos do sensor para os eixos da tela
  float leituraX = SINAL_X * a.acceleration.y;
  float leituraY = SINAL_Y * a.acceleration.x;

  // filtro passa-baixa exponencial para suavizar o ruido
  filtroX += 0.15f * (leituraX - filtroX);
  filtroY += 0.15f * (leituraY - filtroY);

  // calibracao: media das leituras por 2 s define a referencia de nivel
  if (!calibrado) {
    somaCalX += filtroX; somaCalY += filtroY; amostrasCal++;
    if (agora - inicioCalibracao >= 2000) {
      refX = somaCalX / amostrasCal; refY = somaCalY / amostrasCal;
      filtroX = refX; filtroY = refY;
      calibrado = true; tempoAnterior = agora;
    }
    delay(5);
    return;
  }

  // inclinacao relativa a referencia, com zona morta para ignorar pequenas variacoes
  float inclinacaoX = filtroX - refX;
  float inclinacaoY = filtroY - refY;

  if (fabs(inclinacaoX) < ZONA_MORTA) inclinacaoX = 0;
  if (fabs(inclinacaoY) < ZONA_MORTA) inclinacaoY = 0;

  intencaoX = inclinacaoX;
  intencaoY = inclinacaoY;

  // velocidade alvo proporcional a inclinacao; a velocidade real converge de forma exponencial
  float alvoX = constrain(inclinacaoX * GANHO, -VELOCIDADE_MAX, VELOCIDADE_MAX);
  float alvoY = constrain(inclinacaoY * GANHO, -VELOCIDADE_MAX, VELOCIDADE_MAX);
  float k = 1.0f - expf(-RESPOSTA * dt);
  velX += (alvoX - velX) * k;
  velY += (alvoY - velY) * k;

  // zera velocidades residuais quando nao ha inclinacao
  if (alvoX == 0 && fabs(velX) < 1.0f) velX = 0;
  if (alvoY == 0 && fabs(velY) < 1.0f) velY = 0;

  int antigoX = roundf(bolaX), antigoY = roundf(bolaY);

  // move a bola; retorna true se houve reinicio, vitoria ou bomba
  if (moverBola(velX * dt, velY * dt, dt)) {
    delay(5);
    return;
  }

  // redesenha apenas se a posicao em pixels mudou
  if (roundf(bolaX) != antigoX || roundf(bolaY) != antigoY) {
    atualizarBola();
  }

  delay(5);
}
/*
 * ============================================================================
 *  KTNE-ESP32 — Grupo 3: SEQUÊNCIA DE SINAIS (Jogo da Memória)
 *  Gabriel Baggio, Arthur B. e Diego
 * ============================================================================
 *
 *  O display mostra 4 quadrados (2x2). Eles piscam numa sequência aleatória
 *  e o jogador repete a sequência nos 4 botões físicos, pela POSIÇÃO:
 *
 *        +-----------+-----------+
 *        | CIMA-ESQ  | CIMA-DIR  |      quadrado 0 -> botão GPIO 2
 *        |  (verde)  | (vermelho)|      quadrado 1 -> botão GPIO 32
 *        +-----------+-----------+      quadrado 2 -> botão GPIO 15
 *        | BAIXO-ESQ | BAIXO-DIR |      quadrado 3 -> botão GPIO 33
 *        | (amarelo) |  (azul)   |
 *        +-----------+-----------+
 *
 *  LIGAÇÃO: cada botão tem um lado no GND (pino "G") e o outro no GPIO.
 *  Os pinos usam o pull-up interno do ESP32: solto = HIGH, apertado = LOW.
 *  Não precisa de resistor externo.
 *
 *  Estilo "Genius": a sequência começa com TAMANHO_INICIAL (3) sinais e
 *  cada rodada acrescenta 1 sinal, até TAMANHO_MAXIMO (8). A cada rodada o
 *  piscar fica mais rápido. Completar a última rodada = módulo RESOLVIDO.
 *
 *  ---------------------------------------------------------------------------
 *  BIBLIOTECAS (Arduino IDE > Gerenciador de Bibliotecas)
 *   - TFT_eSPI (Bodmer)   -> em User_Setup_Select.h, comente o
 *                            "#include <User_Setup.h>" e descomente
 *                            "#include <User_Setups/Setup25_TTGO_T_Display.h>"
 *   - PubSubClient (Nick O'Leary)
 *  Placa: "ESP32 Dev Module" (ou "TTGO T1"), pacote esp32 da Espressif.
 *
 *  ---------------------------------------------------------------------------
 *  PROTOCOLO MQTT (RFC 001 do Grupo 1 — texto puro, sem JSON)
 *  https://github.com/GroosL/KTNE-ESP32
 *
 *  Assina:
 *   jogo/estado            START | RESET | VICTORY | DEFEAT     (retain)
 *   jogo/serial            ex.: "B4-92" ou "-----"             (retain)
 *   jogo/modulo/ativo      grupo da vez, ex.: "2", "3"; "0" = nenhum (retain)
 *   jogo/tempo             segundos restantes, ex.: "299"       (opcional, 1 Hz)
 *   jogo/erros             strikes da bomba, "0" a "3"           (retain)
 *   jogo/acertos           módulos já resolvidos                (retain)
 *   jogo/total             total de módulos na partida          (retain)
 *   jogo/modulos           IDs online, ex.: "2 3 4" ou "0"      (retain)
 *   jogo/modulo/3/dica/+   dicas de outros módulos; o último nível do
 *                          tópico é o grupo que enviou. Aparecem por
 *                          TEMPO_DICA_MS na tela compartilhada drawDica().
 *
 *  Publica:
 *   jogo/modulo/3/status   "ONLINE" ao conectar (retain). "OFFLINE" fica
 *                          registrado como Last Will (retain): se o ESP cair,
 *                          o broker avisa a Central, que tira o módulo da fila.
 *   jogo/modulo/3/evento   "SOLVED" ao vencer a última rodada
 *                          "ERROR"  a cada botão errado ou tempo esgotado
 *                          Só é enviado na vez deste módulo (a Central
 *                          ignora eventos de quem não é o módulo ativo).
 *   jogo/modulo/3/dica     dica do sinal oculto (ver abaixo), ex.: "5o = AZUL".
 *                          A Central sorteia um módulo em espera e entrega
 *                          a ele em jogo/modulo/<sorteado>/dica/3.
 *                          (Com DICA_DESTINO = N vai direto para
 *                          jogo/modulo/N/dica/3.)
 *
 *  TURNOS: a Central sorteia a ordem dos módulos a cada partida e libera um
 *  por vez. Depois do START o módulo fica na tela compartilhada até
 *  jogo/modulo/ativo ser "3"; só então a sequência começa. Se a vez passar
 *  para outro grupo no meio do desafio, ele é bloqueado.
 *
 *  ---------------------------------------------------------------------------
 *  TELAS COMPARTILHADAS (iguais em todos os módulos)
 *  Copiadas de exemplos/exemplo_modulo.ino do repositório, sem alteração,
 *  na seção "FUNÇÕES COMPARTILHADAS". Elas foram desenhadas em PAISAGEM
 *  (240x135); nosso jogo é em RETRATO (grade 2x2), então o display gira:
 *  ROTACAO_COMPARTILHADA nas telas compartilhadas, ROTACAO_DISPLAY no jogo.
 *   drawLobby()      antes do START: "AGUARDANDO INICIO" + módulos online
 *   drawRunning()    partida rodando e não é a nossa vez (ou já resolvemos):
 *                    SN, módulo atual, tempo, erros e progresso da bomba
 *   drawDica()       dica recebida de outro módulo, por TEMPO_DICA_MS (20 s)
 *   drawFinish()     VICTORY / DEFEAT
 *  Também usamos registrarSucesso(), registrarErro() e enviarDicaAleatoria()
 *  / enviarDicaDireta() do exemplo (com a fila de reenvio deste módulo).
 *
 *  ---------------------------------------------------------------------------
 *  SISTEMA DE DICAS
 *  Só um módulo joga por vez; os outros ficam em espera (drawRunning). O
 *  módulo que está jogando cria uma dica e manda para a Central, que entrega
 *  a um módulo em espera sorteado. Esse módulo mostra a dica (drawDica) e
 *  quem está nele fala para quem está jogando.
 *
 *  Nossa dica — SINAL OCULTO: a partir da rodada RODADA_OCULTA (3), o sinal
 *  que entra na sequência nessa rodada (o 5º) não pisca: no lugar dele os
 *  quatro quadrados ficam cinza com "?". Ao começar essa rodada, a cor dele
 *  vai como dica para a Central ("5o = VERDE"; curta para caber em uma linha
 *  da drawDica). Na hora de repetir, esse passo tem TEMPO_LIMITE_OCULTO_MS
 *  (20 s) em vez de 5 s, para dar tempo de pedir a dica aos colegas. O sinal
 *  continua oculto até o fim da sequência (decore a dica!); se a sequência
 *  for refeita após um erro, sai uma dica nova.
 *  No modo solo não há sinal oculto (não há ninguém para receber a dica).
 *
 *  ---------------------------------------------------------------------------
 *  ESTADOS DO MÓDULO
 *   CONECTANDO          tentando WiFi/broker (até TIMEOUT_WIFI_MS)
 *   AGUARDANDO_CENTRAL  online, esperando START da Central (drawLobby)
 *   AGUARDANDO_VEZ      partida rodando, esperando a vez (drawRunning)
 *                       (ou sem rede: desafio pausado até reconectar)
 *   AGUARDANDO_SOLO     sem Central: aperte um botão para jogar
 *   PREPARANDO          "OBSERVE" antes de cada sequência
 *   MOSTRANDO           quadrados piscando (botões ignorados)
 *   ENTRADA             jogador repete a sequência
 *   RODADA_OK           acertou a rodada, vai para a próxima
 *   ERRO                errou/estourou o tempo: ERROR + bloqueio temporário
 *   RESOLVIDO           venceu a última rodada: SOLVED; depois de
 *                       TEMPO_RESOLVIDO_MS mostra drawRunning
 *   VITORIA / DERROTA   ordem da Central: módulo travado (drawFinish)
 *
 *  QUEDA DE REDE: se a conexão cair durante a vez do módulo, o desafio pausa
 *  (a Central não receberia o resultado) e recomeça quando a rede voltar,
 *  se ainda for a vez dele. Eventos que não puderam ser enviados ficam
 *  guardados e saem na reconexão.
 *
 *  MODO STANDALONE: se não houver WiFi/broker, ou se nenhuma mensagem de
 *  jogo/estado chegar em TIMEOUT_CENTRAL_MS, o módulo joga sozinho. Nas telas
 *  de espera (e em AGUARDANDO_VEZ sem rede), SEGURAR qualquer botão por 2 s
 *  também força o modo solo. No modo solo, um START da Central traz o
 *  módulo de volta ao jogo em rede.
 * ============================================================================
 */

 // ============================================================================
//  PAPEL DESTE DISPOSITIVO (Co-op a 2 dispositivos)
// ============================================================================
// 0 = COMPLETO   (Joga e vê no mesmo aparelho - Padrão original)
// 1 = OBSERVADOR (Apenas vê as cores piscando, botões físicos desativados)
// 2 = JOGADOR    (Só aperta os botões; não vê a sequência)
// 3 = LOOPBACK   (Testa a comunicação cooperativa no próprio ESP via MQTT)
// Só um aparelho representa o módulo 3 perante a Central (status/LWT,
// eventos e pistas): todos os papéis menos o JOGADOR. Assim, desligar o
// JOGADOR não faz o broker anunciar o módulo como OFFLINE.
#define PAPEL_ATUAL 3 // <-- Mudar isso ao upar para cada ESP

#include <WiFi.h>
#include <PubSubClient.h>
#include <TFT_eSPI.h>
#include <SPI.h>

// ============================================================================
//  CONFIGURAÇÃO DO JOGO  (altere aqui)
// ============================================================================
#define TAMANHO_INICIAL          3     // sinais na primeira rodada
#define TAMANHO_MAXIMO           8     // sinais na última rodada (pode aumentar)

#define TEMPO_ACESO_INICIAL_MS   600   // quanto cada quadrado fica aceso na 1ª rodada
#define TEMPO_ACESO_MINIMO_MS    250   // limite de velocidade nas rodadas finais
#define REDUCAO_POR_RODADA_MS    50    // quanto o piscar acelera a cada rodada
#define TEMPO_APAGADO_MS         250   // intervalo apagado entre dois sinais
#define TEMPO_PREPARO_MS         1000  // tela "OBSERVE" antes da sequência
#define TEMPO_RODADA_OK_MS       900   // tela "BOA!" entre rodadas

#define TEMPO_LIMITE_TOQUE_MS    5000  // tempo máximo entre dois toques (0 = sem limite)
#define BLOQUEIO_ERRO_MS         3000  // módulo travado após um erro
#define VOLTAR_AO_INICIO_NO_ERRO 1     // 1 = erro volta para TAMANHO_INICIAL com sequência nova
                                       // 0 = erro repete a mesma rodada

// Sinal oculto / dica (ver "SISTEMA DE DICAS" no topo)
#define USAR_SINAL_OCULTO        1     // 0 = desliga (Genius normal, sem dica)
#define RODADA_OCULTA            3     // primeira rodada com o sinal oculto
#define TEMPO_LIMITE_OCULTO_MS   20000 // tempo para apertar o sinal oculto (pedir a dica)
#define DICA_DESTINO             0     // 0 = a Central sorteia um módulo em espera
                                       // N = envia direto ao Grupo N

#define TEMPO_RESOLVIDO_MS       2000  // tela "RESOLVIDO" antes da tela compartilhada

// Valores do exemplo compartilhado (exemplos/exemplo_modulo.ino)
#define MAX_ERROS 3                              // strikes da bomba (drawRunning)
constexpr unsigned long TEMPO_DICA_MS = 20000;   // quanto tempo drawDica fica na tela

#if TAMANHO_MAXIMO < TAMANHO_INICIAL
#error "TAMANHO_MAXIMO precisa ser >= TAMANHO_INICIAL"
#endif

// A rodada r tem TAMANHO_INICIAL + r - 1 sinais; o sinal que entra nela tem
// índice TAMANHO_INICIAL + r - 2. É esse que fica oculto (o jogador nunca o
// viu, então só sabe a cor pela dica).
#define POS_OCULTA (TAMANHO_INICIAL + RODADA_OCULTA - 2)
#if USAR_SINAL_OCULTO && (RODADA_OCULTA < 1 || POS_OCULTA >= TAMANHO_MAXIMO)
#error "RODADA_OCULTA precisa ser uma das rodadas do jogo"
#endif

// ============================================================================
//  HARDWARE
// ============================================================================
#define PINO_CIMA_ESQ    2
#define PINO_CIMA_DIR    32
#define PINO_BAIXO_ESQ   15
#define PINO_BAIXO_DIR   33

// 0 = botão liga o GPIO ao GND quando apertado (lê LOW)  -> usa pull-up  <- NOSSA LIGAÇÃO
// 1 = botão liga o GPIO ao 3V3 quando apertado (lê HIGH) -> usa pull-down
// ATENÇÃO: não use GPIO 34 a 39 para botões (inclui 37 e 38): eles NÃO
// têm resistor interno e ficariam flutuando sem um resistor externo.
// Evite também os pinos do display (4, 5, 16, 18, 19, 23) e o GPIO 12.
#define BOTAO_ATIVO_EM_HIGH  0

#define DEBOUNCE_MS          30
#define TEMPO_SEGURAR_SOLO_MS 2000  // segurar botão nas telas de espera -> modo solo

#define ROTACAO_DISPLAY      0      // nosso jogo: 0 = retrato. De cabeça para baixo? use 2
#define ROTACAO_COMPARTILHADA 1     // telas compartilhadas: 1 = paisagem, como no exemplo.
                                    // De cabeça para baixo? use 3
#define PINO_BACKLIGHT       4      // backlight do TTGO T-Display

// 1 = só testa os botões (acende o quadrado do botão apertado), sem jogo/rede.
//     Útil para conferir a ligação antes de jogar.
#define MODO_TESTE_BOTOES    0

// ============================================================================
//  REDE / MQTT
// ============================================================================
#define STR_(x) #x
#define STR(x)  STR_(x)

#define MODULO_NUM 3                  // número do grupo (comparado com jogo/modulo/ativo)
#define MODULO_ID  STR(MODULO_NUM)    // o mesmo número como texto: "3"

const char* WIFI_SSID     = "iot2022";
const char* WIFI_PASSWORD = "S3nhab0@";
const char* MQTT_BROKER   = "192.168.2.100";
const int   MQTT_PORT     = 1883;

#define TIMEOUT_WIFI_MS        10000  // espera pelo WiFi ao ligar
#define TIMEOUT_CENTRAL_MS     5000   // espera por jogo/estado antes de ir para solo
#define INTERVALO_RECONEXAO_MS 5000   // entre tentativas de reconectar ao broker

#define TOPICO_ESTADO         "jogo/estado"
#define TOPICO_SERIAL         "jogo/serial"
#define TOPICO_ATIVO          "jogo/modulo/ativo"
#define TOPICO_TEMPO          "jogo/tempo"
#define TOPICO_ERROS          "jogo/erros"
#define TOPICO_ACERTOS        "jogo/acertos"
#define TOPICO_TOTAL          "jogo/total"
#define TOPICO_MODULOS        "jogo/modulos"
#define TOPICO_STATUS         "jogo/modulo/" MODULO_ID "/status"
#define TOPICO_EVENTO         "jogo/modulo/" MODULO_ID "/evento"
#define TOPICO_DICA_RECEBIDA  "jogo/modulo/" MODULO_ID "/dica/+"     // + = grupo que enviou
#define FORMATO_DICA_RECEBIDA "jogo/modulo/" MODULO_ID "/dica/%d%n"  // extrai o remetente
// (o envio de dicas usa enviarDicaAleatoria/enviarDicaDireta do exemplo)

#if USAR_SINAL_OCULTO && DICA_DESTINO == MODULO_NUM
#error "DICA_DESTINO nao pode ser o proprio modulo"
#endif

// Sincronização interna do co-op (a Central não assina estes tópicos)
#define TOPICO_SYNC_SEQ    "jogo/modulo/" MODULO_ID "/interno/seq"
#define TOPICO_SYNC_BOTAO  "jogo/modulo/" MODULO_ID "/interno/botao"

int botaoRemoto = -1; // Armazena o clique recebido via MQTT

// ============================================================================
//  TIPOS E VARIÁVEIS GLOBAIS
// ============================================================================
enum Estado {
  CONECTANDO,
  AGUARDANDO_CENTRAL,
  AGUARDANDO_VEZ,
  AGUARDANDO_SOLO,
  PREPARANDO,
  MOSTRANDO,
  ENTRADA,
  RODADA_OK,
  ERRO,
  RESOLVIDO,
  VITORIA,
  DERROTA
};

const char* NOME_ESTADO[] = {
  "CONECTANDO", "AGUARDANDO_CENTRAL", "AGUARDANDO_VEZ", "AGUARDANDO_SOLO", "PREPARANDO",
  "MOSTRANDO", "ENTRADA", "RODADA_OK", "ERRO", "RESOLVIDO", "VITORIA", "DERROTA"
};

// Índices: 0 = cima-esq, 1 = cima-dir, 2 = baixo-esq, 3 = baixo-dir
const uint8_t PINOS[4]    = { PINO_CIMA_ESQ, PINO_CIMA_DIR, PINO_BAIXO_ESQ, PINO_BAIXO_DIR };
const char*   NOME_POS[4] = { "CIMA-ESQ", "CIMA-DIR", "BAIXO-ESQ", "BAIXO-DIR" };
const char*   NOME_COR[4] = { "VERDE", "VERMELHO", "AMARELO", "AZUL" };

struct Botao {
  bool     estavel;    // estado já filtrado (true = apertado)
  bool     leitura;    // última leitura bruta
  uint32_t tMudanca;   // quando a leitura bruta mudou
  uint32_t tPress;     // quando o botão foi apertado (estado estável)
};
Botao botoes[4];
bool pressionou[4];    // borda de descida (apertou) neste ciclo
bool soltou[4];        // borda de subida (soltou) neste ciclo

TFT_eSPI     tft = TFT_eSPI();
WiFiClient   espClient;
PubSubClient mqtt(espClient);
char         clientId[24];

// Estado do jogo
Estado   estado = CONECTANDO;
uint32_t tEstado = 0;                 // millis() da entrada no estado atual
bool     modoSolo = false;

uint8_t  sequencia[TAMANHO_MAXIMO];
uint8_t  tamanhoAtual = TAMANHO_INICIAL;
uint8_t  passoMostra = 0;             // índice do sinal sendo mostrado
bool     sinalAceso = false;
uint8_t  indiceEntrada = 0;           // próximo sinal que o jogador deve acertar
uint32_t tUltimoToque = 0;
int      larguraBarra = -1;           // cache para redesenhar a barra só quando muda

// Dados vindos da rede. Os nomes marcados com (C) são os mesmos do exemplo
// compartilhado, porque as funções compartilhadas os usam diretamente.
enum EstadoJogo { LOBBY, RUNNING, VICTORY, DEFEAT };   // (C) espelha jogo/estado
EstadoJogo estadoJogo = LOBBY;                          // (C)
char     serialBomba[8] = "-----";                      // (C)
char     listaModulos[64] = "NENHUM MODULO ONLINE";     // (C) jogo/modulos formatado
int      tempoRestante = -1;          // (C) -1 = Central ainda não mandou o tempo
int      erros = 0;                   // (C) jogo/erros
int      acertos = 0;                 // (C) jogo/acertos
int      totalModulos = 0;            // (C) jogo/total
int      moduloAtivo = 0;             // (C) jogo/modulo/ativo: grupo da vez (0 = nenhum)
char     serialPartida[8] = "";       // serial da partida em andamento (detecta START retido)
char     ultimoEstadoCentral[10] = "";
bool     jaResolvido = false;         // (C) SOLVED desta partida já aconteceu
bool     centralDetectada = false;
bool     aguardandoDeteccao = false;
uint32_t tConexaoMqtt = 0;
uint32_t tUltimaTentativa = 0;

// Mensagens que não puderam ser enviadas (broker fora do ar)
uint8_t  errosPendentes = 0;
bool     solvedPendente = false;
bool     dicaPendente = false;

// Dica do sinal oculto (enviada por este módulo)
char     textoDica[48] = "";
bool     dicaEnviada = false;         // já mandou a dica desta sequência

// Dicas recebidas de outros módulos (C)
bool     temDicaNova = false;         // (C) chegou agora: drawDica() no próximo loop
bool     temDicaAtiva = false;        // (C) drawDica() está na tela
unsigned long inicioDicaMs = 0;       // (C)
int      grupoOrigemDica = 0;         // (C)
char     conteudoDica[64] = "";       // (C)

// Telas compartilhadas: o que redesenhar no próximo loop() (C)
bool     precisaDesenharTela = false;   // (C) tela inteira
bool     precisaAtualizarDados = false; // (C) só os dados (tempo, erros, ...)
bool     telaCompartilhada = false;     // a tela atual é uma das compartilhadas (paisagem)

// Layout do display (calculado em setup)
int LARG, ALT, LADO;
const int GAP = 5;
const int Y_GRADE = 34;
int Y_MSG, Y_BARRA, Y_RODAPE;
bool cabecalhoSujo = true;

uint16_t COR_ACESA[4];
uint16_t COR_APAGADA[4];
uint16_t COR_OCULTA;

// Protótipos
void mudarEstado(Estado novo);
void novaPartida();
void iniciarRodada();

// ============================================================================
//  REGRAS DE TURNO (RFC 001)
// ============================================================================
// A Central mandou START e ainda não encerrou a partida
bool partidaEmAndamento() { return estadoJogo == RUNNING; }

// Jogo em rede, partida rodando e jogo/modulo/ativo aponta para este módulo
bool minhaVez() { return !modoSolo && partidaEmAndamento() && moduloAtivo == MODULO_NUM; }

// Este aparelho fala com a Central em nome do módulo? (no co-op, o Jogador não)
bool falaComCentral() { return PAPEL_ATUAL != 2; }

// O desafio está em andamento (sequência sendo mostrada ou repetida)
bool emJogada() {
  return estado == PREPARANDO || estado == MOSTRANDO || estado == ENTRADA ||
         estado == RODADA_OK  || estado == ERRO;
}

// O sinal 'i' fica oculto nesta rodada? Só no jogo em rede: no modo solo
// não há ninguém em espera para receber a dica.
bool sinalOculto(uint8_t i) {
#if USAR_SINAL_OCULTO
  return !modoSolo && i == POS_OCULTA && tamanhoAtual > POS_OCULTA;
#else
  return false;
#endif
}

// ============================================================================
//  BOTÕES
// ============================================================================
bool lerPino(uint8_t i) {
  int v = digitalRead(PINOS[i]);
  return BOTAO_ATIVO_EM_HIGH ? (v == HIGH) : (v == LOW);
}

void configurarBotoes() {
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t modo;
    if (PINOS[i] >= 34) {                                // sem resistor interno
      modo = INPUT;
      Serial.printf("[BOTAO] AVISO: GPIO %d nao tem pull-up/pull-down interno!\n", PINOS[i]);
    } else modo = BOTAO_ATIVO_EM_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP;
    pinMode(PINOS[i], modo);
  }
  delay(5);
  uint32_t agora = millis();
  for (uint8_t i = 0; i < 4; i++) {
    bool l = lerPino(i);
    botoes[i] = { l, l, agora, agora };
  }
}

// Debounce: só aceita mudança que se mantém por DEBOUNCE_MS
void atualizarBotoes() {
  uint32_t agora = millis();
  for (uint8_t i = 0; i < 4; i++) {
    pressionou[i] = false;
    soltou[i] = false;
    bool l = lerPino(i);
    Botao& b = botoes[i];
    if (l != b.leitura) {
      b.leitura = l;
      b.tMudanca = agora;
    } else if (l != b.estavel && agora - b.tMudanca >= DEBOUNCE_MS) {
      b.estavel = l;
      if (l) { pressionou[i] = true; b.tPress = agora; }
      else   { soltou[i] = true; }
    }
  }
}

int primeiroPressionado() {
  // Se for Observador, o único controle que vale é o que vem do Jogador via rede
  if (PAPEL_ATUAL == 1 || PAPEL_ATUAL == 3) {
    int b = botaoRemoto;
    botaoRemoto = -1;
    return b;
  }
  // Se for Completo ou Jogador, lê o botão físico localmente
  for (uint8_t i = 0; i < 4; i++) if (pressionou[i]) return i;
  return -1;
}

bool algumSeguradoPor(uint32_t ms) {
  uint32_t agora = millis();
  for (uint8_t i = 0; i < 4; i++)
    if (botoes[i].estavel && agora - botoes[i].tPress >= ms) return true;
  return false;
}

// Na inicialização, verifica se algum botão parece flutuando (sem resistor)
// ou preso. Amostra cada pino 200 vezes; o esperado é 0 leituras "apertado".
bool diagnosticarBotoes() {
  int ativos[4] = { 0, 0, 0, 0 };
  for (int k = 0; k < 200; k++) {
    for (uint8_t i = 0; i < 4; i++) if (lerPino(i)) ativos[i]++;
    delayMicroseconds(500);
  }
  bool ok = true;
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Teste dos botoes", 4, 4, 2);
  for (uint8_t i = 0; i < 4; i++) {
    char linha[32];
    const char* status;
    uint16_t cor;
    if (ativos[i] == 0)        { status = "ok";        cor = TFT_GREEN; }
    else if (ativos[i] >= 195) { status = "APERTADO?"; cor = TFT_ORANGE; ok = false; }
    else                       { status = "FLUTUANDO"; cor = TFT_RED;    ok = false; }
    snprintf(linha, sizeof(linha), "G%-2d %-9s %s", PINOS[i], NOME_POS[i], status);
    tft.setTextColor(cor, TFT_BLACK);
    tft.drawString(linha, 4, 28 + i * 18, 2);
    Serial.printf("[BOTAO] GPIO %d (%s): %d/200 leituras ativas -> %s\n",
                  PINOS[i], NOME_POS[i], ativos[i], status);
  }
  if (!ok) {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Confira a ligacao", 4, 110, 2);
    tft.drawString("GPIO <-> botao <-> G", 4, 128, 2);
    Serial.println("[BOTAO] AVISO: confira a ligacao (botao entre o GPIO e o GND; nao usar GPIO 34-39)");
    delay(4000);
  } else {
    delay(800);
  }
  return ok;
}

// ============================================================================
//  DISPLAY
// ============================================================================
void configurarCores() {
  COR_ACESA[0] = TFT_GREEN;   COR_APAGADA[0] = tft.color565(0, 55, 0);
  COR_ACESA[1] = TFT_RED;     COR_APAGADA[1] = tft.color565(70, 0, 0);
  COR_ACESA[2] = TFT_YELLOW;  COR_APAGADA[2] = tft.color565(65, 55, 0);
  COR_ACESA[3] = TFT_BLUE;    COR_APAGADA[3] = tft.color565(0, 0, 85);
  COR_OCULTA   = tft.color565(95, 95, 95);   // sinal oculto: cinza com "?"
}

void calcularLayout() {
  LARG = tft.width();
  ALT  = tft.height();
  LADO = (LARG - 3 * GAP) / 2;
  Y_MSG    = Y_GRADE + 2 * LADO + GAP + 6;
  Y_BARRA  = Y_MSG + 46;
  Y_RODAPE = ALT - 16;
}

void desenharQuadrado(uint8_t i, bool aceso) {
  int x = GAP + (i % 2) * (LADO + GAP);
  int y = Y_GRADE + (i / 2) * (LADO + GAP);
  tft.fillRoundRect(x, y, LADO, LADO, 8, aceso ? COR_ACESA[i] : COR_APAGADA[i]);
  if (aceso) {
    tft.drawRoundRect(x, y, LADO, LADO, 8, TFT_WHITE);
    tft.drawRoundRect(x + 1, y + 1, LADO - 2, LADO - 2, 7, TFT_WHITE);
  }
}

void desenharGrade(bool acesos) {
  for (uint8_t i = 0; i < 4; i++) desenharQuadrado(i, acesos);
}

void desenharGradeCor(uint16_t cor) {
  for (uint8_t i = 0; i < 4; i++) {
    int x = GAP + (i % 2) * (LADO + GAP);
    int y = Y_GRADE + (i / 2) * (LADO + GAP);
    tft.fillRoundRect(x, y, LADO, LADO, 8, cor);
  }
}

// Sinal oculto: os 4 quadrados ficam cinza com "?" (não revela a cor)
void desenharGradeOculta() {
  desenharGradeCor(COR_OCULTA);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, COR_OCULTA);
  for (uint8_t i = 0; i < 4; i++) {
    int x = GAP + (i % 2) * (LADO + GAP) + LADO / 2;
    int y = Y_GRADE + (i / 2) * (LADO + GAP) + LADO / 2;
    tft.drawString("?", x, y, 4);
  }
}

// Linha 1 em fonte grande (se couber), linha 2 em fonte pequena
void mostrarMensagem(const char* l1, const char* l2, uint16_t cor) {
  tft.fillRect(0, Y_MSG, LARG, 44, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(cor, TFT_BLACK);
  int fonte = (tft.textWidth(l1, 4) <= LARG - 4) ? 4 : 2;
  tft.drawString(l1, LARG / 2, Y_MSG + 12, fonte);
  if (l2 && l2[0]) {
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(l2, LARG / 2, Y_MSG + 35, 2);
  }
}

void desenharBarra(float fracao, uint16_t cor) {
  if (fracao < 0) fracao = 0;
  if (fracao > 1) fracao = 1;
  int w = (int)((LARG - 2 * GAP) * fracao);
  if (w == larguraBarra) return;
  larguraBarra = w;
  tft.fillRect(GAP, Y_BARRA, LARG - 2 * GAP, 5, TFT_DARKGREY);
  if (w > 0) tft.fillRect(GAP, Y_BARRA, w, 5, cor);
}

void apagarBarra() {
  tft.fillRect(0, Y_BARRA, LARG, 5, TFT_BLACK);
  larguraBarra = -1;
}

const char* rotuloModo() {
  if (modoSolo) return "SOLO";
  if (mqtt.connected()) return "ONLINE";
  return "SEM REDE";
}

void desenharCabecalho() {
  tft.fillRect(0, 0, LARG, Y_GRADE - 2, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("SEQUENCIA", LARG / 2, 1, 2);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(modoSolo ? TFT_ORANGE : (mqtt.connected() ? TFT_GREEN : TFT_RED), TFT_BLACK);
  tft.drawString(rotuloModo(), 3, 20, 1);

  if (!modoSolo && tempoRestante >= 0) {
    char t[24];
    snprintf(t, sizeof(t), "%02d:%02d", tempoRestante / 60, tempoRestante % 60);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(tempoRestante <= 30 ? TFT_RED : TFT_WHITE, TFT_BLACK);
    tft.drawString(t, LARG - 3, 20, 1);
  }
  cabecalhoSujo = false;
}

void desenharRodape() {
  tft.fillRect(0, Y_RODAPE, LARG, ALT - Y_RODAPE, TFT_BLACK);
  char s[16];
  snprintf(s, sizeof(s), "SN: %s", serialBomba);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(s, LARG / 2, Y_RODAPE, 2);
}

// Tela do jogo (retrato): cabeçalho, grade 2x2, área de mensagem e rodapé
void desenharTelaCompleta() {
  telaCompartilhada = false;
  tft.setRotation(ROTACAO_DISPLAY);     // sai da paisagem das telas compartilhadas
  larguraBarra = -1;
  tft.fillScreen(TFT_BLACK);
  desenharCabecalho();
  desenharGrade(false);
  desenharRodape();
}

// ============================================================================
//  FUNÇÕES COMPARTILHADAS — copiadas sem alteração de exemplos/exemplo_modulo.ino
//  (https://github.com/GroosL/KTNE-ESP32). Não edite: se o exemplo mudar,
//  copie de novo. As coordenadas são de paisagem (240x135): só são chamadas
//  com o display em ROTACAO_COMPARTILHADA (ver ativarTelaCompartilhada()).
// ============================================================================
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
// ======================== fim das funções compartilhadas ====================

// ============================================================================
//  INTEGRAÇÃO DAS TELAS COMPARTILHADAS
// ============================================================================
// Passa para as telas compartilhadas (gira o display para paisagem). O
// desenho acontece no loop(), em atualizarTelaCompartilhada(), como no exemplo.
void ativarTelaCompartilhada() {
  if (!telaCompartilhada) {
    telaCompartilhada = true;
    tft.setRotation(ROTACAO_COMPARTILHADA);
  }
  precisaDesenharTela = true;
}

// Aviso deste módulo (não existe no exemplo): sem broker os dados da tela
// param de atualizar. Fica num espaço livre de cada tela compartilhada.
void desenharAvisoRede() {
  if (mqtt.connected()) return;
  tft.setTextColor(TFT_RED, TFT_BLACK);
  if (estadoJogo == RUNNING) {
    tft.drawCentreString("SEM REDE - reconectando", 120, 84, 2);   // entre o tempo e os erros
  } else if (estadoJogo == LOBBY) {
    tft.fillRect(0, 95, 240, 35, TFT_BLACK);                       // no lugar da lista
    tft.drawCentreString("SEM REDE - reconectando", 120, 102, 2);
  }
}

// O mesmo que o loop() do exemplo faz quando não é a vez do módulo: mostra
// a dica recebida por TEMPO_DICA_MS e, fora isso, a tela do estado da partida.
// Uma dica que chega durante a nossa jogada espera até voltarmos para cá.
void atualizarTelaCompartilhada(uint32_t agora) {
  if (!telaCompartilhada) return;

  if (temDicaNova) {
    temDicaNova = false;
    inicioDicaMs = agora;
    drawDica();
  }

  if (temDicaAtiva && (agora - inicioDicaMs >= TEMPO_DICA_MS)) {
    temDicaAtiva = false;
    precisaDesenharTela = true;
  }

  if (temDicaAtiva) return;
  if (precisaDesenharTela) {
    precisaDesenharTela = false;
    precisaAtualizarDados = false;
    if (estadoJogo == LOBBY)        drawLobby();
    else if (estadoJogo == RUNNING) { drawRunning(); updateRunningData(); }
    else if (estadoJogo == VICTORY) drawFinish(true);
    else if (estadoJogo == DEFEAT)  drawFinish(false);
    desenharAvisoRede();
  } else if (precisaAtualizarDados) {
    precisaAtualizarDados = false;
    if (estadoJogo == LOBBY)        { updateLobbyModules(); desenharAvisoRede(); }
    else if (estadoJogo == RUNNING) updateRunningData();
  }
}

// ============================================================================
//  MQTT — envio
// ============================================================================
// Publica SOLVED/ERROR ou, sem broker, guarda para enviar na reconexão.
// No co-op só o aparelho que fala com a Central publica.
void publicarEvento(const char* evento) {
  if (modoSolo || !falaComCentral()) return;
  Serial.printf("[MQTT] -> %s: %s\n", TOPICO_EVENTO, evento);
  if (mqtt.connected() && mqtt.publish(TOPICO_EVENTO, evento)) return;
  if (strcmp(evento, "SOLVED") == 0) solvedPendente = true;
  else if (errosPendentes < 255) errosPendentes++;
}

// --- registrarSucesso / registrarErro / enviarDica*: funções do exemplo ---
// Mesmas regras do exemplo (só na partida, na nossa vez, antes de resolver).
// Adaptações: o cliente aqui se chama 'mqtt', minhaVez é uma função, e os
// eventos passam por publicarEvento (co-op + fila de reenvio).
void registrarSucesso() {
  if (estadoJogo != RUNNING || !minhaVez() || jaResolvido) return;
  jaResolvido = true;
  publicarEvento("SOLVED");
}

void registrarErro() {
  if (estadoJogo != RUNNING || !minhaVez() || jaResolvido) return;
  publicarEvento("ERROR");
}

void enviarDicaDireta(int destino, const char* conteudo) {
  if (estadoJogo != RUNNING) return;
  char topico[36];
  snprintf(topico, sizeof(topico), "jogo/modulo/%d/dica/" MODULO_ID, destino);
  mqtt.publish(topico, conteudo);
}

void enviarDicaAleatoria(const char* conteudo) {
  if (estadoJogo != RUNNING) return;
  mqtt.publish("jogo/modulo/" MODULO_ID "/dica", conteudo);
}
// --- fim das funções do exemplo ---

// Manda textoDica pela função compartilhada escolhida em DICA_DESTINO
void enviarDicaOculta() {
#if DICA_DESTINO == 0
  enviarDicaAleatoria(textoDica);        // a Central sorteia um módulo em espera
#else
  enviarDicaDireta(DICA_DESTINO, textoDica);
#endif
}

// Dica do sinal oculto. Curta de propósito: drawDica() dos outros módulos
// mostra o conteúdo numa linha só, em fonte grande, abaixo de "De: Grupo 3".
void publicarDicaOculta() {
#if USAR_SINAL_OCULTO
  dicaEnviada = true;
  if (modoSolo || !falaComCentral() || !minhaVez()) return;
  snprintf(textoDica, sizeof(textoDica), "%do = %s", POS_OCULTA + 1, NOME_COR[sequencia[POS_OCULTA]]);
  Serial.printf("[DICA] -> Central: %s\n", textoDica);
  if (mqtt.connected()) enviarDicaOculta();
  else dicaPendente = true;
#endif
}

// Reenvia o que falhou enquanto o broker estava fora. Só sai se ainda for a
// vez deste módulo; uma nova partida (ou sequência) descarta o que sobrou.
void enviarPendentes() {
  if (!minhaVez()) return;
  while (errosPendentes > 0 && mqtt.publish(TOPICO_EVENTO, "ERROR")) errosPendentes--;
  if (solvedPendente && mqtt.publish(TOPICO_EVENTO, "SOLVED")) solvedPendente = false;
  if (dicaPendente) { enviarDicaOculta(); dicaPendente = false; }
}

// ============================================================================
//  MQTT — recebimento
// ============================================================================
// Antes do START: tela compartilhada drawLobby()
void entrarAguardandoCentral() {
  mudarEstado(AGUARDANDO_CENTRAL);
  ativarTelaCompartilhada();
}

void entrarModoSolo() {
  modoSolo = true;
  aguardandoDeteccao = false;
  mudarEstado(AGUARDANDO_SOLO);
  desenharTelaCompleta();
  mostrarMensagem("MODO SOLO", "Aperte um botao", TFT_ORANGE);
}

// VICTORY / DEFEAT: tela compartilhada drawFinish()
void entrarFimDeJogo(bool vitoria) {
  mudarEstado(vitoria ? VITORIA : DERROTA);
  ativarTelaCompartilhada();
}

// Partida rodando e não é a nossa vez: tela compartilhada drawRunning()
void entrarAguardandoVez() {
  if (estado != AGUARDANDO_VEZ) mudarEstado(AGUARDANDO_VEZ);
  ativarTelaCompartilhada();
}

// Descarta a dica recebida (como o exemplo faz no START, RESET, fim de jogo
// e quando chega a vez do módulo)
void limparDica() {
  temDicaNova = false;
  temDicaAtiva = false;
}

// Zera o que pertence a uma partida da Central (no START novo e no RESET)
void limparPartida() {
  jaResolvido = false;
  limparDica();
  errosPendentes = 0;
  solvedPendente = false;
  dicaPendente = false;
}

// Regra de turnos do RFC: o desafio só roda quando jogo/modulo/ativo é este
// módulo. Chamada a cada START e a cada mensagem de módulo ativo.
void avaliarVez() {
  if (modoSolo || !partidaEmAndamento() || jaResolvido) return;
  if (minhaVez()) {
    if (estado == AGUARDANDO_VEZ) {
      Serial.println("[JOGO] Vez deste modulo: desafio liberado");
      novaPartida();
    }
  } else if (emJogada()) {
    Serial.println("[JOGO] A vez passou para outro modulo: desafio bloqueado");
    entrarAguardandoVez();
  }
}

void tratarEstadoCentral(const char* msg) {
  bool eraSolo = modoSolo;
  // O START fica retido no broker e chega de novo a cada reconexão. Se o
  // serial não mudou, é a mesma partida: o desafio não deve recomeçar.
  bool mesmaPartida = (strcmp(ultimoEstadoCentral, "START") == 0) &&
                      (strcmp(serialBomba, serialPartida) == 0);
  strncpy(ultimoEstadoCentral, msg, sizeof(ultimoEstadoCentral) - 1);
  ultimoEstadoCentral[sizeof(ultimoEstadoCentral) - 1] = '\0';

  // Espelha jogo/estado em estadoJogo, usado pelas telas compartilhadas
  if      (strcmp(msg, "START") == 0)   estadoJogo = RUNNING;
  else if (strcmp(msg, "RESET") == 0)   estadoJogo = LOBBY;
  else if (strcmp(msg, "VICTORY") == 0) estadoJogo = VICTORY;
  else if (strcmp(msg, "DEFEAT") == 0)  estadoJogo = DEFEAT;

  centralDetectada = true;
  aguardandoDeteccao = false;

  // No modo solo, só um START (partida ativa) traz o módulo de volta à rede
  if (eraSolo && strcmp(msg, "START") != 0) {
    Serial.printf("[JOGO] Modo solo: ignorando '%s'\n", msg);
    return;
  }
  modoSolo = false;

  if (strcmp(msg, "START") == 0) {
    if (mesmaPartida && !eraSolo && estado != AGUARDANDO_CENTRAL) {
      Serial.println("[JOGO] START repetido (retain) da mesma partida: desafio continua");
    } else {
      Serial.printf("[JOGO] Nova partida, SN %s\n", serialBomba);
      strncpy(serialPartida, serialBomba, sizeof(serialPartida) - 1);
      serialPartida[sizeof(serialPartida) - 1] = '\0';
      moduloAtivo = 0;     // a Central publica o módulo da vez logo após o START
      limparPartida();
      entrarAguardandoVez();
    }
    // Quem libera o desafio é o jogo/modulo/ativo, que sempre chega depois
    // do START (ao vivo e nos retidos). Não decide aqui com um valor antigo.
  } else if (strcmp(msg, "RESET") == 0) {
    serialPartida[0] = '\0';
    moduloAtivo = 0;
    tempoRestante = -1;
    erros = 0;
    acertos = 0;
    limparPartida();
    entrarAguardandoCentral();
  } else if (strcmp(msg, "VICTORY") == 0 || strcmp(msg, "DEFEAT") == 0) {
    limparDica();
    entrarFimDeJogo(strcmp(msg, "VICTORY") == 0);
  } else {
    Serial.printf("[JOGO] Estado desconhecido ignorado: '%s'\n", msg);
  }
  cabecalhoSujo = true;
}

// jogo/modulo/ativo: qual grupo está liberado para jogar agora
void tratarModuloAtivo(const char* msg) {
  int novo = atoi(msg);
  if (novo != moduloAtivo) {
    Serial.printf("[JOGO] Modulo ativo: G%d%s\n", novo, novo == MODULO_NUM ? " (nossa vez)" : "");
    moduloAtivo = novo;
    // Como no exemplo: quando chega a nossa vez, a dica na tela sai
    if (novo == MODULO_NUM) limparDica();
  }
  precisaAtualizarDados = true;  // "ATUAL: Gx" da tela compartilhada
  if (!modoSolo) avaliarVez();   // também no valor retido que chega ao reconectar
}

// Dicas recebidas de outros módulos (em geral sorteadas pela Central entre
// os módulos em espera). Como no exemplo: drawDica() por TEMPO_DICA_MS,
// para alguém ler e falar a quem está jogando. O desenho fica para o loop().
void processarDica(int origem, const char* msg) {
  grupoOrigemDica = origem;
  strncpy(conteudoDica, msg, sizeof(conteudoDica) - 1);
  conteudoDica[sizeof(conteudoDica) - 1] = '\0';
  Serial.printf("[DICA] Recebida do G%d: '%s'\n", origem, conteudoDica);
  temDicaNova = true;
  temDicaAtiva = true;
}

void callbackMqtt(char* topic, byte* payload, unsigned int length) {
  char msg[65];
  unsigned int n = length < sizeof(msg) - 1 ? length : sizeof(msg) - 1;
  memcpy(msg, payload, n);
  msg[n] = '\0';
  // Remove espaços/quebras de linha no fim (ex.: publicado pelo terminal)
  while (n > 0 && (msg[n - 1] == ' ' || msg[n - 1] == '\r' || msg[n - 1] == '\n')) msg[--n] = '\0';

  Serial.printf("[MQTT] <- %s: %s\n", topic, msg);

  // Os tratadores podem publicar, o que reaproveita o buffer de 'topic' da
  // PubSubClient: por isso 'topic' só é lido nas condições abaixo.
  int origem = 0, pos = 0;
  if (strcmp(topic, TOPICO_ESTADO) == 0) {
    tratarEstadoCentral(msg);
  } else if (strcmp(topic, TOPICO_ATIVO) == 0) {
    tratarModuloAtivo(msg);
  } else if (strcmp(topic, TOPICO_SERIAL) == 0) {
    strncpy(serialBomba, msg, sizeof(serialBomba) - 1);
    serialBomba[sizeof(serialBomba) - 1] = '\0';
    precisaAtualizarDados = true;
    if (!telaCompartilhada && estado != CONECTANDO) desenharRodape();   // nosso rodapé
  } else if (strcmp(topic, TOPICO_TEMPO) == 0) {
    int t = atoi(msg);
    if (t != tempoRestante) {
      tempoRestante = t;
      precisaAtualizarDados = true;                  // relógio da tela compartilhada
      if (!telaCompartilhada) cabecalhoSujo = true;  // relógio pequeno do nosso jogo
    }
  } else if (strcmp(topic, TOPICO_ERROS) == 0) {
    erros = atoi(msg);
    precisaAtualizarDados = true;
  } else if (strcmp(topic, TOPICO_ACERTOS) == 0) {
    acertos = atoi(msg);
    precisaAtualizarDados = true;
  } else if (strcmp(topic, TOPICO_TOTAL) == 0) {
    totalModulos = atoi(msg);
    precisaAtualizarDados = true;
  } else if (strcmp(topic, TOPICO_MODULOS) == 0) {
    // Mesmo tratamento do exemplo: "2 3 4" -> "ONLINE: G2 G3 G4"
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
  } else if (sscanf(topic, FORMATO_DICA_RECEBIDA, &origem, &pos) == 1 && topic[pos] == '\0') {
    processarDica(origem, msg);
  } else if (strcmp(topic, TOPICO_SYNC_SEQ) == 0) {
    // Se for o Jogador, recebe a sequência mestre gerada pelo Observador
    if (PAPEL_ATUAL == 2 && strlen(msg) >= TAMANHO_MAXIMO) {
      for (int i = 0; i < TAMANHO_MAXIMO; i++) sequencia[i] = (msg[i] - '0') & 3;
      tamanhoAtual = TAMANHO_INICIAL;
      iniciarRodada();
    }
  } else if (strcmp(topic, TOPICO_SYNC_BOTAO) == 0) {
    // Se for o Observador, registra que o Jogador remoto apertou um botão
    if (PAPEL_ATUAL == 1 || PAPEL_ATUAL == 3) {
      botaoRemoto = atoi(msg);
    }
  }  
}

bool conectarMqtt() {
  if (WiFi.status() != WL_CONNECTED) return false;
  Serial.printf("[MQTT] Conectando a %s:%d como %s...\n", MQTT_BROKER, MQTT_PORT, clientId);
  // Last Will: se este ESP cair, o broker publica OFFLINE (retido) e a
  // Central tira o módulo da fila. O Jogador do co-op conecta sem LWT.
  bool ok = falaComCentral()
              ? mqtt.connect(clientId, TOPICO_STATUS, 0, true, "OFFLINE")
              : mqtt.connect(clientId);
  if (!ok) {
    Serial.printf("[MQTT] Falhou (rc=%d)\n", mqtt.state());
    return false;
  }
  // Anuncia presença: a Central só coloca na fila quem está ONLINE
  if (falaComCentral()) mqtt.publish(TOPICO_STATUS, "ONLINE", true);

  // A ordem importa: os valores retidos chegam na ordem das inscrições, e o
  // serial precisa vir antes do START (ver tratarEstadoCentral), que precisa
  // vir antes do módulo ativo.
  mqtt.subscribe(TOPICO_SERIAL);
  mqtt.subscribe(TOPICO_ESTADO);
  mqtt.subscribe(TOPICO_ATIVO);
  mqtt.subscribe(TOPICO_TEMPO);
  mqtt.subscribe(TOPICO_ERROS);       // estes quatro alimentam as telas compartilhadas
  mqtt.subscribe(TOPICO_ACERTOS);
  mqtt.subscribe(TOPICO_TOTAL);
  mqtt.subscribe(TOPICO_MODULOS);
  mqtt.subscribe(TOPICO_DICA_RECEBIDA);
  if (PAPEL_ATUAL == 2) mqtt.subscribe(TOPICO_SYNC_SEQ);
  if (PAPEL_ATUAL == 1 || PAPEL_ATUAL == 3) mqtt.subscribe(TOPICO_SYNC_BOTAO);
  Serial.println("[MQTT] Conectado e inscrito");
  tConexaoMqtt = millis();
  cabecalhoSujo = true;
  return true;
}

// Mantém a conexão sem travar o jogo: nunca tenta reconectar durante uma
// jogada, pois a tentativa pode bloquear por alguns segundos. (No jogo em
// rede isso não ocorre: a queda pausa o desafio; só o modo solo joga offline.)
void gerenciarRede(uint32_t agora) {
  // Reage quando a conexão com o broker cai ou volta
  static bool conectadoAntes = false;
  bool conectado = mqtt.connected();
  if (conectado != conectadoAntes) {
    conectadoAntes = conectado;
    cabecalhoSujo = true;                     // rótulo ONLINE / SEM REDE
    if (!conectado && !modoSolo && emJogada()) {
      // Sem broker a Central não receberia SOLVED/ERROR: pausa o desafio.
      // Ao reconectar, o jogo/modulo/ativo retido diz se ainda é a nossa vez.
      Serial.println("[MQTT] Conexao perdida durante a vez: desafio pausado");
      entrarAguardandoVez();
    } else if (telaCompartilhada) {
      precisaDesenharTela = true;             // põe/tira o aviso "SEM REDE"
    }
  }

  if (conectado) {
    mqtt.loop();
    if (!modoSolo) enviarPendentes();
  } else if (WiFi.status() == WL_CONNECTED && !emJogada() &&
             agora - tUltimaTentativa >= INTERVALO_RECONEXAO_MS) {
    tUltimaTentativa = agora;
    conectarMqtt();
  }

  // Conectou mas nenhuma mensagem de estado chegou: não há Central
  if (aguardandoDeteccao && estado == AGUARDANDO_CENTRAL &&
      agora - tConexaoMqtt >= TIMEOUT_CENTRAL_MS) {
    Serial.println("[JOGO] Nenhuma Central detectada -> modo solo");
    entrarModoSolo();
  }
}

// ============================================================================
//  LÓGICA DO JOGO
// ============================================================================
void mudarEstado(Estado novo) {
  if (novo != estado) Serial.printf("[ESTADO] %s -> %s\n", NOME_ESTADO[estado], NOME_ESTADO[novo]);
  estado = novo;
  tEstado = millis();
}

void gerarSequencia() {
  if (PAPEL_ATUAL == 2) return; // O Jogador recebe via MQTT, não gera.

  char seqString[TAMANHO_MAXIMO + 1];
  for (uint8_t i = 0; i < TAMANHO_MAXIMO; i++) {
    sequencia[i] = esp_random() % 4;
    seqString[i] = '0' + sequencia[i]; // Converte pra string
  }
  seqString[TAMANHO_MAXIMO] = '\0';

  // O Observador (ou modo completo) compartilha a sequência com o parceiro
  if (PAPEL_ATUAL == 1 && !modoSolo) {
    mqtt.publish(TOPICO_SYNC_SEQ, seqString);
  }

  Serial.print("[JOGO] Sequencia: ");
  for (uint8_t i = 0; i < TAMANHO_MAXIMO; i++) Serial.printf("%s ", NOME_POS[sequencia[i]]);
  Serial.println();
}

uint32_t tempoAceso() {
  int t = TEMPO_ACESO_INICIAL_MS - (tamanhoAtual - TAMANHO_INICIAL) * REDUCAO_POR_RODADA_MS;
  return t < TEMPO_ACESO_MINIMO_MS ? TEMPO_ACESO_MINIMO_MS : t;
}

void novaPartida() {
  if (PAPEL_ATUAL == 2) {
    // Só avança quando receber a msg MQTT com a sequência. Muda de estado
    // para não ser chamada de novo a cada loop (ex.: no fim do ERRO).
    entrarAguardandoVez();
    return;
  }
  gerarSequencia();
  tamanhoAtual = TAMANHO_INICIAL;
  dicaEnviada = false;        // sequência nova = sinal oculto novo = dica nova
  dicaPendente = false;       // a dica antiga (se não saiu) já não vale
  desenharTelaCompleta();
  iniciarRodada();
}

void iniciarRodada() {
  if (telaCompartilhada) desenharTelaCompleta();   // vindo da tela compartilhada
  char l2[24];
  snprintf(l2, sizeof(l2), "Rodada %d de %d",
           tamanhoAtual - TAMANHO_INICIAL + 1, TAMANHO_MAXIMO - TAMANHO_INICIAL + 1);
  desenharGrade(false);
  apagarBarra();
  mostrarMensagem("OBSERVE", l2, TFT_CYAN);
  mudarEstado(PREPARANDO);
  // Primeira rodada com o sinal oculto: a cor dele vai como dica
  if (sinalOculto(POS_OCULTA) && !dicaEnviada) publicarDicaOculta();
}

// Acende/apaga o sinal 'passo' da sequência. O sinal oculto não mostra a
// cor: os 4 quadrados ficam cinza com "?" e a tela avisa que é preciso dica.
void mostrarSinal(uint8_t passo, bool aceso) {
  if (PAPEL_ATUAL == 2) return;              // o Jogador não vê a sequência
  if (!sinalOculto(passo)) {
    desenharQuadrado(sequencia[passo], aceso);
  } else if (aceso) {
    desenharGradeOculta();
    mostrarMensagem("OCULTO!", "Peca a dica!", TFT_YELLOW);
  } else {
    desenharGrade(false);
    mostrarMensagem("OBSERVE", "", TFT_CYAN);
  }
}

// Tempo para o próximo toque: maior no sinal oculto (pedir a dica)
uint32_t limiteToque() {
  return sinalOculto(indiceEntrada) ? TEMPO_LIMITE_OCULTO_MS : TEMPO_LIMITE_TOQUE_MS;
}

// Mensagem durante a repetição: progresso, ou o aviso do sinal oculto
void mostrarProgressoEntrada() {
  if (sinalOculto(indiceEntrada)) {
    mostrarMensagem("OCULTO!", "Pergunte a dica", TFT_YELLOW);
    return;
  }
  char l2[24] = "";
  if (PAPEL_ATUAL != 2) snprintf(l2, sizeof(l2), "%d de %d", indiceEntrada, tamanhoAtual);
  mostrarMensagem("SUA VEZ", l2, TFT_WHITE);
}

void iniciarEntrada() {
  indiceEntrada = 0;
  tUltimoToque = millis();
  mostrarProgressoEntrada();
  mudarEstado(ENTRADA);
}

void falhar(const char* motivo) {
  Serial.printf("[JOGO] Falha: %s\n", motivo);
  registrarErro();
  apagarBarra();
  desenharGradeCor(TFT_RED);
  mostrarMensagem(motivo, "Modulo bloqueado", TFT_RED);
  mudarEstado(ERRO);
}

void resolver() {
  Serial.println("[JOGO] Modulo RESOLVIDO");
  mudarEstado(RESOLVIDO);
  registrarSucesso();          // marca jaResolvido: não volta a jogar nesta partida
  apagarBarra();
  desenharGrade(true);
  mostrarMensagem("RESOLVIDO", modoSolo ? "Aperte p/ jogar" : "Modulo desarmado", TFT_GREEN);
}

void atualizarJogo(uint32_t agora) {
  uint32_t decorrido = agora - tEstado;

  switch (estado) {
    case CONECTANDO:
      break;

    // Telas de espera da Central: segurar um botão força o modo solo
    case AGUARDANDO_CENTRAL:
    case VITORIA:
    case DERROTA:
      if (algumSeguradoPor(TEMPO_SEGURAR_SOLO_MS)) {
        Serial.println("[JOGO] Botao segurado -> modo solo");
        entrarModoSolo();
      }
      break;

    // Esperando a vez: sem rede, segurar um botão também permite jogar sozinho
    case AGUARDANDO_VEZ:
      if (!mqtt.connected() && algumSeguradoPor(TEMPO_SEGURAR_SOLO_MS)) {
        Serial.println("[JOGO] Botao segurado sem rede -> modo solo");
        entrarModoSolo();
      }
      break;

    case AGUARDANDO_SOLO:
      if (primeiroPressionado() >= 0) novaPartida();
      break;

    case PREPARANDO:
      if (decorrido >= TEMPO_PREPARO_MS) {
        passoMostra = 0;
        sinalAceso = true;
        if (PAPEL_ATUAL != 2) {
          mostrarMensagem("OBSERVE", "", TFT_CYAN);
          mostrarSinal(0, true);
        } else {   // o Jogador não vê a sequência
          mostrarMensagem("MEMORIZE", "Ouca o parceiro!", TFT_ORANGE);
        }
        mudarEstado(MOSTRANDO);
      }
      break;

    case MOSTRANDO:   // apertos neste estado são ignorados
      if (sinalAceso && decorrido >= tempoAceso()) {
        mostrarSinal(passoMostra, false);
        sinalAceso = false;
        tEstado = agora;
      } else if (!sinalAceso && decorrido >= TEMPO_APAGADO_MS) {
        passoMostra++;
        if (passoMostra >= tamanhoAtual) {
          iniciarEntrada();
        } else {
          if (PAPEL_ATUAL != 2) {
            mostrarSinal(passoMostra, true);
          } else {
            mostrarMensagem("MEMORIZE", "Ouca o parceiro!", TFT_ORANGE);
          }
          sinalAceso = true;
          tEstado = agora;
        }
      }
      break;

    case ENTRADA: {
      // 1. LER FÍSICO E TRANSMITIR (Apenas o Jogador e o Loopback publicam)
      if (PAPEL_ATUAL == 2 || PAPEL_ATUAL == 3) {
         for (uint8_t i = 0; i < 4; i++) {
            if (pressionou[i]) {
               if (mqtt.connected()) {
                  char msgB[4];
                  snprintf(msgB, sizeof(msgB), "%d", i);
                  mqtt.publish(TOPICO_SYNC_BOTAO, msgB);
               } else if (PAPEL_ATUAL == 3) {
                  botaoRemoto = i; // Offline loopback bypass
               }
            }
         }
      }

      // 2. CAPTURAR A JOGADA UMA ÚNICA VEZ
      int b = primeiroPressionado(); 

      // 3. FEEDBACK VISUAL (Apaga automático no Observador/Loopback)
      if (PAPEL_ATUAL == 1 || PAPEL_ATUAL == 3) {
         static uint32_t tApagaRemoto = 0;
         if (b >= 0) tApagaRemoto = agora; // Usa a variável capturada!
         if (tApagaRemoto > 0 && agora - tApagaRemoto > 250) {
            desenharGrade(false);
            tApagaRemoto = 0;
         }
      } else {
         for (uint8_t i = 0; i < 4; i++) if (soltou[i]) desenharQuadrado(i, false);
      }

      // 4. PROCESSAR A JOGADA
      if (b >= 0) {
        if (PAPEL_ATUAL != 2) desenharQuadrado(b, true); 
        
        if (b == sequencia[indiceEntrada]) {
          indiceEntrada++;
          tUltimoToque = agora;
          if (indiceEntrada >= tamanhoAtual) {
            if (tamanhoAtual >= TAMANHO_MAXIMO) {
              resolver();
            } else {
              apagarBarra();
              mostrarMensagem("BOA!", "Proxima rodada", TFT_GREEN);
              mudarEstado(RODADA_OK);
            }
          } else {
            mostrarProgressoEntrada();    // inclui o aviso do sinal oculto
          }
        } else {
          Serial.printf("[JOGO] Esperado %s, apertado %s\n",
                        NOME_POS[sequencia[indiceEntrada]], NOME_POS[b]);
          falhar("ERRO!");
        }
        break;
      }

#if TEMPO_LIMITE_TOQUE_MS > 0
      uint32_t limite = limiteToque();     // maior no sinal oculto
      uint32_t semToque = agora - tUltimoToque;
      if (semToque >= limite) {
        falhar("TEMPO!");
      } else {
        float resta = 1.0f - (float)semToque / limite;
        uint16_t corBarra = resta < 0.3f ? TFT_RED
                          : (sinalOculto(indiceEntrada) ? TFT_YELLOW : TFT_GREEN);
        desenharBarra(resta, corBarra);
      }
#endif
      break;
    }

    case RODADA_OK:
      if (decorrido >= TEMPO_RODADA_OK_MS) {
        tamanhoAtual++;
        iniciarRodada();
      }
      break;

    case ERRO:
      if (decorrido >= BLOQUEIO_ERRO_MS) {
#if VOLTAR_AO_INICIO_NO_ERRO
        novaPartida();
#else
        desenharGrade(false);
        iniciarRodada();
#endif
      }
      break;

    case RESOLVIDO:
      // Online: fica desarmado até a Central mandar VICTORY/DEFEAT/RESET.
      // Depois de um instante vai para a tela compartilhada (drawRunning),
      // que mostra o tempo e as dicas de quem joga depois.
      // Solo: depois de um instante, qualquer botão começa de novo.
      if (modoSolo) {
        if (decorrido >= 1500 && primeiroPressionado() >= 0) novaPartida();
      } else if (!telaCompartilhada && decorrido >= TEMPO_RESOLVIDO_MS) {
        ativarTelaCompartilhada();
      }
      break;
  }
}

// ============================================================================
//  MODO DE TESTE DOS BOTÕES
// ============================================================================
void loopTesteBotoes() {
  atualizarBotoes();
  for (uint8_t i = 0; i < 4; i++) {
    if (pressionou[i]) {
      desenharQuadrado(i, true);
      Serial.printf("[TESTE] Apertou %s (GPIO %d)\n", NOME_POS[i], PINOS[i]);
    }
    if (soltou[i]) desenharQuadrado(i, false);
  }
}

// ============================================================================
//  SETUP E LOOP
// ============================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== KTNE Grupo 3 - Sequencia de Sinais ===");

  pinMode(PINO_BACKLIGHT, OUTPUT);
  digitalWrite(PINO_BACKLIGHT, HIGH);
  tft.init();
  tft.setRotation(ROTACAO_DISPLAY);
  tft.fillScreen(TFT_BLACK);
  configurarCores();
  calcularLayout();

  configurarBotoes();
  diagnosticarBotoes();

#if MODO_TESTE_BOTOES
  modoSolo = true;
  desenharTelaCompleta();
  mostrarMensagem("TESTE", "Aperte os botoes", TFT_YELLOW);
  return;
#endif

  // ID único por placa evita derrubar outro cliente com o mesmo nome
  snprintf(clientId, sizeof(clientId), "Modulo_" MODULO_ID "_%04X",
           (unsigned)((ESP.getEfuseMac() >> 32) & 0xFFFF));
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(callbackMqtt);
  mqtt.setSocketTimeout(2);

  // Tela de conexão
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Conectando", LARG / 2, ALT / 2 - 20, 4);
  tft.drawString(WIFI_SSID, LARG / 2, ALT / 2 + 10, 2);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < TIMEOUT_WIFI_MS) delay(200);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WIFI] Conectado, IP %s\n", WiFi.localIP().toString().c_str());
    tUltimaTentativa = millis();
    if (conectarMqtt()) {
      aguardandoDeteccao = true;   // espera jogo/estado por TIMEOUT_CENTRAL_MS
      entrarAguardandoCentral();
      return;
    }
  } else {
    Serial.println("[WIFI] Sem conexao (seguira tentando em segundo plano)");
  }
  entrarModoSolo();
}

void loop() {
#if MODO_TESTE_BOTOES
  loopTesteBotoes();
  return;
#endif
  uint32_t agora = millis();
  gerenciarRede(agora);
  atualizarBotoes();
  atualizarJogo(agora);
  atualizarTelaCompartilhada(agora);
  if (cabecalhoSujo && !telaCompartilhada) desenharCabecalho();   // só na tela do jogo
}
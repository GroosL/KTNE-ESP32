# RFC 001: Protocolo de Comunicação MQTT — Jogo IoT Cooperativo

* **Status:** Proposta de Padronização Oficial
* **Autor:** Grupo 1 (Central de Controle)
* **Público-alvo:** Grupos 2 a 7
* **Formato de Dados:** Texto simples (ASCII / Plain Text), sem JSON

---

## 1. Filosofia de Arquitetura

O sistema opera de forma **desacoplada e orientada a eventos**:

* **A Central é um orquestrador cego:** A Central de Controle **não** valida códigos, senhas, medições de sensores ou regras internas de nenhum minijogo. Cada grupo tem total autonomia para gerenciar e validar seus desafios internamente.
* **Sem dependência de bibliotecas pesadas:** Todas as mensagens trafegam como strings literais puras, dispensando o uso de parsers JSON (`ArduinoJson`) nos microcontroladores.
* **Tolerância a falhas de rede:** Mensagens cruciais usam flag *Retain*, garantindo que um módulo que reinicie durante a partida receba imediatamente o estado atual e o identificador do jogo.

---

## 2. Especificação Geral de Tópicos

| Tópico | Direção | Retain | Frequência | Descrição / Payloads |
| --- | --- | --- | --- | --- |
| `jogo/estado` | Central $\to$ Todos | **Sim** | Sob transição | `START`, `RESET`, `VICTORY`, `DEFEAT` |
| `jogo/serial` | Central $\to$ Todos | **Sim** | No `START` e `RESET` | Identificador único da bomba (ex: `A7-31` ou `-----`) |
| `jogo/tempo` | Central $\to$ Todos | Não | 1 Hz (1x por seg) | Inteiro puro com segundos restantes (ex: `299`). **Opcional.** |
| `jogo/modulo/<ID>/evento` | Módulo $\to$ Central | Não | Sob evento | `SOLVED` (sucesso) ou `ERROR` (falha/strike) |
| `jogo/modulo/<ID>/dica` | Módulo $\to$ Módulo | Não | Conforme lógica | Troca direta de dados entre desafios sem passar pela Central |

---

## 3. Detalhamento dos Tópicos

### 3.1. Estado Global (`jogo/estado`)

Todos os módulos devem assinar este tópico para controlar sua máquina de estados local.

* **`START`:** Habilita sensores/atuadores, reseta variáveis internas e inicia o desafio.
* **`RESET`:** Cancela a partida e coloca o módulo em espera (tela inicial).
* **`VICTORY`:** Desarma o módulo (vitória dos jogadores). Trava interações e exibe mensagem de sucesso.
* **`DEFEAT`:** Trava o módulo em falha (explosão por tempo esgotado ou excesso de erros).

---

### 3.2. Serial da Partida (`jogo/serial`)

Identificador alfanumérico estático sorteado pela Central no início de cada partida. Atende ao requisito de exibir dados distribuídos no display e serve como base para regras dos manuais.

* **Formato:** 5 caracteres ASCII: `LL-DD` (duas letras maiúsculas, hífen, dois dígitos numéricos).
* **Exemplos:** `B4-92`, `K7-03`, `XP-88`. No reset: `-----`.
* **Como os grupos utilizam:**
1. **Visualização:** Reservar uma área no display TFT para estampar o serial (ex: cabeçalho ou rodapé: `SN: B4-92`).
2. **Regras de Desafio (Manuais em papel):**
* *Grupo 2 (Teclado):* *"Se o último dígito do serial for ímpar, adicione 2 ao primeiro dígito da senha."*
* *Grupo 4 (Distância):* *"Se o serial contiver a letra 'B', o alvo é 15 cm; caso contrário, 30 cm."*





---

### 3.3. Sincronização de Tempo (`jogo/tempo`) — *Opcional*

Publicado pela Central a cada 1 segundo enquanto a partida estiver ativa (`RUNNING`).

* **Payload:** String ASCII com o número de segundos (ex: `"300"`, `"299"`, `"59"`).
* **Uso sugerido:** Atualizar um relógio local ou acelerar o piscar de LEDs/bipes de aviso nos últimos 30 segundos.
* *Nota:* Módulos que não precisam saber o tempo exato podem simplesmente ignorar este tópico.

---

### 3.4. Notificação de Eventos (`jogo/modulo/<ID>/evento`)

Substitua `<ID>` pelo número do seu grupo (de `2` a `7`). A Central assina o padrão curinga `jogo/modulo/+/evento`.

* **`SOLVED`:** Notifica que o desafio do módulo foi vencido. A Central incrementa o contador global de módulos resolvidos (meta: 6).
* **`ERROR`:** Notifica que o jogador cometeu um erro grave na interação. A Central adiciona 1 *strike* (limite: 3 strikes antes da derrota).

> **Atenção:** Publique apenas quando houver resolução definitiva ou erro confirmado. Não envie dados contínuos de sensores neste tópico.

---

### 3.5. Pistas entre Módulos (`jogo/modulo/<ID_DESTINO>/dica`)

Comunicação direta para satisfazer o requisito de dependência entre módulos sem intermediários.

* **Exemplo:** O Grupo 3 (Sequência de luzes) foi resolvido e gera o código de liberação para o Grupo 2 (Teclado).
* O Grupo 3 publica `"8310"` no tópico `jogo/modulo/2/dica`.
* O Grupo 2 escuta esse tópico e atualiza o estado interno da trava.



---

## 4. Código de Referência para os Módulos (C++ / Arduino)

Snippet pronto para ESP32 compatível com a biblioteca `PubSubClient`:

```cpp
#include <WiFi.h>
#include <PubSubClient.h>

// Defina o número do seu grupo aqui (de "2" a "7")
#define MODULO_ID "2"

constexpr char* WIFI_SSID     = "iot2026";
constexpr char* WIFI_PASSWORD = "Senhab0@";
constexpr char* MQTT_BROKER   = "10.150.207.154";
constexpr int   MQTT_PORT     = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

// Variáveis de estado global sincronizadas pela rede
char serialBomba[8] = "-----";
int tempoRestante   = 0;
bool desafioAtivo   = false;

void callbackMqtt(char* topic, byte* payload, unsigned int length) {
  char msg[length + 1];
  memcpy(msg, payload, length);
  msg[length] = '\0';

  // 1. Estados globais da partida
  if (strcmp(topic, "jogo/estado") == 0) {
    if (strcmp(msg, "START") == 0) {
      desafioAtivo = true;
      // Iniciar leituras de sensores / habilitar botoes
    } 
    else if (strcmp(msg, "RESET") == 0) {
      desafioAtivo = false;
      // Resetar variaveis locais e voltar para tela de espera
    } 
    else if (strcmp(msg, "VICTORY") == 0 || strcmp(msg, "DEFEAT") == 0) {
      desafioAtivo = false;
      // Travar interacoes do jogador
    }
  }

  // 2. Serial da partida (recebe no START)
  else if (strcmp(topic, "jogo/serial") == 0) {
    strncpy(serialBomba, msg, sizeof(serialBomba));
    // DICA: Atualizar display do modulo com o serial:
    // tft.drawString(serialBomba, 10, 10, 2);
  }

  // 3. Sincronização opcional de tempo
  else if (strcmp(topic, "jogo/tempo") == 0) {
    tempoRestante = atoi(msg);
  }

  // 4. Pistas recebidas de outro módulo
  else if (strcmp(topic, "jogo/modulo/" MODULO_ID "/dica") == 0) {
    // Processar dado recebido (ex: msg contem "8310")
  }
}

void reconectarMQTT() {
  while (!client.connected()) {
    if (client.connect("Modulo_" MODULO_ID)) {
      // Inscricoes obrigatorias
      client.subscribe("jogo/estado");
      client.subscribe("jogo/serial");

      // Inscricoes opcionais (comente se nao for usar)
      client.subscribe("jogo/tempo");
      client.subscribe("jogo/modulo/" MODULO_ID "/dica");
    } else {
      delay(1000);
    }
  }
}

// Funcoes de notificacao para a Central
void registrarSucesso() {
  client.publish("jogo/modulo/" MODULO_ID "/evento", "SOLVED");
}

void registrarErro() {
  client.publish("jogo/modulo/" MODULO_ID "/evento", "ERROR");
}

void setup() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  client.setServer(MQTT_BROKER, MQTT_PORT);
  client.setCallback(callbackMqtt);
}

void loop() {
  if (!client.connected()) reconectarMQTT();
  client.loop();

  // Exemplo de uso no seu loop:
  // if (desafioAtivo && jogadorAcertou())  registrarSucesso();
  // if (desafioAtivo && jogadorErrou())    registrarErro();
}

```

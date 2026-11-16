# RFC 001: Protocolo de Comunicação MQTT — Jogo IoT Cooperativo

* **Status:** Especificação Oficial (Atualizada)
* **Autor:** Grupo 1 (Central de Controle)
* **Público-alvo:** Grupos 2 a 11 (até 10 módulos simultâneos)
* **Formato de Dados:** Texto simples (ASCII / Plain Text), sem JSON

## 1. Filosofia de Arquitetura

O sistema opera de forma **desacoplada, dinâmica e orientada a eventos**:

* **A Central é um orquestrador cego:** A Central de Controle **não** valida códigos, senhas, medições de sensores ou regras internas de nenhum minijogo. Cada grupo tem total autonomia para gerenciar e validar seus desafios internamente.
* **Descoberta Dinâmica e Fila Sequencial:** Os módulos anunciam sua presença ao conectar. A Central monta a fila apenas com os grupos realmente online (mesmo que haja saltos na numeração, ex: Grupos 2, 4 e 9) e libera um módulo por vez em ordem crescente.
* **Pistas Diretas e Aleatórias:** Os módulos podem enviar pistas diretamente para um grupo específico ou solicitar à Central que sorteie um destinatário aleatório entre os grupos online (incluindo os que já foram resolvidos). Em ambos os casos, o remetente é identificado no próprio tópico.
* **Tolerância a falhas de rede:** Mensagens de estado e presença usam flag *Retain* e *Last Will and Testament (LWT)*, garantindo que quedas de energia ou reconexões sejam tratadas automaticamente.

## 2. Especificação Geral de Tópicos

| Tópico | Direção | Retain | Frequência | Descrição / Payloads |
| --- | --- | --- | --- | --- |
| `jogo/modulo/<ID>/status` | Módulo $\to$ Central | **Sim** | Ao conectar / LWT | `ONLINE` ou `OFFLINE` (registra o módulo na fila) |
| `jogo/estado` | Central $\to$ Todos | **Sim** | Sob transição | `START`, `RESET`, `VICTORY`, `DEFEAT` |
| `jogo/serial` | Central $\to$ Todos | **Sim** | No `START` e `RESET` | Identificador único da bomba (ex: `A7-31` ou `-----`) |
| `jogo/modulo/ativo` | Central $\to$ Todos | **Sim** | Sob turno / transição | ID do grupo da vez (ex: `2`, `4`, `9`) ou `0` (travado) |
| `jogo/tempo` | Central $\to$ Todos | Não | 1 Hz (1x por seg) | Inteiro puro com segundos restantes (ex: `299`). **Opcional.** |
| `jogo/modulo/<ID>/evento` | Módulo $\to$ Central | Não | Sob evento | `SOLVED` (sucesso) ou `ERROR` (falha/strike) |
| `jogo/modulo/<ORIG>/dica` | Módulo $\to$ Central | Não | Conforme lógica | Payload puro da dica para a Central rotear aleatoriamente |
| `jogo/modulo/<DEST>/dica/<ORIG>` | Módulo/Central $\to$ Módulo | Não | Sob envio/roteamento | Entrega da dica ao `<DEST>` informando quem enviou (`<ORIG>`) |

## 3. Detalhamento dos Tópicos

### 3.1. Registro de Presença (`jogo/modulo/<ID>/status`)

Obrigatório para que a Central inclua o módulo na partida e no sorteio de dicas. Deve ser configurado com **Last Will and Testament (LWT)** no momento da conexão ao broker.

* **`ONLINE`:** Publicado (com `retain = true`) imediatamente após conectar ao broker.
* **`OFFLINE`:** Configurado como mensagem de LWT (`retain = true`). Se o ESP32 perder energia ou conexão, o broker avisa a Central automaticamente.

### 3.2. Estado Global (`jogo/estado`)

Todos os módulos devem assinar este tópico para controlar sua máquina de estados local.

* **`START`:** Inicia a partida e reseta variáveis internas do desafio.
* **`RESET`:** Cancela a partida e coloca o módulo em espera (tela inicial).
* **`VICTORY`:** Desarma o módulo (vitória dos jogadores). Trava interações e exibe mensagem de sucesso.
* **`DEFEAT`:** Trava o módulo em falha (explosão por tempo esgotado ou excesso de erros).

### 3.3. Controle de Turno Sequencial (`jogo/modulo/ativo`)

Os módulos são resolvidos sequencialmente. A Central publica o ID do grupo que está liberado para jogar no momento.

* **Payloads:** O número do grupo ativo (ex: `"2"`, `"4"`, `"9"`) ou `"0"` quando o jogo estiver no Lobby ou finalizado.
* **Comportamento esperado do Módulo:**
  * Se `ativo == MEU_ID`: Libera os sensores/botões para o jogador interagir.
  * Se `ativo != MEU_ID`: Mantém a interação do desafio bloqueada (aguardando a vez ou já concluído).
  * *Nota:* A Central ignora eventos `SOLVED` ou `ERROR` enviados por módulos que não sejam o ativo atual.

### 3.4. Serial da Partida (`jogo/serial`)

Identificador alfanumérico estático sorteado pela Central no início de cada partida.

* **Formato:** 5 caracteres ASCII: `LL-DD` (duas letras maiúsculas, hífen, dois dígitos numéricos).
* **Exemplos:** `B4-92`, `K7-03`, `XP-88`. No reset: `-----`.
* **Como os grupos utilizam:**
  1. **Visualização:** Reservar uma área no display TFT para estampar o serial (ex: `SN: B4-92`).
  2. **Regras de Desafio (Manuais em papel):**
     * *Grupo 2 (Teclado):* *"Se o último dígito do serial for ímpar, adicione 2 ao primeiro dígito da senha."*
     * *Grupo 4 (Distância):* *"Se o serial contiver a letra 'B', o alvo é 15 cm; caso contrário, 30 cm."*

### 3.5. Sincronização de Tempo (`jogo/tempo`) — *Opcional*

Publicado pela Central a cada 1 segundo enquanto a partida estiver ativa (`RUNNING`).

* **Payload:** String ASCII com o número de segundos (ex: `"300"`, `"299"`, `"59"`).
* **Uso sugerido:** Atualizar um relógio local ou acelerar bipes de aviso nos últimos 30 segundos.

### 3.6. Notificação de Eventos (`jogo/modulo/<ID>/evento`)

Substitua `<ID>` pelo número do seu grupo (de `2` a `11`).

* **`SOLVED`:** Notifica que o desafio do módulo foi vencido. A Central avança para o próximo módulo online da fila (ou declara `VICTORY` se for o último).
* **`ERROR`:** Notifica que o jogador cometeu um erro grave. A Central adiciona 1 *strike* (limite: 3 strikes antes de `DEFEAT`).

> **Atenção:** Publique apenas quando houver resolução definitiva ou erro confirmado, e somente quando for a vez do seu módulo.

### 3.7. Pistas e Dependências (Direta ou Aleatória)

Todo módulo deve assinar `jogo/modulo/<MEU_ID>/dica/+` para receber pistas. O último nível do tópico informa qual grupo enviou o dado, deixando o payload 100% livre para qualquer texto ou formato (ex: coordenadas de labirinto ou códigos).

Existem **duas formas** de um módulo enviar uma pista:

1. **Envio Direto (Para um grupo específico):**
   * **Publicar em:** `jogo/modulo/<ID_DESTINO>/dica/<ID_ORIGEM>`
   * **Exemplo:** Grupo 2 enviando senha `8310` especificamente para o Grupo 4 $\to$ publica `"8310"` em `jogo/modulo/4/dica/2` (vai direto pelo broker sem passar pela Central).

2. **Envio Aleatório (A Central escolhe o destinatário online):**
   * **Publicar em:** `jogo/modulo/<ID_ORIGEM>/dica`
   * **Exemplo:** Grupo 5 enviando labirinto para qualquer outro grupo disponível $\to$ publica `"3;0,0;5,5"` em `jogo/modulo/5/dica`. A Central sorteia um módulo online diferente de 5 (ex: Grupo 3) e republica em `jogo/modulo/3/dica/5`.

> **Importante para os displays:** Mesmo após o seu módulo enviar `SOLVED`, **mantenha o callback de dicas ativo** e continue exibindo dicas recebidas na tela durante toda a partida (`RUNNING`), pois outros grupos podem precisar consultar a pista no seu display.

## 4. Código de Referência para os Módulos (C++ / Arduino)

Snippet pronto para ESP32 compatível com a biblioteca `PubSubClient`:

```cpp
#include <WiFi.h>
#include <PubSubClient.h>

// Defina o número do seu grupo aqui (ex: "2" a "11")
#define MODULO_ID "2"
#define MODULO_NUM 2

constexpr const char* WIFI_SSID     = "iot2026";
constexpr const char* WIFI_PASSWORD = "Senhab0@";
constexpr const char* MQTT_BROKER   = "10.150.207.154";
constexpr int         MQTT_PORT     = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

// Variáveis de estado global sincronizadas pela rede
char serialBomba[8] = "-----";
int  tempoRestante  = 0;
bool jogoRodando    = false;
bool minhaVez       = false;
bool jaResolvido    = false;

void callbackMqtt(char* topic, byte* payload, unsigned int length) {
  char msg[length + 1];
  memcpy(msg, payload, length);
  msg[length] = '\0';

  // Higieniza eventuais quebras de linha residuais
  while (length > 0 && (msg[length - 1] == '\r' || msg[length - 1] == '\n' || msg[length - 1] == ' ')) {
    msg[--length] = '\0';
  }

  // 1. Estados globais da partida
  if (strcmp(topic, "jogo/estado") == 0) {
    if (strcmp(msg, "START") == 0) {
      jogoRodando = true;
      jaResolvido = false;
      // Resetar variaveis do desafio para nova partida
    } 
    else if (strcmp(msg, "RESET") == 0) {
      jogoRodando = false;
      minhaVez    = false;
      jaResolvido = false;
      // Voltar para tela de espera
    } 
    else if (strcmp(msg, "VICTORY") == 0 || strcmp(msg, "DEFEAT") == 0) {
      jogoRodando = false;
      minhaVez    = false;
      // Travar interacoes do jogador
    }
  }

  // 2. Controle sequencial (verifica se é o turno deste módulo)
  else if (strcmp(topic, "jogo/modulo/ativo") == 0) {
    int idAtivo = atoi(msg);
    minhaVez = (idAtivo == MODULO_NUM);
    
    if (minhaVez) {
      // Habilitar sensores/botoes do desafio!
    } else {
      // Bloquear interacao do desafio (aguardando vez ou ja concluido)
    }
  }

  // 3. Serial da partida (recebe no START)
  else if (strcmp(topic, "jogo/serial") == 0) {
    strncpy(serialBomba, msg, sizeof(serialBomba));
    // Atualizar display do modulo com o serial
  }

  // 4. Sincronização opcional de tempo
  else if (strcmp(topic, "jogo/tempo") == 0) {
    tempoRestante = atoi(msg);
  }

  // 5. Pistas recebidas (diretas ou roteadas pela Central)
  else {
    int idOrigem = 0;
    if (sscanf(topic, "jogo/modulo/" MODULO_ID "/dica/%d", &idOrigem) == 1) {
      // 'idOrigem' informa qual grupo enviou a dica
      // 'msg' contem o payload puro (ex: labirinto ou codigo)
      // IMPORTANTE: Exibir no display mesmo se 'jaResolvido == true'!
    }
  }
}

void reconectarMQTT() {
  while (!client.connected()) {
    // Conecta configurando Last Will (LWT) para avisar OFFLINE se cair
    if (client.connect("Modulo_" MODULO_ID,
                       "jogo/modulo/" MODULO_ID "/status",
                       0,
                       true,
                       "OFFLINE")) {
      
      // Anuncia que o modulo esta ONLINE (com retain)
      client.publish("jogo/modulo/" MODULO_ID "/status", "ONLINE", true);

      // Inscricoes obrigatorias
      client.subscribe("jogo/estado");
      client.subscribe("jogo/serial");
      client.subscribe("jogo/modulo/ativo");
      client.subscribe("jogo/modulo/" MODULO_ID "/dica/+");

      // Inscricao opcional de tempo
      client.subscribe("jogo/tempo");
    } else {
      delay(1000);
    }
  }
}

// Funcoes auxiliares para publicar eventos e dicas
void registrarSucesso() {
  if (!jogoRodando || !minhaVez || jaResolvido) return;
  jaResolvido = true;
  client.publish("jogo/modulo/" MODULO_ID "/evento", "SOLVED");
}

void registrarErro() {
  if (!jogoRodando || !minhaVez || jaResolvido) return;
  client.publish("jogo/modulo/" MODULO_ID "/evento", "ERROR");
}

// Envia dica diretamente para um grupo especifico
void enviarDicaDireta(int destino, const char* conteudo) {
  if (!jogoRodando) return;
  char topico[36];
  snprintf(topico, sizeof(topico), "jogo/modulo/%d/dica/" MODULO_ID, destino);
  client.publish(topico, conteudo);
}

// Envia dica para a Central rotear a um grupo aleatorio online
void enviarDicaAleatoria(const char* conteudo) {
  if (!jogoRodando) return;
  client.publish("jogo/modulo/" MODULO_ID "/dica", conteudo);
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

  // Exemplo de uso no loop:
  // if (jogoRodando && minhaVez) {
  //   if (jogadorAcertou()) registrarSucesso();
  //   if (jogadorErrou())   registrarErro();
  // }
}
```

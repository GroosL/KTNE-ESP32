# Trabalho final de IoT

* **Projeto:** Jogo Cooperativo IoT (*Keep Talking and Nobody Explodes*)
* **Responsável:** Grupo 1 — Central de Controle
* **Público-alvo:** Grupos 2 a 7
* **Formato de Dados:** Texto simples (ASCII / Plain Text)

---

## 1. Filosofia de Arquitetura

O sistema opera de forma **desacoplada e orientada a eventos**, visando estabilidade na rede Wi-Fi e simplicidade de implementação nos ESP32:

1. **A Central é um orquestrador cego:** A Central **não** valida códigos, senhas ou leituras de sensores de nenhum minijogo. Cada grupo tem total autonomia e responsabilidade para validar a lógica interna do seu módulo.
2. **Sem dependência de bibliotecas pesadas:** Todas as mensagens são strings simples de texto puro, dispensando bibliotecas de manipulação JSON (`ArduinoJson`). Comparações são feitas via `strcmp()` e conversões via `atoi()`.
3. **Consumo de tempo opcional:** O cronômetro oficial é mantido pela Central. O tempo restante é transmitido a cada segundo em um tópico separado para quem quiser criar animações, alertas ou exibir em display próprio; os módulos que não precisarem dessa informação não precisam assinar esse tópico.

---

---
## 2. Possiveis alterações

1. Geração e envio do serial da bomba no começo de cada jogo pela central

---

## 3. Especificação dos Tópicos MQTT

### 3.1. Estado Global da Partida (Central $\to$ Todos)
Todos os módulos (Grupos 2 a 7) **devem assinar (`subscribe`)** este tópico para reagir às mudanças globais da partida.

* **Tópico:** `jogo/estado`
* **QoS:** 1 (ou 0 com broker estável)
* **Payloads aceitos:**

| Payload | Origem | Ação esperada nos Módulos (2 a 7) |
| :--- | :--- | :--- |
| `START` | Central | Habilitar periféricos, zerar tentativas locais e iniciar o desafio. |
| `RESET` | Central | Cancelar partida em andamento e retornar à tela de repouso (*standby*). |
| `VICTORY` | Central | Todos os módulos concluídos dentro do prazo. Travar em modo vitória. |
| `DEFEAT` | Central | Tempo esgotado ou limite de 3 erros atingido. Travar em modo falha. |

---

### 3.2. Sincronização do Cronômetro (Central $\to$ Rede) — *Opcional*
Publicado pela Central a cada segundo **apenas enquanto o jogo estiver em andamento** (`RUNNING`).

* **Tópico:** `jogo/tempo`
* **QoS:** 0 (*fire-and-forget*, não sobrecarrega a fila do broker)
* **Payload:** Inteiro em formato texto representando os segundos restantes (ex.: `"299"`, `"180"`, `"15"`).
* **Uso sugerido nos módulos:**
  * Mostrar tempo no display local (se houver).
  * Acelerar bipes de buzzer ou piscar de LEDs nos últimos 30 segundos.
  * **Atenção:** Módulos que não forem utilizar essa funcionalidade **não devem assinar** o tópico `jogo/tempo`, economizando ciclos de CPU e memória.

---

### 3.3. Notificação de Eventos (Módulos $\to$ Central)
Cada módulo publica apenas quando o jogador cometer um erro ou concluir o desafio. Substituir `<ID>` pelo número do seu grupo ($2 \text{ a } 7$).

* **Tópico:** `jogo/modulo/<ID>/evento` *(Exemplo Grupo 2: `jogo/modulo/2/evento`)*
* **QoS:** 1 (recomendado para garantir a entrega)
* **Payloads aceitos:**

| Payload | Quando publicar | Efeito na Central |
| :--- | :--- | :--- |
| `SOLVED` | Quando o desafio do módulo for concluído com sucesso. | Soma +1 no contador de resolvidos (meta: 6). |
| `ERROR` | Quando o jogador cometer um erro crítico na interação. | Adiciona 1 strike (ao atingir 3 strikes $\to$ `DEFEAT`). |

> **Importante:** Não envie medições parciais ou telemetria contínua neste tópico. Envie apenas na ocorrência do evento.

---

### 3.4. Compartilhamento de Pistas (Módulo $\to$ Módulo)
Para atender à regra de dependência mútua entre desafios, os módulos trocam informações diretamente entre si sem intermediação da Central.

* **Tópico:** `jogo/modulo/<ID_DESTINO>/dica` ou a escolha dos grupos envolvidos
* **Payload:** Texto simples com a pista necessária (ex.: `"3841"`, `"VERDE"`, `"FAIXA_B"`).
* **Exemplo de fluxo:**
  1. O Grupo 3 (Sequência) é resolvido pelo jogador.
  2. O código do Grupo 3 publica `"8421"` no tópico `jogo/modulo/2/dica`.
  3. O Grupo 2 (Senha) recebe a dica e valida se o jogador digita esse mesmo código no teclado matricial.

---

## 4. Configurações de Conexão e Broker

* **Retain Flag:** Publicar sempre com `retain = false`. O uso indevido de mensagens retidas pode disparar comandos antigos quando um ESP32 for reiniciado.
* **Keep Alive:** Padrão de 60 segundos na biblioteca `PubSubClient`.
* **Identificador MQTT (Client ID):** Usar nomes únicos no momento da conexão, como `ESP32_Modulo_2`, `ESP32_Modulo_3`, etc., para evitar desconexões por colisão de IDs no broker.

---

## 5. Implementação de Referência (C++ / Arduino)

Snippet base compatível com ESP32 e biblioteca `PubSubClient`:

```cpp
#include <WiFi.h>
#include <PubSubClient.h>

#define MODULO_ID "2" // Substitua pelo número do seu grupo (2 a 7)
#define USAR_TEMPO false // Altere para true se o seu módulo for usar o relógio

WiFiClient espClient;
PubSubClient client(espClient);

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  char msg[length + 1];
  memcpy(msg, payload, length);
  msg[length] = '\0';

  // 1. Mensagens globais da Central
  if (strcmp(topic, "jogo/estado") == 0) {
    if (strcmp(msg, "START") == 0) {
      // Iniciar/habilitar desafio local
    } 
    else if (strcmp(msg, "RESET") == 0) {
      // Resetar variáveis e tela local
    } 
    else if (strcmp(msg, "VICTORY") == 0 || strcmp(msg, "DEFEAT") == 0) {
      // Travar periféricos e exibir tela final
    }
  }

  // 2. Sincronização de tempo (se habilitado)
  #if USAR_TEMPO
  if (strcmp(topic, "jogo/tempo") == 0) {
    int segundos_restantes = atoi(msg);
    // Exemplo: if (segundos_restantes <= 30) { alertarTempoCritico(); }
  }
  #endif

  // 3. Dica recebida de outro módulo
  if (strcmp(topic, "jogo/modulo/" MODULO_ID "/dica") == 0) {
    // Processar dica (ex: msg contém "8421")
  }
}

void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32_Modulo_" MODULO_ID)) {
      client.subscribe("jogo/estado");
      client.subscribe("jogo/modulo/" MODULO_ID "/dica");
      
      #if USAR_TEMPO
      client.subscribe("jogo/tempo");
      #endif
    } else {
      delay(1000);
    }
  }
}

// Funções para notificar a Central:
void notificarDesafioConcluido() {
  client.publish("jogo/modulo/" MODULO_ID "/evento", "SOLVED");
}

void notificarErroJogador() {
  client.publish("jogo/modulo/" MODULO_ID "/evento", "ERROR");
}
```

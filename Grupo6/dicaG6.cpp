/*
  Tela de dicas do modulo Labirinto (Grupo 6) - trecho para o super codigo

  Plataforma: ESP32 TTGO T-Display (TFT 240x135).
  Dependencias: TFT_eSPI e PubSubClient (ja usados pelo super codigo).

  O QUE FAZ
  Enquanto o Labirinto (G6) e o modulo ativo, as placas dos OUTROS grupos mostram
  esta tela. No inicio da vez do G6, o labirinto publica as 3 posicoes das bombas
  em jogo/modulo/6/dica; a central sorteia uma placa para cada posicao e repassa
  em jogo/modulo/<ID_DA_PLACA>/dica/6. A placa que recebe exibe a posicao
  (ex.: "BOMBA EM: B4") para o jogador avisar quem esta no labirinto.
  Uma mesma placa pode receber mais de uma posicao; a tela guarda e mostra ate 3.

  COMO INTEGRAR
  1. Este arquivo e um TRECHO de codigo: cole o bloco "if" inteiro dentro do loop()
     do super codigo, depois de client.loop(). Nao deixe este .cpp solto na pasta
     do sketch, pois a IDE tentaria compila-lo sozinho.
  2. Ajuste a condicao do if para a variavel que guarda o modulo ativo
     (valor recebido em jogo/modulo/ativo). O nome usado abaixo e moduloAtivoAtual.
  3. O trecho usa as variaveis do codigo de referencia do README (RFC 001):
       tft              objeto TFT_eSPI da placa
       temDicaNova      true quando chega mensagem em jogo/modulo/<MEU_ID>/dica/<ORIGEM>
       grupoOrigemDica  numero do grupo de origem extraido do topico
       conteudoDica     payload da dica (string terminada em '\0')
       serialBomba      serial da partida (jogo/serial)
     Se o super codigo usar outros nomes, troque-os no trecho.
  4. A placa precisa assinar jogo/modulo/<MEU_ID>/dica/+ ao conectar no broker.
  5. Enquanto o G6 estiver ativo, o tratamento generico de dicas do super codigo
     nao deve consumir (zerar temDicaNova) as dicas vindas do grupo 6.
  6. O bloco deve executar a cada volta do loop(): o PubSubClient entrega uma
     mensagem por chamada de client.loop(), e cada dica e consumida aqui antes
     de a proxima sobrescrever conteudoDica.
  7. Nao e necessario configurar rotacao: a tela funciona com rotacao 1 ou 3.

  COMPORTAMENTO
  - Ao entrar na vez do G6, a tela "Aguardando dica..." e desenhada.
  - Cada dica do grupo 6 e normalizada (sem espacos, maiuscula), repetidas sao
    ignoradas, e a tela e redesenhada com as posicoes recebidas.
  - As dicas sao descartadas quando o serial da partida muda (nova partida).
*/

// TODO: confirmar o nome da variavel do modulo ativo no super codigo
if (moduloAtivoAtual == 6) {
  // estado da tela de dicas, mantido entre as voltas do loop
  static char dicasG6[3][8];
  static int qtdDicasG6 = 0;
  static char serialDicasG6[8] = "";
  static unsigned long ultimaExecucaoG6 = 0;
  static bool redesenharG6 = true;

  // nova partida (serial diferente): descarta as dicas da partida anterior
  if (strcmp(serialBomba, serialDicasG6) != 0) {
    strncpy(serialDicasG6, serialBomba, sizeof(serialDicasG6) - 1);
    serialDicasG6[sizeof(serialDicasG6) - 1] = '\0';
    qtdDicasG6 = 0;
    redesenharG6 = true;
  }

  // bloco ficou mais de 300 ms sem executar: e uma nova entrada na vez do G6, entao redesenha
  if (millis() - ultimaExecucaoG6 > 300) redesenharG6 = true;
  ultimaExecucaoG6 = millis();

  // consome a dica vinda do grupo 6
  if (temDicaNova && grupoOrigemDica == 6) {
    temDicaNova = false;

    // normaliza o texto: remove espacos e converte para maiusculo (ex.: " b4\n" -> "B4")
    char posG6[8];
    int n = 0;
    for (const char* p = conteudoDica; *p && n < (int)sizeof(posG6) - 1; p++) {
      if (!isspace((unsigned char)*p)) posG6[n++] = toupper((unsigned char)*p);
    }
    posG6[n] = '\0';

    // verifica se a posicao ja foi recebida
    bool repetidaG6 = false;
    for (int i = 0; i < qtdDicasG6; i++)
      if (strcmp(dicasG6[i], posG6) == 0) repetidaG6 = true;

    // guarda a nova posicao se for valida, inedita e ainda houver espaco
    if (n > 0 && !repetidaG6 && qtdDicasG6 < 3) {
      strcpy(dicasG6[qtdDicasG6++], posG6);
      redesenharG6 = true;
    }
  }

  // desenha a tela apenas quando houver mudanca
  if (redesenharG6) {
    redesenharG6 = false;

    // cabecalho com titulo e linha separadora
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("LABIRINTO (G6)", 120, 12);
    tft.drawFastHLine(10, 26, 220, TFT_DARKGREY);

    if (qtdDicasG6 == 0) {
      // sem dicas: mostra a espera e a instrucao para o jogador
      tft.setTextSize(2);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("Aguardando dica...", 120, 52);

      tft.setTextSize(1);
      tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      tft.drawString("Se aparecer a posicao de uma bomba,", 120, 84);
      tft.drawString("avise quem esta jogando o LABIRINTO", 120, 98);
      tft.drawString("para a bola NAO passar por ela.", 120, 112);
    } else {
      // titulo no singular ou plural conforme a quantidade
      tft.setTextSize(2);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString(qtdDicasG6 == 1 ? "BOMBA EM:" : "BOMBAS EM:", 120, 40);

      // monta uma linha com as posicoes separadas por dois espacos, sem estourar o buffer
      char linhaG6[32] = "";
      for (int i = 0; i < qtdDicasG6; i++) {
        if (i > 0) strncat(linhaG6, "  ", sizeof(linhaG6) - strlen(linhaG6) - 1);
        strncat(linhaG6, dicasG6[i], sizeof(linhaG6) - strlen(linhaG6) - 1);
      }

      // reduz o tamanho da fonte conforme aumenta a quantidade, para caber na largura da tela
      tft.setTextSize(qtdDicasG6 == 1 ? 5 : (qtdDicasG6 == 2 ? 4 : 3));
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString(linhaG6, 120, 74);

      // instrucao para repassar a dica a quem joga o labirinto
      tft.setTextSize(1);
      tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      tft.drawString("Avise quem esta jogando o LABIRINTO:", 120, 108);
      tft.drawString("a bola NAO pode passar por ai!", 120, 122);
    }
  }
}
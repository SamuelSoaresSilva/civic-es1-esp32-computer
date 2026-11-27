#pragma once

#include <stdint.h>

struct Leitura {
  float valor;
  bool valida;
};

struct AmostraPainel {
  Leitura rpm;
  Leitura borboleta;
  Leitura arAdmissao;
  Leitura arrefecimento;
  Leitura sonda;
  Leitura ajusteCurto;
  Leitura ajusteLongo;
  Leitura tensao;
  Leitura avanco;
};

// A fonte devolve o último valor disponível de cada sinal, sem bloquear.
using FontePainel = AmostraPainel (*)(uint32_t tempoMs);
AmostraPainel lerSimulacao(uint32_t tempoMs);

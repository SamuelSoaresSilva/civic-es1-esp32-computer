#pragma once

#include <stdint.h>

struct Leitura {
  int16_t valor;
  const char *unidade;
  bool valida;
};

struct AmostraPainel {
  Leitura rpm;
  Leitura velocidade;
  Leitura temperatura;
};

// Recebe o tempo desde a inicialização. Uma fonte futura deve devolver
// a amostra disponível sem bloquear a atualização do painel.
AmostraPainel lerSimulacao(uint32_t tempoMs);

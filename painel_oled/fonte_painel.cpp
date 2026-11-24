#include "fonte_painel.h"

namespace {
struct Ponto {
  uint32_t tempoMs;
  int16_t rpm;
  int16_t velocidade;
  int16_t temperatura;
};

constexpr Ponto roteiro[] = {
    {0,     820,  0, 86},  // Marcha lenta.
    {3000,  820,  0, 86},
    {4000, 2200, 12, 86},
    {7000, 5500, 42, 88},  // Aceleração em primeira.
    {7500, 3200, 42, 88},  // Troca para segunda.
    {12000, 5100, 69, 91},
    {12500, 3500, 69, 91}, // Troca para terceira.
    {15000, 4100, 81, 92},
    {18000, 2300, 51, 91}, // Desaceleração.
    {21000, 1100, 17, 89},
    {23000,  820,  0, 88},
    {28000,  820,  0, 86},
};
constexpr auto totalPontos = sizeof(roteiro) / sizeof(roteiro[0]);

int16_t interpolar(int16_t inicio, int16_t fim, int32_t tempo, int32_t duracao) {
  return inicio + (static_cast<int32_t>(fim) - inicio) * tempo / duracao;
}
}  // namespace

AmostraPainel lerSimulacao(uint32_t tempoMs) {
  const uint32_t fase = tempoMs % roteiro[totalPontos - 1].tempoMs;
  unsigned indice = 1;
  while (fase >= roteiro[indice].tempoMs) {
    ++indice;
  }
  const Ponto &inicio = roteiro[indice - 1];
  const Ponto &fim = roteiro[indice];
  const int32_t tempo = fase - inicio.tempoMs;
  const int32_t duracao = fim.tempoMs - inicio.tempoMs;
  const bool disponivel = fase < 24000 || fase >= 26000;
  return {
      {interpolar(inicio.rpm, fim.rpm, tempo, duracao), "RPM", disponivel},
      {interpolar(inicio.velocidade, fim.velocidade, tempo, duracao), "km/h", disponivel},
      {interpolar(inicio.temperatura, fim.temperatura, tempo, duracao), "\xC2\xB0" "C", disponivel},
  };
}

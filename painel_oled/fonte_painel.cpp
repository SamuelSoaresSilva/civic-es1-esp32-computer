#include "fonte_painel.h"
#include <math.h>

namespace {
struct Ponto {
  uint32_t tempoMs;
  float rpm;
  float borboleta;
  float ar;
  float liquido;
  float avanco;
};

constexpr Ponto roteiro[] = {
    {0,     820,  8, 36, 83,  12},
    {3000,  820,  8, 36, 84,  12},
    {4000, 2200, 55, 34, 84,  23},
    {7000, 6800, 82, 31, 86,  32},
    {7500, 3200, 20, 31, 86,  16},
    {12000, 5100, 74, 29, 89, 34},
    {12500, 3500, 18, 29, 89, 14},
    {15000, 4100, 57, 28, 91, 29},
    {18000, 2300,  9, 30, 91, -4},
    {19500, 1100,  8, 32, 101,  8},
    {20500,  480,  5, 33, 102,  4},
    {21500,  480,  5, 34, 101,  4},
    {23000,  820,  8, 34, 87, 12},
    {28000,  820,  8, 36, 83, 12},
};
constexpr auto totalPontos = sizeof(roteiro) / sizeof(roteiro[0]);

float interpolar(float inicio, float fim, float fracao) {
  return inicio + (fim - inicio) * fracao;
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
  const float fracao = static_cast<float>(fase - inicio.tempoMs) / (fim.tempoMs - inicio.tempoMs);
  const bool disponivel = fase < 24000 || fase >= 26000;
  const float segundos = fase / 1000.0f;
  const float tensao = fase >= 20500 && fase < 22300
      ? 11.35f : 13.85f + 0.23f * sinf(segundos * 1.4f);
  return {
      {interpolar(inicio.rpm, fim.rpm, fracao), disponivel},
      {interpolar(inicio.borboleta, fim.borboleta, fracao), disponivel},
      {interpolar(inicio.ar, fim.ar, fracao), disponivel},
      {interpolar(inicio.liquido, fim.liquido, fracao), disponivel},
      {0.45f + 0.35f * sinf(segundos * 6), disponivel},
      {5.0f * sinf(segundos * 2), disponivel},
      {-2.0f, disponivel},
      {tensao, disponivel},
      {interpolar(inicio.avanco, fim.avanco, fracao), disponivel},
  };
}

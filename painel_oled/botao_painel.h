#pragma once

#include <stdint.h>

enum class AcaoBotao { Nenhuma, Clique, PressaoLonga };

class BotaoPainel {
 public:
  AcaoBotao atualizar(bool pressionado, uint32_t agora) {
    if (pressionado != leituraBruta) {
      leituraBruta = pressionado;
      mudouEm = agora;
    }

    if (leituraBruta != estadoEstavel && agora - mudouEm >= DEBOUNCE_MS) {
      estadoEstavel = leituraBruta;
      if (estadoEstavel) {
        pressionadoEm = mudouEm;
        longaEmitida = false;
      } else if (!longaEmitida) {
        return mudouEm - pressionadoEm >= PRESSAO_LONGA_MS
                   ? AcaoBotao::PressaoLonga
                   : AcaoBotao::Clique;
      }
    }

    if (estadoEstavel && leituraBruta && !longaEmitida &&
        agora - pressionadoEm >= PRESSAO_LONGA_MS) {
      longaEmitida = true;
      return AcaoBotao::PressaoLonga;
    }
    return AcaoBotao::Nenhuma;
  }

 private:
  static constexpr uint32_t DEBOUNCE_MS = 25;
  static constexpr uint32_t PRESSAO_LONGA_MS = 800;
  bool leituraBruta = false;
  bool estadoEstavel = false;
  bool longaEmitida = false;
  uint32_t mudouEm = 0;
  uint32_t pressionadoEm = 0;
};

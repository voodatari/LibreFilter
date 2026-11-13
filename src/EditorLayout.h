#pragma once

#include <juce_graphics/juce_graphics.h>

// Geometria del editor (skin LibreFilter), en el espacio de diseno 940 x 980 del pack grafico.
// Las posiciones salen de LibreFilter_visual_reference.png (resources/); si se mueve algo, se mueve aqui.
namespace ffl
{
    constexpr int designW = 940, designH = 980;
    constexpr int kBandCount = 30;                  // = ff::kBands
    using R = juce::Rectangle<float>;

    // ---- analizador ----
    const R analyzerRect { 26.0f, 208.0f, 888.0f, 326.0f };     // marco (graph_frame.svg)
    const R plotRect     { 41.0f, 226.0f, 783.0f, 292.0f };     // zona de curvas
    const R scaleColumn  { 838.0f, 226.0f, 70.0f, 292.0f };     // etiquetas dB / kHz a la derecha
    constexpr float dbTop = 20.0f, dbBottom = -80.0f;

    // ---- tablero de 30 sliders ----
    const R boardRect { 38.0f, 594.0f, 780.0f, 152.0f };
    constexpr float bandW = 780.0f / (float) kBandCount;        // 26
    const R boardScale { 838.0f, 566.0f, 70.0f, 24.0f };        // etiqueta "dB" de la escala

    // ---- cabecera ----
    const R logoRect   { 636.0f, 62.0f, 290.0f, 59.0f };        // logo LibreFilter; clic = creditos
    const R aboutRect  = logoRect;
    const R stripRect  { 31.0f, 141.0f, 880.0f, 53.0f };
    const R morphBar   { 41.0f, 153.0f, 252.0f, 31.0f };
    const R morphLabel { 300.0f, 151.0f, 114.0f, 38.0f };
    const R morphBox   { 424.0f, 151.0f, 107.0f, 38.0f };
    const R hzLabel    { 545.0f, 151.0f, 62.0f, 38.0f };
    const R hzBox      { 613.0f, 151.0f, 106.0f, 38.0f };
    const R dbLabel    { 727.0f, 151.0f, 66.0f, 38.0f };
    const R dbBox      { 799.0f, 151.0f, 106.0f, 38.0f };
    const R statusRect { 56.0f, 230.0f, 700.0f, 30.0f };

    // ---- fila inferior ----
    const R globalLabel { 312.0f, 788.0f, 100.0f, 46.0f };
    const R valueBox    { 820.0f, 788.0f, 92.0f, 46.0f };
    const R presetField { 86.0f, 854.0f, 332.0f, 40.0f };
    const R voodRect    { 797.0f, 850.0f, 205.0f, 65.0f };
    const R versionRect { 800.0f, 910.0f, 130.0f, 18.0f };

    enum ButtonId { Source, Dest, Learn, Match, ModeFree, ModeLevel, ModeLine, Reset, Freeze, Lin, Log,
                    PresetPrev, PresetNext, PresetSave, PresetDel, NumButtons };

    inline R buttonRect (int id)
    {
        switch (id)
        {
            case Source:     return {  41.0f,  71.0f, 112.0f, 54.0f };
            case Dest:       return { 183.0f,  71.0f, 112.0f, 54.0f };
            case Learn:      return { 318.0f,  71.0f, 112.0f, 54.0f };
            case Match:      return { 450.0f,  71.0f, 112.0f, 54.0f };
            case ModeFree:   return {  42.0f, 788.0f,  76.0f, 46.0f };
            case ModeLevel:  return { 140.0f, 788.0f,  76.0f, 46.0f };
            case ModeLine:   return { 228.0f, 788.0f,  76.0f, 46.0f };
            case Reset:      return { 416.0f, 788.0f,  84.0f, 46.0f };
            case Freeze:     return { 516.0f, 788.0f,  94.0f, 46.0f };
            case Lin:        return { 627.0f, 788.0f,  76.0f, 46.0f };
            case Log:        return { 719.0f, 788.0f,  76.0f, 46.0f };
            case PresetPrev: return {  34.0f, 854.0f,  40.0f, 40.0f };
            case PresetNext: return { 431.0f, 854.0f,  40.0f, 40.0f };
            case PresetSave: return { 483.0f, 854.0f,  84.0f, 40.0f };
            case PresetDel:  return { 581.0f, 854.0f,  66.0f, 40.0f };
            default:         return {};
        }
    }
}

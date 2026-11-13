#pragma once

#include <juce_graphics/juce_graphics.h>

// Geometria del editor (skin LibreFilter), en el espacio de diseno 940 x 964 (el pack grafico es 940 x 980).
// Las posiciones salen de LibreFilter_visual_reference.png (resources/); si se mueve algo, se mueve aqui.
//
// Reparto vertical (de arriba abajo): botones + logo | lecturas | analizador | leyenda | bolitas + sliders |
// modos y Global | presets + credito.
namespace lfl
{
    constexpr int designW = 940, designH = 964;
    constexpr int kBandCount = 30;                  // = lf::kBands
    using R = juce::Rectangle<float>;

    // ---- cabecera ----
    const R logoRect   { 622.0f, 14.0f, 300.0f, 84.0f };        // logo del plugin (logo_plugin.png); clic = tema rojo
    const R stripRect  { 31.0f, 112.0f, 880.0f, 50.0f };
    const R morphBar   { 41.0f, 122.0f, 252.0f, 31.0f };
    const R morphLabel { 300.0f, 118.0f, 114.0f, 38.0f };
    const R morphBox   { 424.0f, 118.0f, 107.0f, 38.0f };
    const R hzLabel    { 545.0f, 118.0f, 62.0f, 38.0f };
    const R hzBox      { 613.0f, 118.0f, 106.0f, 38.0f };
    const R dbLabel    { 727.0f, 118.0f, 66.0f, 38.0f };
    const R dbBox      { 799.0f, 118.0f, 106.0f, 38.0f };

    // ---- analizador ----
    const R analyzerRect { 26.0f, 168.0f, 888.0f, 372.0f };     // marco (graph_frame.svg)
    const R plotRect     { 41.0f, 186.0f, 783.0f, 338.0f };     // zona de curvas
    const R scaleColumn  { 838.0f, 186.0f, 70.0f, 338.0f };     // etiquetas dB / kHz a la derecha
    const R statusRect   { 56.0f, 190.0f, 700.0f, 30.0f };
    constexpr float dbTop = 20.0f, dbBottom = -80.0f;

    // leyenda de colores: pegada al marco del analizador, lejos de las bolitas
    constexpr float legendTextY = 543.0f, legendTextH = 24.0f, legendLineY = 553.0f;

    // ---- tablero de 30 sliders ----
    const R boardRect { 38.0f, 616.0f, 780.0f, 200.0f };
    constexpr float bandW = 780.0f / (float) kBandCount;        // 26
    constexpr float ledOffset = 28.0f, ledSize = 20.0f;         // bolita: boardRect.y - ledOffset
    const R boardScale { 838.0f, 586.0f, 70.0f, 24.0f };        // etiqueta "dB" de la escala

    // ---- fila inferior ----
    const R globalLabel { 296.0f, 842.0f, 92.0f, 46.0f };       // termina 28 px antes del boton Reset
    const R valueBox    { 820.0f, 842.0f, 92.0f, 46.0f };
    const R presetField { 86.0f, 900.0f, 332.0f, 40.0f };       // caja del preset: desplegable al pulsar
    const R creatorRect { 722.0f, 896.0f, 200.0f, 42.0f };      // logo del creador (logo_creator.png); clic = creditos
    const R versionRect { 760.0f, 942.0f, 150.0f, 14.0f };      // 6 px por encima del borde inferior

    enum ButtonId { Source, Dest, Learn, Match, ModeFree, ModeLevel, ModeLine, Reset, Freeze, Lin, Log,
                    PresetPrev, PresetNext, PresetSave, PresetDel, NumButtons };

    inline R buttonRect (int id)
    {
        switch (id)
        {
            case Source:     return {  41.0f,  28.0f, 112.0f, 54.0f };
            case Dest:       return { 183.0f,  28.0f, 112.0f, 54.0f };
            case Learn:      return { 318.0f,  28.0f, 112.0f, 54.0f };
            case Match:      return { 450.0f,  28.0f, 112.0f, 54.0f };
            case ModeFree:   return {  42.0f, 842.0f,  76.0f, 46.0f };
            case ModeLevel:  return { 140.0f, 842.0f,  76.0f, 46.0f };
            case ModeLine:   return { 228.0f, 842.0f,  76.0f, 46.0f };
            case Reset:      return { 416.0f, 842.0f,  84.0f, 46.0f };
            case Freeze:     return { 516.0f, 842.0f,  94.0f, 46.0f };
            case Lin:        return { 627.0f, 842.0f,  76.0f, 46.0f };
            case Log:        return { 719.0f, 842.0f,  76.0f, 46.0f };
            case PresetPrev: return {  34.0f, 900.0f,  40.0f, 40.0f };
            case PresetNext: return { 431.0f, 900.0f,  40.0f, 40.0f };
            case PresetSave: return { 483.0f, 900.0f,  84.0f, 40.0f };
            case PresetDel:  return { 581.0f, 900.0f,  66.0f, 40.0f };
            default:         return {};
        }
    }
}

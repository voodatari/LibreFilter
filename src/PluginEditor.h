#pragma once

#include "PluginProcessor.h"
#include "LibreSkin.h"
#include "EditorLayout.h"
#include <juce_dsp/juce_dsp.h>
#include <set>

// Interfaz LibreFilter (pack grafico 940x980: analizador, 30 sliders, botones).
// Las imagenes salen de resources/librefilter (no se usan los mapas de bits del plugin original).
class LibreFilterEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit LibreFilterEditor (LibreFilterProcessor&);
    ~LibreFilterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static constexpr int designW = lfl::designW, designH = lfl::designH;

private:
    enum class DrawMode { freehand, level, line };

    void timerCallback() override;
    juce::AffineTransform designToScreen() const;
    juce::Point<float> toDesign (juce::Point<float> p) const;
    void rebuildBackground();

    void paintAnalyzer (juce::Graphics&);
    void paintBands (juce::Graphics&);
    void paintButtons (juce::Graphics&);
    void paintAbout (juce::Graphics&);
    void drawButton (juce::Graphics&, juce::Rectangle<float>, const juce::String&, bool on, bool enabled = true);

    // geometría (espacio de diseño)
    int bandAt (float x) const;
    float valueAt (float y) const;
    float freqToX (float hz) const;
    float xToFreq (float x) const;
    float dbToY (float db) const;

    void setBand (int b, float dB);

    // Bolitas sobre los sliders (como en el original): marcan la banda que se esta tocando o, al dibujar
    // sobre varias a la vez, el rango tocado. Se apagan al cambiar de modo de dibujo y con Reset.
    std::array<bool, lf::kBands> led {};
    void clearLeds() { led.fill (false); }
    void setLedRange (int lo, int hi) { clearLeds(); for (int i = lo; i <= hi; ++i) led[(size_t) i] = true; }
    void resetAll();
    bool isLogAxis() const;

    LibreFilterProcessor& processor;

    // analizador
    static constexpr int kFftOrder = 11, kFftSize = 1 << kFftOrder;
    juce::dsp::FFT fft { kFftOrder };
    std::array<float, kFftSize> window {};
    std::array<float, kFftSize * 2> fftBuf {};
    std::array<float, kFftSize> sampBuf {};
    std::array<float, kFftSize / 2> specIn {}, specOut {};

    float axisMax() const;                           // frecuencia maxima de ambos ejes (<= 24 kHz)
    // ---- presets de curvas aprendidas (sin dialogos del sistema) ----
    // Paleta del tema (azul LibreFilter o rojo "Steinberg")
    struct Palette
    {
        juce::Colour textLight, textDark, boxFill, boxLine, gridLine, bed, bedEdge, morphBg, morphOn, morphOff,
                     sliderFill, field, focus, highlight, hint, panelDark, menuBg;
    };
    Palette pal;
    bool redTheme = false;
    void applyTheme (bool red);

    // Caja de presets: al pulsar abre un desplegable; "New name..." la convierte en un campo de texto
    juce::TextEditor presetName;                     // guarda el nombre actual; visible solo mientras se escribe
    juce::LookAndFeel_V4 menuLaf;
    bool editingName = false;
    juce::String nameBeforeEdit;
    void paintPresetField (juce::Graphics&);
    void showPresetMenu();
    void presetLoadIndex (int index);
    void startNameEdit();
    void endNameEdit (bool commit);
    std::vector<juce::File> presetFiles;
    int presetIndex = -1;
    juce::int64 delArmedUntil = 0;
    void refreshPresets (const juce::String& selectName = {});
    void presetStep (int delta);
    void presetSave();
    void presetDelete();
    void layoutPresetEditor();

    LibreSkin skin;
    juce::Image background;
    bool showAbout = false;

    DrawMode drawMode = DrawMode::freehand;
    std::set<int> touched;
    bool draggingBoard = false;
    juce::Point<float> dragStart;
    int lastBand = -1;
    std::array<float, lf::kBands> startValues {};

    juce::Point<float> hover { -1.0f, -1.0f };
    juce::String readHz, readDb;
    juce::String statusText;
    juce::int64 statusUntil = 0;
    void setStatus (const juce::String& s);
    bool morphDragging = false;
    void setMorphFromX (float x);
    juce::String idleStatus() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibreFilterEditor)
};

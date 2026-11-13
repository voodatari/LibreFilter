#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

// Recursos graficos del pack "LibreFilter" (resources/librefilter, incrustados con BinaryData).
// Solo dibuja: no toca parametros ni logica del plugin.
//
// JUCE no interpreta bien algunas cosas de los SVG del pack (filtros de brillo, el patron "grain",
// el degradado del texto), asi que al cargarlos se preprocesa el XML: se quita el patron, y los
// brillos se sustituyen por halos de varias capas. Los logos se dibujan con texto de JUCE.
class LibreSkin
{
public:
    enum class ButtonState { normal, active, disabled };

    LibreSkin();

    // Tema rojizo (easter egg, recuerda al plugin original de Steinberg): recolorea los SVG/PNG del pack al vuelo.
    void setRedTheme (bool red);
    bool isRedTheme() const { return redTheme; }

    // Panel completo (ondas, borde, grano) recortado a su esquina redondeada; w x h = tamano en pixeles.
    void drawBackground (juce::Graphics&, juce::Rectangle<float> area) const;
    void drawButton (juce::Graphics&, juce::Rectangle<float>, ButtonState) const;
    void drawGraphFrame (juce::Graphics&, juce::Rectangle<float>) const;
    void drawSliderTrack (juce::Graphics&, juce::Rectangle<float>) const;
    void drawSliderThumb (juce::Graphics&, juce::Rectangle<float>) const;
    // Bolita indicadora de banda: morada o roja (grupos de 5 bandas alternos), encendida o apagada.
    void drawLed (juce::Graphics&, juce::Rectangle<float>, bool lit, bool red) const;
    void drawCredits (juce::Graphics&, juce::Rectangle<float> area) const;   // imagen "cover" (recorta lo que sobre)

    void drawLogoPlugin (juce::Graphics&, juce::Rectangle<float>) const;    // logo LibreFilter (PNG con transparencia)
    void drawLogoCreator (juce::Graphics&, juce::Rectangle<float>) const;   // logo VooD

private:
    void load();
    static std::unique_ptr<juce::Drawable> loadSvg (const void* data, size_t size,
                                                    const std::vector<std::pair<juce::String, juce::String>>& recolour = {},
                                                    int redMode = 0);   // 0 azul, 1 rojo (panel), 2 rojo neutro (botones)
    static void drawFitted (juce::Graphics&, const juce::Image&, juce::Rectangle<float>);

    std::unique_ptr<juce::Drawable> background, buttonNormal, buttonActive, buttonDisabled,
                                    graphFrame, sliderTrack, sliderThumb,
                                    ledPurpleOn, ledPurpleOff, ledRedOn, ledRedOff;
    juce::Image credits, logoPlugin, logoCreator;                       // los del tema actual
    juce::Image creditsBlue, logoPluginBlue, logoCreatorBlue;           // originales del pack
    juce::Image creditsRed, logoPluginRed, logoCreatorRed;              // recoloreados (se crean al primer uso)
    bool redTheme = false;
};

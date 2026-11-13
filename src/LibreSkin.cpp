#include "LibreSkin.h"
#include <BinaryData.h>

namespace
{
    // Quita lo que JUCE no sabe pintar y convierte los filtros de brillo (feGaussianBlur) en halos de capas.
    void preprocessSvg (juce::XmlElement& root)
    {
        std::vector<std::unique_ptr<juce::XmlElement>> kids;
        while (auto* c = root.getFirstChildElement())
        {
            root.removeChildElement (c, false);
            kids.emplace_back (c);
        }

        for (auto& k : kids)
        {
            if (k->getStringAttribute ("fill").startsWith ("url(#grain"))       // patron: JUCE lo pinta negro
                continue;                                                        // (el grano se dibuja en codigo)

            const bool shape = k->hasTagName ("rect") || k->hasTagName ("path");
            if (shape && k->hasAttribute ("filter"))
            {
                k->removeAttribute ("filter");
                const double op = k->getDoubleAttribute ("opacity", 1.0);

                if (k->getStringAttribute ("fill") == "none")                    // trazo con brillo
                {
                    const double w = k->getDoubleAttribute ("stroke-width", 1.0);
                    for (auto layer : { std::pair<double, double> { 9.0, 0.10 }, { 5.0, 0.18 }, { 2.5, 0.30 } })
                    {
                        auto halo = std::make_unique<juce::XmlElement> (*k);
                        halo->setAttribute ("stroke-width", w * layer.first);
                        halo->setAttribute ("opacity", op * layer.second);
                        root.addChildElement (halo.release());
                    }
                }
                else if (k->hasTagName ("rect"))                                 // relleno con brillo
                {
                    for (auto layer : { std::pair<double, double> { 10.0, 0.14 }, { 5.0, 0.22 } })
                    {
                        auto halo = std::make_unique<juce::XmlElement> (*k);
                        const double g = layer.first;
                        halo->setAttribute ("x", k->getDoubleAttribute ("x") - g);
                        halo->setAttribute ("y", k->getDoubleAttribute ("y") - g);
                        halo->setAttribute ("width", k->getDoubleAttribute ("width") + 2.0 * g);
                        halo->setAttribute ("height", k->getDoubleAttribute ("height") + 2.0 * g);
                        halo->setAttribute ("rx", k->getDoubleAttribute ("rx") + g);
                        halo->setAttribute ("opacity", op * layer.second);
                        root.addChildElement (halo.release());
                    }
                }
            }
            root.addChildElement (k.release());
        }
    }

    // Texto en trazo: ancho fijado (el original usa Arial negrita cursiva), baseline en (x, y).
    juce::Path wordPath (const juce::String& text, float x, float baseline, float width)
    {
        juce::Font f (juce::FontOptions ("Arial", 100.0f, juce::Font::bold | juce::Font::italic));
        juce::GlyphArrangement probe;
        probe.addLineOfText (f, text, 0.0f, 0.0f);
        const float w0 = probe.getBoundingBox (0, -1, false).getWidth();
        const float height = 100.0f * (w0 > 0.0f ? width / w0 : 1.0f);

        juce::GlyphArrangement ga;
        ga.addLineOfText (juce::Font (juce::FontOptions ("Arial", height, juce::Font::bold | juce::Font::italic)), text, x, baseline);
        juce::Path p;
        ga.createPath (p);
        return p;
    }

    void drawWordmark (juce::Graphics& g, const juce::Path& text, juce::ColourGradient fill, float outline, float glowCyan)
    {
        if (glowCyan > 0.0f)
        {
            g.setColour (juce::Colour (0xff18dfff).withAlpha (0.20f));
            g.strokePath (text, juce::PathStrokeType (glowCyan * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (juce::Colour (0xff09203c));
        g.strokePath (text, juce::PathStrokeType (outline * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setGradientFill (fill);
        g.fillPath (text);
    }
}

std::unique_ptr<juce::Drawable> LibreSkin::loadSvg (const void* data, size_t size)
{
    auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (static_cast<const char*> (data), (int) size));
    if (xml == nullptr) return nullptr;
    preprocessSvg (*xml);
    return juce::Drawable::createFromSVG (*xml);
}

LibreSkin::LibreSkin()
{
    background     = loadSvg (BinaryData::plugin_background_svg, (size_t) BinaryData::plugin_background_svgSize);
    buttonNormal   = loadSvg (BinaryData::button_normal_svg,     (size_t) BinaryData::button_normal_svgSize);
    buttonActive   = loadSvg (BinaryData::button_active_svg,     (size_t) BinaryData::button_active_svgSize);
    buttonDisabled = loadSvg (BinaryData::button_disabled_svg,   (size_t) BinaryData::button_disabled_svgSize);
    graphFrame     = loadSvg (BinaryData::graph_frame_svg,       (size_t) BinaryData::graph_frame_svgSize);
    sliderTrack    = loadSvg (BinaryData::slider_track_svg,      (size_t) BinaryData::slider_track_svgSize);
    sliderThumb    = loadSvg (BinaryData::slider_thumb_svg,      (size_t) BinaryData::slider_thumb_svgSize);
    knob           = loadSvg (BinaryData::knob_purple_svg,       (size_t) BinaryData::knob_purple_svgSize);
    credits        = juce::ImageCache::getFromMemory (BinaryData::credits_background_png, BinaryData::credits_background_pngSize);
}

void LibreSkin::drawBackground (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const float sx = area.getWidth() / 940.0f, sy = area.getHeight() / 980.0f;

    g.saveState();
    juce::Path panel;
    panel.addRoundedRectangle (area, 24.0f * sx);
    g.reduceClipRegion (panel);

    if (background != nullptr)
        background->draw (g, 1.0f, juce::AffineTransform::scale (sx, sy).translated (area.getX(), area.getY()));
    else
        g.fillAll (juce::Colour (0xff0b2c55));

    // grano: una linea tenue cada 5 unidades (el patron "grain" del SVG)
    g.setColour (juce::Colour (0xff9acbff).withAlpha (0.035f));
    for (float y = 1.0f; y < 980.0f; y += 5.0f)
        g.drawHorizontalLine ((int) std::round (area.getY() + y * sy), area.getX(), area.getRight());
    g.restoreState();
}

void LibreSkin::drawButton (juce::Graphics& g, juce::Rectangle<float> r, ButtonState s) const
{
    const auto* d = s == ButtonState::active ? buttonActive.get()
                  : s == ButtonState::disabled ? buttonDisabled.get() : buttonNormal.get();
    if (d != nullptr)
    {
        // el cuerpo del boton (x 4..136, y 3..55 en los tres SVG) ocupa r; el brillo y la sombra salen por fuera
        const float sx = r.getWidth() / 132.0f, sy = r.getHeight() / 52.0f;
        d->draw (g, 1.0f, juce::AffineTransform::scale (sx, sy).translated (r.getX() - 4.0f * sx, r.getY() - 3.0f * sy));
    }
    else
    {
        g.setColour (s == ButtonState::active ? juce::Colours::limegreen : juce::Colours::lightgrey);
        g.fillRoundedRectangle (r, 6.0f);
    }
}

void LibreSkin::drawGraphFrame (juce::Graphics& g, juce::Rectangle<float> r) const
{
    if (graphFrame != nullptr) graphFrame->drawWithin (g, r, juce::RectanglePlacement::stretchToFit, 1.0f);
    else { g.setColour (juce::Colours::black); g.fillRoundedRectangle (r, 12.0f); }
}

void LibreSkin::drawSliderTrack (juce::Graphics& g, juce::Rectangle<float> r) const
{
    if (sliderTrack != nullptr) sliderTrack->drawWithin (g, r, juce::RectanglePlacement::stretchToFit, 1.0f);
    else { g.setColour (juce::Colour (0xff314b69)); g.fillRect (r); }
}

void LibreSkin::drawSliderThumb (juce::Graphics& g, juce::Rectangle<float> r) const
{
    if (sliderThumb != nullptr) sliderThumb->drawWithin (g, r, juce::RectanglePlacement::stretchToFit, 1.0f);
    else { g.setColour (juce::Colours::white); g.fillRect (r); }
}

void LibreSkin::drawLed (juce::Graphics& g, juce::Rectangle<float> r, bool lit) const
{
    if (knob != nullptr) knob->drawWithin (g, r, juce::RectanglePlacement::stretchToFit, lit ? 1.0f : 0.75f);
    else { g.setColour (lit ? juce::Colour (0xffb65cff) : juce::Colour (0xff5b3b86)); g.fillEllipse (r); }
}

void LibreSkin::drawCredits (juce::Graphics& g, juce::Rectangle<float> area) const
{
    if (! credits.isValid()) return;
    const float k = juce::jmax (area.getWidth() / (float) credits.getWidth(), area.getHeight() / (float) credits.getHeight());
    const float w = (float) credits.getWidth() * k, h = (float) credits.getHeight() * k;
    g.saveState();
    juce::Path panel;
    panel.addRoundedRectangle (area, 24.0f * area.getWidth() / 940.0f);
    g.reduceClipRegion (panel);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (credits, juce::Rectangle<float> (area.getCentreX() - w * 0.5f, area.getCentreY() - h * 0.5f, w, h));
    g.restoreState();
}

// logo_librefilter.svg: 440 x 90, texto de 58 px en (8, 61), degradado blanco -> celeste y trazo celeste debajo.
void LibreSkin::drawLogoLibreFilter (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float s = juce::jmin (r.getWidth() / 440.0f, r.getHeight() / 90.0f);
    const auto o = r.getTopLeft();
    const auto text = wordPath ("LibreFilter", o.x + 8.0f * s, o.y + 61.0f * s, 402.0f * s);

    juce::ColourGradient grad (juce::Colours::white, o.x + 8.0f * s, o.y, juce::Colour (0xff18dfff), o.x + 410.0f * s, o.y, false);
    grad.addColour (0.58, juce::Colour (0xffd7edff));
    drawWordmark (g, text, grad, 1.5f * s, 2.5f * s);

    juce::Path swoosh;
    swoosh.startNewSubPath (o.x + 24.0f * s, o.y + 78.0f * s);
    swoosh.quadraticTo (o.x + 205.0f * s, o.y + 88.0f * s, o.x + 410.0f * s, o.y + 65.0f * s);
    g.setColour (juce::Colour (0xff00cfff).withAlpha (0.18f));
    g.strokePath (swoosh, juce::PathStrokeType (9.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (0xff00cfff));
    g.strokePath (swoosh, juce::PathStrokeType (3.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// logo_vood.svg: 220 x 70, texto de 48 px en (8, 49), degradado blanco -> azul claro.
void LibreSkin::drawLogoVooD (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float s = juce::jmin (r.getWidth() / 220.0f, r.getHeight() / 70.0f);
    const auto o = r.getTopLeft();
    const auto text = wordPath ("VooD", o.x + 8.0f * s, o.y + 49.0f * s, 118.0f * s);
    juce::ColourGradient grad (juce::Colours::white, 0.0f, o.y + 8.0f * s, juce::Colour (0xffa8c7e8), 0.0f, o.y + 52.0f * s, false);
    drawWordmark (g, text, grad, 1.5f * s, 0.0f);
}

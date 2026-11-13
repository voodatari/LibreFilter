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
}

std::unique_ptr<juce::Drawable> LibreSkin::loadSvg (const void* data, size_t size,
                                                    const std::vector<std::pair<juce::String, juce::String>>& recolour)
{
    auto text = juce::String::fromUTF8 (static_cast<const char*> (data), (int) size);
    for (const auto& r : recolour) text = text.replace (r.first, r.second);
    auto xml = juce::XmlDocument::parse (text);
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
    // bolitas: el SVG del pack es morado; las demas variantes se obtienen cambiando los colores del degradado
    const auto* knobSvg = BinaryData::knob_purple_svg;
    const auto knobSize = (size_t) BinaryData::knob_purple_svgSize;
    using Map = std::vector<std::pair<juce::String, juce::String>>;
    ledPurpleOn  = loadSvg (knobSvg, knobSize);
    ledPurpleOff = loadSvg (knobSvg, knobSize, Map { { "#f4d8ff", "#8d7aa8" }, { "#b65cff", "#5a3a8c" }, { "#6d20b8", "#33195c" }, { "#24113e", "#140a24" } });
    ledRedOn     = loadSvg (knobSvg, knobSize, Map { { "#f4d8ff", "#ffd9d9" }, { "#b65cff", "#ff4646" }, { "#6d20b8", "#b81c1c" }, { "#24113e", "#3e1111" } });
    ledRedOff    = loadSvg (knobSvg, knobSize, Map { { "#f4d8ff", "#a87a7a" }, { "#b65cff", "#8c3a3a" }, { "#6d20b8", "#5c1919" }, { "#24113e", "#240a0a" } });

    credits     = juce::ImageCache::getFromMemory (BinaryData::credits_background_png, BinaryData::credits_background_pngSize);
    logoPlugin  = juce::ImageCache::getFromMemory (BinaryData::logo_plugin_png,  BinaryData::logo_plugin_pngSize);
    logoCreator = juce::ImageCache::getFromMemory (BinaryData::logo_creator_png, BinaryData::logo_creator_pngSize);
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

void LibreSkin::drawLed (juce::Graphics& g, juce::Rectangle<float> r, bool lit, bool red) const
{
    const auto* d = red ? (lit ? ledRedOn.get() : ledRedOff.get()) : (lit ? ledPurpleOn.get() : ledPurpleOff.get());
    if (d != nullptr) d->drawWithin (g, r, juce::RectanglePlacement::stretchToFit, 1.0f);
    else
    {
        g.setColour (red ? (lit ? juce::Colour (0xffff4646) : juce::Colour (0xff5c1919)) : (lit ? juce::Colour (0xffb65cff) : juce::Colour (0xff33195c)));
        g.fillEllipse (r);
    }
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

void LibreSkin::drawFitted (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> area)
{
    if (! img.isValid()) return;
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (img, area, juce::RectanglePlacement::centred);
}

void LibreSkin::drawLogoPlugin (juce::Graphics& g, juce::Rectangle<float> r) const  { drawFitted (g, logoPlugin, r); }
void LibreSkin::drawLogoCreator (juce::Graphics& g, juce::Rectangle<float> r) const { drawFitted (g, logoCreator, r); }

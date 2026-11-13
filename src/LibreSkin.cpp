#include "LibreSkin.h"
#include <BinaryData.h>

namespace
{
    // Recoloreado del tema rojo. Los grises, blancos y verdes no cambian.
    //  panel:   azul/cian/morado -> rojo/naranja, mas saturado y algo mas brillante (como el panel original)
    //  neutral: -> gris con un toque calido (botones claros del original, con buen contraste)
    //  gold:    -> ambar vivo para los logos; los contornos oscuros se mantienen oscuros
    enum class RedMode { panel, neutral, gold };

    juce::Colour toRed (juce::Colour c, RedMode mode = RedMode::panel)
    {
        float h, sat, b;
        c.getHSB (h, sat, b);
        const float deg = h * 360.0f;
        if (sat < 0.12f || deg < 170.0f || deg > 290.0f) return c;

        if (mode == RedMode::neutral)
            return juce::Colour::fromHSV (10.0f / 360.0f, sat * 0.14f, b, c.getFloatAlpha());

        if (mode == RedMode::gold)
        {
            const float lift = juce::jlimit (0.0f, 1.0f, (b - 0.25f) / 0.35f);          // 0 en oscuros, 1 en claros
            const float nb = b + (std::pow (b, 0.5f) - b) * lift;
            return juce::Colour::fromHSV ((36.0f + (deg - 190.0f) * 0.1f) / 360.0f, juce::jmin (1.0f, sat * 1.15f), nb, c.getFloatAlpha());
        }

        float nh = (4.0f + (deg - 190.0f) * 0.25f) / 360.0f;
        if (nh < 0.0f) nh += 1.0f;
        return juce::Colour::fromHSV (nh, juce::jmin (1.0f, sat * 1.15f), std::pow (b, 0.8f), c.getFloatAlpha());
    }

    juce::String redShiftSvg (const juce::String& svg, RedMode mode)
    {
        const std::string in = svg.toStdString();
        std::string out;
        out.reserve (in.size());
        auto hex = [] (char ch) { return std::isxdigit ((unsigned char) ch) != 0; };
        for (size_t i = 0; i < in.size(); ++i)
        {
            if (in[i] == '#' && i + 6 < in.size() && hex (in[i + 1]) && hex (in[i + 2]) && hex (in[i + 3])
                && hex (in[i + 4]) && hex (in[i + 5]) && hex (in[i + 6]))
            {
                const auto c = toRed (juce::Colour ((juce::uint32) (0xff000000u | (juce::uint32) std::stoul (in.substr (i + 1, 6), nullptr, 16))), mode);
                out += "#" + c.toDisplayString (false).toLowerCase().toStdString();
                i += 6;
            }
            else
                out += in[i];
        }
        return juce::String (out);
    }

    juce::Image redShiftImage (const juce::Image& src, RedMode mode)
    {
        if (! src.isValid()) return {};
        juce::Image img = src.createCopy().convertedToFormat (juce::Image::ARGB);   // copia: no tocar el original azul
        juce::Image::BitmapData d (img, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < d.height; ++y)
            for (int x = 0; x < d.width; ++x)
            {
                auto* px = reinterpret_cast<juce::PixelARGB*> (d.getPixelPointer (x, y));
                const auto a = px->getAlpha();
                if (a == 0) continue;
                px->unpremultiply();
                const auto c = toRed (juce::Colour (px->getInARGBMaskOrder()), mode);
                px->setARGB (a, c.getRed(), c.getGreen(), c.getBlue());
                px->premultiply();
            }
        return img;
    }

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
                                                    const std::vector<std::pair<juce::String, juce::String>>& recolour, int redMode)
{
    auto text = juce::String::fromUTF8 (static_cast<const char*> (data), (int) size);
    for (const auto& r : recolour) text = text.replace (r.first, r.second);
    if (redMode != 0) text = redShiftSvg (text, redMode == 2 ? RedMode::neutral : RedMode::panel);
    auto xml = juce::XmlDocument::parse (text);
    if (xml == nullptr) return nullptr;
    preprocessSvg (*xml);
    return juce::Drawable::createFromSVG (*xml);
}

LibreSkin::LibreSkin()
{
    creditsBlue     = juce::ImageCache::getFromMemory (BinaryData::credits_background_png, BinaryData::credits_background_pngSize);
    logoPluginBlue  = juce::ImageCache::getFromMemory (BinaryData::logo_plugin_png,  BinaryData::logo_plugin_pngSize);
    logoCreatorBlue = juce::ImageCache::getFromMemory (BinaryData::logo_creator_png, BinaryData::logo_creator_pngSize);
    load();
}

void LibreSkin::setRedTheme (bool red)
{
    if (red == redTheme) return;
    redTheme = red;
    load();
}

void LibreSkin::load()
{
    background     = loadSvg (BinaryData::plugin_background_svg, (size_t) BinaryData::plugin_background_svgSize, {}, redTheme ? 1 : 0);
    buttonNormal   = loadSvg (BinaryData::button_normal_svg,     (size_t) BinaryData::button_normal_svgSize, {}, redTheme ? 2 : 0);
    buttonActive   = loadSvg (BinaryData::button_active_svg,     (size_t) BinaryData::button_active_svgSize);
    buttonDisabled = loadSvg (BinaryData::button_disabled_svg,   (size_t) BinaryData::button_disabled_svgSize, {}, redTheme ? 2 : 0);
    graphFrame     = loadSvg (BinaryData::graph_frame_svg,       (size_t) BinaryData::graph_frame_svgSize, {}, redTheme ? 1 : 0);
    sliderTrack    = loadSvg (BinaryData::slider_track_svg,      (size_t) BinaryData::slider_track_svgSize, {}, redTheme ? 1 : 0);
    sliderThumb    = loadSvg (BinaryData::slider_thumb_svg,      (size_t) BinaryData::slider_thumb_svgSize, {}, redTheme ? 1 : 0);
    // bolitas: el SVG del pack es morado; las demas variantes se obtienen cambiando los colores del degradado
    const auto* knobSvg = BinaryData::knob_purple_svg;
    const auto knobSize = (size_t) BinaryData::knob_purple_svgSize;
    using Map = std::vector<std::pair<juce::String, juce::String>>;
    ledPurpleOn  = loadSvg (knobSvg, knobSize);
    ledPurpleOff = loadSvg (knobSvg, knobSize, Map { { "#f4d8ff", "#8d7aa8" }, { "#b65cff", "#5a3a8c" }, { "#6d20b8", "#33195c" }, { "#24113e", "#140a24" } });
    ledRedOn     = loadSvg (knobSvg, knobSize, Map { { "#f4d8ff", "#ffd9d9" }, { "#b65cff", "#ff4646" }, { "#6d20b8", "#b81c1c" }, { "#24113e", "#3e1111" } });
    ledRedOff    = loadSvg (knobSvg, knobSize, Map { { "#f4d8ff", "#a87a7a" }, { "#b65cff", "#8c3a3a" }, { "#6d20b8", "#5c1919" }, { "#24113e", "#240a0a" } });

    if (redTheme)
    {
        if (! creditsRed.isValid())     creditsRed     = redShiftImage (creditsBlue, RedMode::panel);
        if (! logoPluginRed.isValid())  logoPluginRed  = redShiftImage (logoPluginBlue, RedMode::gold);
        if (! logoCreatorRed.isValid()) logoCreatorRed = redShiftImage (logoCreatorBlue, RedMode::gold);
    }
    credits     = redTheme ? creditsRed     : creditsBlue;
    logoPlugin  = redTheme ? logoPluginRed  : logoPluginBlue;
    logoCreator = redTheme ? logoCreatorRed : logoCreatorBlue;
}

void LibreSkin::drawBackground (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const float sx = area.getWidth() / 940.0f, sy = area.getHeight() / 980.0f;

    g.saveState();
    juce::Path panel;
    panel.addRoundedRectangle (area, 24.0f * sx);
    g.reduceClipRegion (panel);

    if (redTheme)
    {
        // panel rojo del plugin original: degradado y trama metalica fina
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffc2432f), 0.0f, area.getY(), juce::Colour (0xff8f2a1f), 0.0f, area.getBottom(), false));
        g.fillRect (area);
        juce::Random rng (0xF1F1);
        for (float y = area.getY(); y < area.getBottom(); y += 1.0f)
        {
            const float a = rng.nextFloat();
            g.setColour ((a > 0.5f ? juce::Colours::white : juce::Colours::black).withAlpha (0.015f + 0.05f * std::abs (a - 0.5f)));
            g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
        }
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawRoundedRectangle (area.reduced (0.5f), 24.0f * sx, 1.0f);
        g.restoreState();          // devuelve el color/opacidad anteriores (JUCE usa el alfa actual al pintar imagenes)
        return;
    }
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
    g.setOpacity (1.0f);
    g.drawImage (img, area, juce::RectanglePlacement::centred);
}

void LibreSkin::drawLogoPlugin (juce::Graphics& g, juce::Rectangle<float> r) const  { drawFitted (g, logoPlugin, r); }
void LibreSkin::drawLogoCreator (juce::Graphics& g, juce::Rectangle<float> r) const { drawFitted (g, logoCreator, r); }

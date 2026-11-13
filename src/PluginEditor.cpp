#include "PluginEditor.h"
#include "EditorLayout.h"

using namespace ffl;

namespace
{
    // Paleta LibreFilter (pack grafico)
    const juce::Colour textLight  { 0xfff2f8ff };
    const juce::Colour textDark   { 0xff0e2a4a };
    const juce::Colour boxFill    { 0xff010510 };
    const juce::Colour boxLine    { 0xff7ed0ff };
    const juce::Colour gridLine   { 0xff2b3f5c };

    juce::Font uiFont (float h, bool bold = false, bool italic = false)
    {
        int style = juce::Font::plain;
        if (bold)   style |= juce::Font::bold;
        if (italic) style |= juce::Font::italic;
        return juce::Font (juce::FontOptions (h, style));
    }

    juce::String formatHz (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + "k" : juce::String (juce::roundToInt (hz));
    }
}

//==============================================================================
FreeFilterEditor::FreeFilterEditor (FreeFilterProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    for (int i = 0; i < kFftSize; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (kFftSize - 1)));

    specIn.fill (-120.0f);
    specOut.fill (-120.0f);
    startValues.fill (0.0f);

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) designW / designH);
    setResizeLimits (designW / 2, designH / 2, designW * 2, designH * 2);
    setSize (designW * 4 / 5, designH * 4 / 5);

    presetName.setMultiLine (false);
    presetName.setReturnKeyStartsNewLine (false);
    presetName.setScrollbarsShown (false);
    presetName.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff061426));
    presetName.setColour (juce::TextEditor::textColourId, textLight);
    presetName.setColour (juce::TextEditor::outlineColourId, boxLine.withAlpha (0.45f));
    presetName.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xff3df0ff));
    presetName.setColour (juce::TextEditor::highlightColourId, juce::Colour (0xff1e5fb4));
    presetName.setTextToShowWhenEmpty ("preset name", juce::Colour (0xff7f95ad));
    presetName.setIndents (8, 0);
    presetName.setInputRestrictions (40);
    addAndMakeVisible (presetName);
    refreshPresets();

    startTimerHz (30);
}

FreeFilterEditor::~FreeFilterEditor() { stopTimer(); }

juce::AffineTransform FreeFilterEditor::designToScreen() const
{
    return juce::AffineTransform::scale ((float) getWidth() / designW, (float) getHeight() / designH);
}

juce::Point<float> FreeFilterEditor::toDesign (juce::Point<float> p) const
{
    return p.transformedBy (designToScreen().inverted());
}

bool FreeFilterEditor::isLogAxis() const
{
    return processor.isThirdOctave();   // Log = tercios de octava, Lin = bandas iguales
}

void FreeFilterEditor::setStatus (const juce::String& s)
{
    statusText = s;
    statusUntil = juce::Time::currentTimeMillis() + 3500;
}

juce::String FreeFilterEditor::idleStatus() const
{
    const bool d = processor.learnedFrames (true) > 0, s = processor.learnedFrames (false) > 0;
    if (processor.isLearnOn()) return processor.isLearnDest() ? "Learning destination..." : "Learning source...";
    if (d && s) return "Source and Destination learned.";
    if (d) return "Destination learned.";
    if (s) return "Source learned.";
    return {};
}

void FreeFilterEditor::setMorphFromX (float x)
{
    const float t = juce::jlimit (0.0f, 1.0f, (x - morphBar.getX()) / morphBar.getWidth());
    if (auto* p = processor.apvts.getParameter ("morph"))
        p->setValueNotifyingHost (p->convertTo0to1 (t * 200.0f));
}

float FreeFilterEditor::axisMax() const
{
    return juce::jmin (24000.0f, (float) processor.getSampleRate_() * 0.5f);   // mismo maximo en Lin y Log
}

float FreeFilterEditor::freqToX (float hz) const
{
    const float hi = axisMax();
    if (isLogAxis())
    {
        const float lo = 20.0f;
        const float t = std::log (juce::jmax (lo, hz) / lo) / std::log (hi / lo);
        return plotRect.getX() + juce::jlimit (0.0f, 1.0f, t) * plotRect.getWidth();
    }
    return plotRect.getX() + juce::jlimit (0.0f, 1.0f, hz / hi) * plotRect.getWidth();
}

float FreeFilterEditor::xToFreq (float x) const
{
    const float hi = axisMax();
    const float t = juce::jlimit (0.0f, 1.0f, (x - plotRect.getX()) / plotRect.getWidth());
    if (isLogAxis())
    {
        const float lo = 20.0f;
        return lo * std::pow (hi / lo, t);
    }
    return t * hi;
}

float FreeFilterEditor::dbToY (float db) const
{
    return plotRect.getY() + (dbTop - juce::jlimit (dbBottom, dbTop, db)) / (dbTop - dbBottom) * plotRect.getHeight();
}

int FreeFilterEditor::bandAt (float x) const
{
    return juce::jlimit (0, ff::kBands - 1, (int) std::floor ((x - boardRect.getX()) / bandW));
}

float FreeFilterEditor::valueAt (float y) const
{
    const float t = juce::jlimit (0.0f, 1.0f, (y - boardRect.getY()) / boardRect.getHeight());
    return 15.0f - t * 30.0f;
}

void FreeFilterEditor::setBand (int b, float dB)
{
    if (auto* prm = processor.apvts.getParameter (processor.activeBandId (b)))
    {
        if (touched.insert (b).second) prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (juce::jlimit (-15.0f, 15.0f, dB)));
    }
}

void FreeFilterEditor::resetAll() { processor.requestReset(); }

//==============================================================================
void FreeFilterEditor::resized() { rebuildBackground(); layoutPresetEditor(); }

// Parte estatica de la interfaz (panel, marcos, pistas de los sliders), dibujada una vez por tamano.
void FreeFilterEditor::rebuildBackground()
{
    const float scale = juce::Component::getApproximateScaleFactorForComponent (this);
    const int pw = juce::jmax (1, juce::roundToInt (getWidth() * scale));
    const int ph = juce::jmax (1, juce::roundToInt (getHeight() * scale));
    background = juce::Image (juce::Image::RGB, pw, ph, true);
    juce::Graphics g (background);

    g.fillAll (juce::Colour (0xff030c1a));
    skin.drawBackground (g, { 0.0f, 0.0f, (float) pw, (float) ph });

    g.addTransform (juce::AffineTransform::scale ((float) pw / designW, (float) ph / designH));

    // fila de lecturas
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillRoundedRectangle (stripRect, 8.0f);

    // analizador: marco y fondo negro de la zona de curvas
    skin.drawGraphFrame (g, analyzerRect);
    g.setColour (juce::Colour (0xff01030a));
    g.fillRect (plotRect);

    // tablero de sliders
    g.setColour (juce::Colour (0xff071428));
    g.fillRoundedRectangle (boardRect.expanded (10.0f, 12.0f), 10.0f);
    g.setColour (juce::Colour (0xff3da8ff).withAlpha (0.55f));
    g.drawRoundedRectangle (boardRect.expanded (10.0f, 12.0f), 10.0f, 2.0f);
    for (int b = 0; b < ff::kBands; ++b)
        skin.drawSliderTrack (g, { boardRect.getX() + (float) b * bandW + 2.0f, boardRect.getY(), bandW - 4.0f, boardRect.getHeight() });
}

void FreeFilterEditor::drawButton (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool on, bool enabled)
{
    const auto state = ! enabled ? LibreSkin::ButtonState::disabled
                                 : on ? LibreSkin::ButtonState::active : LibreSkin::ButtonState::normal;
    skin.drawButton (g, r, state);
    if (text.isEmpty()) return;

    g.setColour (! enabled ? juce::Colour (0xff2b4461) : on ? juce::Colour (0xff04200c) : textDark);
    g.setFont (uiFont (r.getHeight() * 0.44f, true));
    g.drawText (text, r.reduced (4.0f, 0.0f), juce::Justification::centred, false);
}

void FreeFilterEditor::paintButtons (juce::Graphics& g)
{
    // fila superior: Source/Dest (selector), Learn y Match
    drawButton (g, buttonRect (Source), "Source", ! processor.isLearnDest());
    drawButton (g, buttonRect (Dest),   "Dest.",  processor.isLearnDest());
    drawButton (g, buttonRect (Learn),  "Learn",  processor.isLearnOn());
    drawButton (g, buttonRect (Match),  "Match",  processor.isMatchOn(), processor.matchAvailable() || processor.isMatchOn());
    // presets de curvas aprendidas
    drawButton (g, buttonRect (PresetPrev), "<", false, ! presetFiles.empty());
    drawButton (g, buttonRect (PresetNext), ">", false, ! presetFiles.empty());
    drawButton (g, buttonRect (PresetSave), "Save", false, processor.learnedFrames (processor.isLearnDest()) > 0);
    drawButton (g, buttonRect (PresetDel),  "Del",  juce::Time::currentTimeMillis() < delArmedUntil, presetIndex >= 0);

    // modos de dibujo
    const DrawMode modes[3] = { DrawMode::freehand, DrawMode::level, DrawMode::line };
    for (int i = 0; i < 3; ++i)
    {
        const auto r = buttonRect (ModeFree + i);
        drawButton (g, r, "", drawMode == modes[i]);
        g.setColour (juce::Colour (0xff04200c).interpolatedWith (textDark, drawMode == modes[i] ? 0.0f : 1.0f));
        juce::Path p;
        const auto c = r.reduced (16.0f, 12.0f);
        const float lw = 3.2f;
        if (i == 0)
        {
            p.startNewSubPath (c.getX(), c.getBottom());
            p.cubicTo (c.getX() + c.getWidth() * 0.3f, c.getY() - 5, c.getX() + c.getWidth() * 0.6f, c.getBottom() + 5, c.getRight(), c.getY());
            g.strokePath (p, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (i == 1)
        {
            g.drawLine (c.getX(), c.getCentreY(), c.getRight(), c.getCentreY(), lw);
        }
        else
        {
            g.drawLine (c.getX(), c.getBottom(), c.getRight(), c.getY(), lw);
            g.fillEllipse (c.getX() - 4, c.getBottom() - 4, 8, 8);
            g.fillEllipse (c.getRight() - 4, c.getY() - 4, 8, 8);
        }
    }

    g.setColour (textLight);
    g.setFont (uiFont (24.0f, true));
    g.drawText ("Global", globalLabel, juce::Justification::centredRight, false);

    const bool frozen = processor.apvts.getRawParameterValue ("freeze")->load() > 0.5f;
    drawButton (g, buttonRect (Reset),  "Reset",  false);
    drawButton (g, buttonRect (Freeze), "Freeze", frozen);
    drawButton (g, buttonRect (Lin),    "Lin",    ! isLogAxis());
    drawButton (g, buttonRect (Log),    "Log",    isLogAxis());
}

void FreeFilterEditor::paintBands (juce::Graphics& g)
{
    const float cy = boardRect.getCentreY();
    for (int b = 0; b < ff::kBands; ++b)
    {
        const float x = boardRect.getX() + (float) b * bandW;
        const float v = processor.getBandValue (b);
        const float vy = boardRect.getY() + (15.0f - v) / 30.0f * boardRect.getHeight();

        // relleno desde 0 dB hasta el valor
        g.setColour (juce::Colour (0xff2aa8ff).withAlpha (0.45f));
        g.fillRect (x + 5.0f, juce::jmin (vy, cy), bandW - 10.0f, std::abs (vy - cy));

        // marcador de valor
        skin.drawSliderThumb (g, { x + 1.5f, vy - 7.0f, bandW - 3.0f, 14.0f });

        // "LED" superior
        skin.drawLed (g, { x + (bandW - 20.0f) * 0.5f, boardRect.getY() - 28.0f, 20.0f, 20.0f }, std::abs (v) > 0.05f);
    }

    // linea de 0 dB
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.drawHorizontalLine ((int) cy, boardRect.getX(), boardRect.getRight());

    // escala dB a la derecha
    g.setFont (uiFont (21.0f, true));
    g.setColour (textLight);
    g.drawText ("dB", boardScale, juce::Justification::centredLeft, false);
    const int marks[7] = { 15, 10, 5, 0, -5, -10, -15 };
    for (int m : marks)
    {
        const float y = boardRect.getY() + (15.0f - (float) m) / 30.0f * boardRect.getHeight();
        g.drawText (juce::String (m), juce::Rectangle<float> (scaleColumn.getX() + 8.0f, y - 12.0f, 62.0f, 24.0f), juce::Justification::centredLeft, false);
        g.drawHorizontalLine ((int) y, boardRect.getRight() + 12.0f, boardRect.getRight() + 24.0f);
    }

    // cuadro de valor
    g.setColour (boxFill);
    g.fillRoundedRectangle (valueBox, 4.0f);
    g.setColour (boxLine.withAlpha (0.7f));
    g.drawRoundedRectangle (valueBox, 4.0f, 2.0f);
    g.setColour (textLight);
    g.setFont (uiFont (25.0f, true));
    g.drawText (readDb, valueBox, juce::Justification::centred, false);
}

void FreeFilterEditor::paintAnalyzer (juce::Graphics& g)
{
    g.saveState();
    g.reduceClipRegion (plotRect.toNearestInt());

    // rejilla
    g.setColour (gridLine);
    for (int d = -80; d <= 20; d += 20)
        g.drawHorizontalLine ((int) dbToY ((float) d), plotRect.getX(), plotRect.getRight());
    const float nyq = (float) processor.getSampleRate_() * 0.5f;
    const float axMax = axisMax();
    if (isLogAxis())
    {
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
            if (f < axMax) g.drawVerticalLine ((int) freqToX (f), plotRect.getY(), plotRect.getBottom());
    }
    else
    {
        for (float f = 5000.0f; f < axMax; f += 5000.0f)
            g.drawVerticalLine ((int) freqToX (f), plotRect.getY(), plotRect.getBottom());
    }

    auto drawSpec = [&] (const std::array<float, kFftSize / 2>& spec, juce::Colour c)
    {
        juce::Path p;
        bool started = false;
        for (float x = plotRect.getX(); x <= plotRect.getRight(); x += 1.5f)
        {
            const float f = xToFreq (x);
            const float bin = f / nyq * (float) (kFftSize / 2 - 1);
            const int i0 = juce::jlimit (0, kFftSize / 2 - 2, (int) bin);
            const float t = bin - (float) i0;
            const float db = spec[(size_t) i0] * (1.0f - t) + spec[(size_t) i0 + 1] * t;
            const float y = dbToY (db);
            if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (2.0f));
    };
    drawSpec (specIn,  juce::Colour (0xff3cd23c));
    drawSpec (specOut, juce::Colour (0xffe53935));

    // curva del filtro (amarilla)
    {
        juce::Path p;
        bool started = false;
        for (float x = plotRect.getX(); x <= plotRect.getRight(); x += 3.0f)
        {
            const float y = dbToY (processor.responseDbAt (xToFreq (x)));
            if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
        }
        g.setColour (juce::Colour (0xffffe600));
        g.strokePath (p, juce::PathStrokeType (3.2f));
    }
    g.restoreState();

    // etiquetas
    g.setFont (uiFont (21.0f, true));
    g.setColour (textLight);
    g.drawText ("dB", juce::Rectangle<float> (scaleColumn.getX() + 8.0f, plotRect.getY() - 2.0f, 62.0f, 26.0f), juce::Justification::centredLeft, false);
    for (int d = 0; d >= -60; d -= 20)   // el +20 y el -80 quedan para las etiquetas 'dB' y 'kHz'
        g.drawText (juce::String (d), juce::Rectangle<float> (scaleColumn.getX() + 8.0f, dbToY ((float) d) - 13.0f, 62.0f, 26.0f),
                    juce::Justification::centredLeft, false);
    const float axisY = plotRect.getBottom() - 28.0f;
    g.drawText ("kHz", juce::Rectangle<float> (scaleColumn.getX() + 8.0f, axisY, 62.0f, 26.0f), juce::Justification::centredLeft, false);
    g.setColour (textLight.withAlpha (0.65f));
    if (isLogAxis())
    {
        for (float f : { 100.0f, 1000.0f, 10000.0f })
            if (f < axMax)
                g.drawText (juce::String (f / 1000.0f, f < 1000.0f ? 1 : 0), juce::Rectangle<float> (freqToX (f) + 4.0f, axisY, 60.0f, 26.0f),
                            juce::Justification::centredLeft, false);
    }
    else
    {
        for (float f = 5000.0f; f < axMax; f += 5000.0f)
            g.drawText (juce::String (f / 1000.0f, 0), juce::Rectangle<float> (freqToX (f) + 4.0f, axisY, 60.0f, 26.0f),
                        juce::Justification::centredLeft, false);
    }

    // leyenda
    g.setFont (uiFont (22.0f, true));
    const struct { const char* name; juce::Colour c; float x; } legend[3] = {
        { "Input", juce::Colour (0xff3cd23c), 283.0f }, { "Output", juce::Colour (0xffe53935), 414.0f }, { "Filter", juce::Colour (0xffffe600), 560.0f } };
    for (auto& l : legend)
    {
        g.setColour (l.c);
        g.fillRect (l.x, 555.0f, 34.0f, 4.0f);
        g.drawText (l.name, juce::Rectangle<float> (l.x + 44.0f, 542.0f, 110.0f, 30.0f), juce::Justification::centredLeft, false);
    }
}

void FreeFilterEditor::paint (juce::Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());
    if (showAbout) { paintAbout (g); return; }

    g.addTransform (designToScreen());

    LibreSkin::drawLogoLibreFilter (g, logoRect);

    // fila de lecturas (Morph / Hz / dB)
    {
        g.setColour (textLight);
        g.setFont (uiFont (23.0f, true));
        g.drawText ("Morph %", morphLabel, juce::Justification::centredRight, false);
        g.drawText ("Hz", hzLabel, juce::Justification::centredRight, false);
        g.drawText ("dB", dbLabel, juce::Justification::centredRight, false);

        auto box = [&] (juce::Rectangle<float> r, const juce::String& txt, bool dim)
        {
            g.setColour (boxFill);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (boxLine.withAlpha (0.7f));
            g.drawRoundedRectangle (r, 4.0f, 2.0f);
            g.setColour (textLight.withAlpha (dim ? 0.4f : 1.0f));
            g.setFont (uiFont (25.0f, true));
            g.drawText (txt, r, juce::Justification::centred, false);
        };
        // Morph 0..200 %
        const float morphPct = processor.apvts.getRawParameterValue ("morph")->load();
        const bool morphOn = processor.isMatchOn();
        g.setColour (juce::Colour (0xff0a1424));
        g.fillRect (morphBar);
        g.setColour (juce::Colour (morphOn ? 0xff6aa6e8 : 0xff4a5f7a));
        g.fillRect (morphBar.withWidth (morphBar.getWidth() * morphPct / 200.0f));
        g.setColour (boxLine.withAlpha (0.5f));
        g.drawRect (morphBar, 2.0f);
        g.drawVerticalLine ((int) (morphBar.getX() + morphBar.getWidth() * 0.5f), morphBar.getY(), morphBar.getBottom());
        box (morphBox, juce::String (juce::roundToInt (morphPct)), ! morphOn);
        box (hzBox, readHz, false);
        box (dbBox, readDb, false);
    }

    paintAnalyzer (g);
    paintBands (g);
    paintButtons (g);

    // estado (Learn/Match/presets): dentro del analizador, arriba a la izquierda
    {
        juce::String msg = juce::Time::currentTimeMillis() < statusUntil ? statusText : idleStatus();
        if (msg.isNotEmpty())
        {
            g.setFont (uiFont (21.0f, false, true));
            g.setColour (textLight.withAlpha (0.92f));
            g.drawText (msg, statusRect, juce::Justification::centredLeft, false);
        }
    }

    // credito y version en la esquina inferior derecha
    LibreSkin::drawLogoVooD (g, voodRect);
    g.setFont (uiFont (15.0f));
    g.setColour (textLight.withAlpha (0.6f));
    g.drawText (juce::String ("v") + JucePlugin_VersionString, versionRect, juce::Justification::centredRight, false);
}

void FreeFilterEditor::paintAbout (juce::Graphics& g)
{
    skin.drawCredits (g, getLocalBounds().toFloat());
    g.addTransform (designToScreen());

    LibreSkin::drawLogoLibreFilter (g, juce::Rectangle<float> (190.0f, 70.0f, 560.0f, 114.0f));

    const juce::Rectangle<float> panel (90.0f, 230.0f, 760.0f, 380.0f);
    g.setColour (juce::Colour (0xff030c1a).withAlpha (0.55f));
    g.fillRoundedRectangle (panel, 18.0f);
    g.setColour (boxLine.withAlpha (0.45f));
    g.drawRoundedRectangle (panel, 18.0f, 2.0f);

    g.setFont (uiFont (26.0f));
    g.setColour (textLight);
    g.drawFittedText (juce::String::fromUTF8 (
                          "Algoritmo original: ME FreeFilter (Steinberg Mastering Edition)\n"
                          "\xC2\xA9 1998-2001 Spectral Design GmbH.\n\n"
                          "Reimplementaci\xC3\xB3n en JUCE para VST3 / AU,\n"
                          "Windows x64 y macOS Apple Silicon.\n"
                          "Bandas iguales, Learn / Match / Morph y tercios de octava.\n"
                          "Contrastado con el original (ver docs/ANALISIS.md).\n\n"
                          "Clic para cerrar."),
                      panel.reduced (24.0f).toNearestInt(), juce::Justification::centred, 12);

    LibreSkin::drawLogoVooD (g, juce::Rectangle<float> (0.0f, 0.0f, 205.0f, 65.0f).withCentre ({ designW * 0.5f + 4.0f, 690.0f }));
    g.setFont (uiFont (15.0f));
    g.setColour (textLight.withAlpha (0.6f));
    g.drawText (juce::String ("v") + JucePlugin_VersionString, versionRect, juce::Justification::centredRight, false);
}

//==============================================================================
static juce::File presetsFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("FreeFilter").getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

static juce::String sanitizeName (juce::String s)
{
    s = s.trim();
    juce::String out;
    for (auto ch : s) out += (juce::CharacterFunctions::isLetterOrDigit (ch) || ch == ' ' || ch == '-' || ch == '_' || ch == '.') ? juce::String::charToString (ch) : juce::String ("_");
    return out.trim();
}

void FreeFilterEditor::refreshPresets (const juce::String& selectName)
{
    presetFiles.clear();
    for (const auto& f : presetsFolder().findChildFiles (juce::File::findFiles, false, "*.ffcurve"))
        presetFiles.push_back (f);
    std::sort (presetFiles.begin(), presetFiles.end(),
               [] (const juce::File& a, const juce::File& b) { return a.getFileNameWithoutExtension().compareIgnoreCase (b.getFileNameWithoutExtension()) < 0; });

    presetIndex = presetFiles.empty() ? -1 : juce::jlimit (0, (int) presetFiles.size() - 1, presetIndex);
    if (selectName.isNotEmpty())
        for (size_t i = 0; i < presetFiles.size(); ++i)
            if (presetFiles[i].getFileNameWithoutExtension() == selectName) presetIndex = (int) i;
}

// Recorre los presets y carga el elegido en el lado seleccionado (Source o Dest.).
void FreeFilterEditor::presetStep (int delta)
{
    refreshPresets();
    if (presetFiles.empty()) { setStatus ("No presets yet. Learn a curve and press Save."); return; }
    presetIndex = (presetIndex < 0 ? (delta > 0 ? 0 : (int) presetFiles.size() - 1)
                                   : (presetIndex + delta + (int) presetFiles.size()) % (int) presetFiles.size());
    const auto& f = presetFiles[(size_t) presetIndex];
    presetName.setText (f.getFileNameWithoutExtension(), false);
    const bool dest = processor.isLearnDest();
    double fileSr = 0.0;
    if (processor.loadLearned (dest, f, &fileSr))
    {
        juce::String msg = juce::String (dest ? "Dest. " : "Source ") + "loaded: " + f.getFileNameWithoutExtension();
        if (fileSr > 0.0 && std::abs (fileSr - processor.getSampleRate_()) > 1.0)
            msg += " (adapted from " + juce::String (juce::roundToInt (fileSr)) + " Hz)";
        setStatus (msg);
    }
    else
        setStatus ("Not a valid FreeFilter preset.");
}

// Guarda la curva aprendida del lado seleccionado con el nombre escrito (o uno automatico).
void FreeFilterEditor::presetSave()
{
    const bool dest = processor.isLearnDest();
    if (processor.learnedFrames (dest) <= 0)
    {
        setStatus (dest ? "Nothing learned in Dest. yet." : "Nothing learned in Source yet.");
        return;
    }
    auto name = sanitizeName (presetName.getText());
    if (name.isEmpty())
    {
        refreshPresets();
        name = juce::String (dest ? "Dest " : "Source ") + juce::String ((int) presetFiles.size() + 1).paddedLeft ('0', 2);
    }
    const auto file = presetsFolder().getChildFile (name + ".ffcurve");
    const bool existed = file.existsAsFile();
    if (processor.saveLearned (dest, file))
    {
        refreshPresets (name);
        presetName.setText (name, false);
        setStatus ((existed ? "Preset overwritten: " : "Preset saved: ") + name);
    }
    else
        setStatus ("Could not save the preset.");
}

// Borrar pide una segunda pulsacion (3,5 s) para evitar accidentes; no hay dialogos.
void FreeFilterEditor::presetDelete()
{
    if (presetIndex < 0 || presetIndex >= (int) presetFiles.size()) { setStatus ("No preset selected."); return; }
    const auto f = presetFiles[(size_t) presetIndex];
    if (juce::Time::currentTimeMillis() >= delArmedUntil)
    {
        delArmedUntil = juce::Time::currentTimeMillis() + 3500;
        setStatus ("Press Del again to delete: " + f.getFileNameWithoutExtension());
        return;
    }
    delArmedUntil = 0;
    const auto n = f.getFileNameWithoutExtension();
    setStatus (f.deleteFile() ? "Preset deleted: " + n : "Could not delete the preset.");
    refreshPresets();
    presetName.setText (presetIndex >= 0 ? presetFiles[(size_t) presetIndex].getFileNameWithoutExtension() : juce::String(), false);
}

void FreeFilterEditor::layoutPresetEditor()
{
    const auto r = presetField.transformedBy (designToScreen()).toNearestInt();
    presetName.setBounds (r);
    presetName.applyFontToAllText (juce::Font (juce::FontOptions ((float) r.getHeight() * 0.62f)));
}

//==============================================================================
void FreeFilterEditor::timerCallback()
{
    const bool frozen = processor.apvts.getRawParameterValue ("freeze")->load() > 0.5f;
    if (! frozen)
    {
        auto update = [&] (std::array<float, kFftSize / 2>& spec, bool output)
        {
            processor.readAnalyzer (sampBuf.data(), kFftSize, output);
            for (int i = 0; i < kFftSize; ++i) fftBuf[(size_t) i] = sampBuf[(size_t) i] * window[(size_t) i];
            for (int i = kFftSize; i < kFftSize * 2; ++i) fftBuf[(size_t) i] = 0.0f;
            fft.performFrequencyOnlyForwardTransform (fftBuf.data());
            for (int k = 0; k < kFftSize / 2; ++k)
            {
                const float db = 20.0f * std::log10 (fftBuf[(size_t) k] / (kFftSize * 0.25f) + 1.0e-9f);
                spec[(size_t) k] = juce::jmax (db, spec[(size_t) k] - 1.5f);   // ataque inmediato, caída suave
            }
        };
        update (specIn, false);
        update (specOut, true);
    }
    repaint();
}

//==============================================================================
void FreeFilterEditor::mouseMove (const juce::MouseEvent& e)
{
    hover = toDesign (e.position);
    readHz.clear();
    readDb.clear();
    if (boardRect.expanded (0.0f, 16.0f).contains (hover))
    {
        const int b = bandAt (hover.x);
        const float centre = processor.isThirdOctave() ? (float) ff::kThirdOctave[b]
                                                       : (float) ((b + 0.5) * processor.getSampleRate_() / 60.0);
        readHz = formatHz (centre);
        readDb = juce::String (processor.getBandValue (b), 1);
    }
    else if (plotRect.contains (hover))
    {
        const float f = xToFreq (hover.x);
        readHz = formatHz (f);
        readDb = juce::String (processor.responseDbAt (f), 1);
    }
}

void FreeFilterEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto p = toDesign (e.position);

    if (showAbout) { showAbout = false; presetName.setVisible (true); repaint(); return; }
    if (aboutRect.contains (p)) { showAbout = true; presetName.setVisible (false); repaint(); return; }

    // botones
    for (int id = 0; id < NumButtons; ++id)
    {
        if (! buttonRect (id).contains (p)) continue;
        switch (id)
        {
            case ModeFree:  drawMode = DrawMode::freehand; break;
            case ModeLevel: drawMode = DrawMode::level;    break;
            case ModeLine:  drawMode = DrawMode::line;     break;
            case Reset:     resetAll();                    break;
            case Freeze:
                if (auto* prm = processor.apvts.getParameter ("freeze"))
                    prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
                break;
            case Lin: if (auto* prm = processor.apvts.getParameter ("thirds")) prm->setValueNotifyingHost (0.0f); break;
            case Log: if (auto* prm = processor.apvts.getParameter ("thirds")) prm->setValueNotifyingHost (1.0f); break;
            case Source: if (auto* prm = processor.apvts.getParameter ("learnDest")) prm->setValueNotifyingHost (0.0f); break;
            case Dest:   if (auto* prm = processor.apvts.getParameter ("learnDest")) prm->setValueNotifyingHost (1.0f); break;
            case Learn:
                if (auto* prm = processor.apvts.getParameter ("learn"))
                {
                    const bool turnOn = prm->getValue() < 0.5f;
                    if (turnOn)
                        if (auto* m = processor.apvts.getParameter ("matchOn")) m->setValueNotifyingHost (0.0f);
                    prm->setValueNotifyingHost (turnOn ? 1.0f : 0.0f);
                }
                break;
            case Match:
                if (processor.isLearnOn())                       setStatus ("Disabled. PlugIn is in learn mode.");
                else if (! processor.matchAvailable() && ! processor.isMatchOn())
                                                                 setStatus ("Disabled. Learn both source and destination data !");
                else if (auto* prm = processor.apvts.getParameter ("matchOn"))
                    prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
                break;
            case PresetPrev: presetStep (-1); break;
            case PresetNext: presetStep (+1); break;
            case PresetSave: presetSave();    break;
            case PresetDel:  presetDelete();  break;
            default: break;
        }
        repaint();
        return;
    }

    // barra de Morph
    if (morphBar.expanded (0.0f, 4.0f).contains (p))
    {
        if (processor.isMatchOn()) { morphDragging = true; setMorphFromX (p.x); }
        else setStatus ("Disabled. Activate match button first !");
        return;
    }

    // tablero de sliders
    if (boardRect.expanded (0.0f, 12.0f).contains (p))
    {
        dragStart = p;
        lastBand = -1;
        draggingBoard = true;
        for (int b = 0; b < ff::kBands; ++b) startValues[(size_t) b] = processor.getBandValue (b);
        mouseDrag (e);
    }
}

void FreeFilterEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = toDesign (e.position);
    if (morphDragging) { setMorphFromX (p.x); return; }
    if (showAbout || ! draggingBoard) return;

    const int b = bandAt (p.x);
    const float v = valueAt (p.y);

    switch (drawMode)
    {
        case DrawMode::freehand:
        {
            if (lastBand < 0) { setBand (b, v); }
            else
            {
                const int lo = juce::jmin (lastBand, b), hi = juce::jmax (lastBand, b);
                const float vl = processor.getBandValue (lastBand);
                for (int i = lo; i <= hi; ++i)
                {
                    const float t = hi == lo ? 1.0f : (float) (i - lo) / (float) (hi - lo);
                    const float from = lastBand <= b ? vl : v, to = lastBand <= b ? v : vl;
                    setBand (i, from + t * (to - from));
                }
            }
            break;
        }
        case DrawMode::level:
            for (int i = 0; i < ff::kBands; ++i) setBand (i, v);
            break;
        case DrawMode::line:
        {
            const int b0 = bandAt (dragStart.x);
            const float v0 = valueAt (dragStart.y);
            const int lo = juce::jmin (b0, b), hi = juce::jmax (b0, b);
            // las bandas que ya no quedan bajo la recta vuelven al valor que tenian al empezar el arrastre
            for (int i : std::vector<int> (touched.begin(), touched.end()))
                if (i < lo || i > hi)
                    setBand (i, startValues[(size_t) i]);
            for (int i = lo; i <= hi; ++i)
            {
                const float t = hi == lo ? 1.0f : (float) (i - b0) / (float) (b - b0);
                setBand (i, v0 + t * (v - v0));
            }
            break;
        }
    }
    lastBand = b;
    mouseMove (e);
}

void FreeFilterEditor::mouseUp (const juce::MouseEvent&)
{
    for (int b : touched)
        if (auto* prm = processor.apvts.getParameter (processor.activeBandId (b)))
            prm->endChangeGesture();
    touched.clear();
    lastBand = -1;
    draggingBoard = false;
    morphDragging = false;
}

void FreeFilterEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto p = toDesign (e.position);
    if (! boardRect.expanded (0.0f, 12.0f).contains (p)) return;
    if (drawMode == DrawMode::level)          // solo los sliders; lo aprendido y Match no se tocan (eso es Reset)
        for (int i = 0; i < ff::kBands; ++i) setBand (i, 0.0f);
    else
        setBand (bandAt (p.x), 0.0f);
    mouseUp (e);
}

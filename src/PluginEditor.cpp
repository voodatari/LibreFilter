#include "PluginEditor.h"
#include "EditorLayout.h"

using namespace lfl;

namespace
{
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
LibreFilterEditor::LibreFilterEditor (LibreFilterProcessor& p)
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
    presetName.setIndents (12, 0);
    presetName.setInputRestrictions (40);
    presetName.onReturnKey = [this] { endNameEdit (true); };
    presetName.onEscapeKey = [this] { endNameEdit (false); };
    presetName.onFocusLost = [this] { if (editingName) endNameEdit (true); };
    addChildComponent (presetName);
    applyTheme (processor.isRedTheme());
    refreshPresets();

    startTimerHz (30);
}

LibreFilterEditor::~LibreFilterEditor() { stopTimer(); setLookAndFeel (nullptr); }

juce::AffineTransform LibreFilterEditor::designToScreen() const
{
    return juce::AffineTransform::scale ((float) getWidth() / designW, (float) getHeight() / designH);
}

juce::Point<float> LibreFilterEditor::toDesign (juce::Point<float> p) const
{
    return p.transformedBy (designToScreen().inverted());
}

bool LibreFilterEditor::isLogAxis() const
{
    return processor.isThirdOctave();   // Log = tercios de octava, Lin = bandas iguales
}

void LibreFilterEditor::setStatus (const juce::String& s)
{
    statusText = s;
    statusUntil = juce::Time::currentTimeMillis() + 3500;
}

juce::String LibreFilterEditor::idleStatus() const
{
    const bool d = processor.learnedFrames (true) > 0, s = processor.learnedFrames (false) > 0;
    if (processor.isLearnOn()) return processor.isLearnDest() ? "Learning destination..." : "Learning source...";
    if (d && s) return "Source and Destination learned.";
    if (d) return "Destination learned.";
    if (s) return "Source learned.";
    return {};
}

void LibreFilterEditor::setMorphFromX (float x)
{
    const float t = juce::jlimit (0.0f, 1.0f, (x - morphBar.getX()) / morphBar.getWidth());
    if (auto* p = processor.apvts.getParameter ("morph"))
        p->setValueNotifyingHost (p->convertTo0to1 (t * 200.0f));
}

float LibreFilterEditor::axisMax() const
{
    return juce::jmin (24000.0f, (float) processor.getSampleRate_() * 0.5f);   // mismo maximo en Lin y Log
}

float LibreFilterEditor::freqToX (float hz) const
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

float LibreFilterEditor::xToFreq (float x) const
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

float LibreFilterEditor::dbToY (float db) const
{
    return plotRect.getY() + (dbTop - juce::jlimit (dbBottom, dbTop, db)) / (dbTop - dbBottom) * plotRect.getHeight();
}

int LibreFilterEditor::bandAt (float x) const
{
    return juce::jlimit (0, lf::kBands - 1, (int) std::floor ((x - boardRect.getX()) / bandW));
}

float LibreFilterEditor::valueAt (float y) const
{
    const float t = juce::jlimit (0.0f, 1.0f, (y - boardRect.getY()) / boardRect.getHeight());
    return 15.0f - t * 30.0f;
}

void LibreFilterEditor::setBand (int b, float dB)
{
    if (auto* prm = processor.apvts.getParameter (processor.activeBandId (b)))
    {
        if (touched.insert (b).second) prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (juce::jlimit (-15.0f, 15.0f, dB)));
    }
}

void LibreFilterEditor::resetAll() { processor.requestReset(); clearLeds(); }

//==============================================================================
void LibreFilterEditor::resized() { rebuildBackground(); layoutPresetEditor(); }

// Parte estatica de la interfaz (panel, marcos, pistas de los sliders), dibujada una vez por tamano.
void LibreFilterEditor::rebuildBackground()
{
    const float scale = juce::Component::getApproximateScaleFactorForComponent (this);
    const int pw = juce::jmax (1, juce::roundToInt (getWidth() * scale));
    const int ph = juce::jmax (1, juce::roundToInt (getHeight() * scale));
    background = juce::Image (juce::Image::RGB, pw, ph, true);
    juce::Graphics g (background);

    g.fillAll (pal.panelDark);
    skin.drawBackground (g, { 0.0f, 0.0f, (float) pw, (float) ph });

    g.addTransform (juce::AffineTransform::scale ((float) pw / designW, (float) ph / designH));

    // logos (PNG con transparencia): del plugin arriba a la derecha y del creador abajo a la derecha
    skin.drawLogoPlugin (g, logoRect);
    skin.drawLogoCreator (g, creatorRect);

    // fila de lecturas
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillRoundedRectangle (stripRect, 8.0f);

    // analizador: marco y fondo negro de la zona de curvas
    skin.drawGraphFrame (g, analyzerRect);
    g.setColour (juce::Colour (0xff01030a));
    g.fillRect (plotRect);

    // tablero de sliders
    g.setColour (pal.bed);
    g.fillRoundedRectangle (boardRect.expanded (10.0f, 12.0f), 10.0f);
    g.setColour (pal.bedEdge.withAlpha (0.55f));
    g.drawRoundedRectangle (boardRect.expanded (10.0f, 12.0f), 10.0f, 2.0f);
    for (int b = 0; b < lf::kBands; ++b)
        skin.drawSliderTrack (g, { boardRect.getX() + (float) b * bandW + 2.0f, boardRect.getY(), bandW - 4.0f, boardRect.getHeight() });
}

void LibreFilterEditor::drawButton (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool on, bool enabled)
{
    const auto state = ! enabled ? LibreSkin::ButtonState::disabled
                                 : on ? LibreSkin::ButtonState::active : LibreSkin::ButtonState::normal;
    skin.drawButton (g, r, state);
    if (text.isEmpty()) return;

    g.setColour (! enabled ? juce::Colour (0xff2b4461) : on ? juce::Colour (0xff04200c) : pal.textDark);
    g.setFont (uiFont (r.getHeight() * 0.44f, true));
    g.drawText (text, r.reduced (4.0f, 0.0f), juce::Justification::centred, false);
}

void LibreFilterEditor::paintButtons (juce::Graphics& g)
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
        g.setColour (juce::Colour (0xff04200c).interpolatedWith (pal.textDark, drawMode == modes[i] ? 0.0f : 1.0f));
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

    g.setColour (pal.textLight);
    g.setFont (uiFont (24.0f, true));
    g.drawText ("Global", globalLabel, juce::Justification::centredRight, false);

    const bool frozen = processor.apvts.getRawParameterValue ("freeze")->load() > 0.5f;
    drawButton (g, buttonRect (Reset),  "Reset",  false);
    drawButton (g, buttonRect (Freeze), "Freeze", frozen);
    drawButton (g, buttonRect (Lin),    "Lin",    ! isLogAxis());
    drawButton (g, buttonRect (Log),    "Log",    isLogAxis());
}

void LibreFilterEditor::paintBands (juce::Graphics& g)
{
    const float cy = boardRect.getCentreY();
    for (int b = 0; b < lf::kBands; ++b)
    {
        const float x = boardRect.getX() + (float) b * bandW;
        const float v = processor.getBandValue (b);
        const float vy = boardRect.getY() + (15.0f - v) / 30.0f * boardRect.getHeight();

        // relleno desde 0 dB hasta el valor
        g.setColour (pal.sliderFill.withAlpha (0.45f));
        g.fillRect (x + 5.0f, juce::jmin (vy, cy), bandW - 10.0f, std::abs (vy - cy));

        // marcador de valor
        skin.drawSliderThumb (g, { x + 1.5f, vy - 7.0f, bandW - 3.0f, 14.0f });

        // "LED" superior
        skin.drawLed (g, { x + (bandW - ledSize) * 0.5f, boardRect.getY() - ledOffset, ledSize, ledSize }, led[(size_t) b], (b / 5) % 2 == 1);
    }

    // linea de 0 dB
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.drawHorizontalLine ((int) cy, boardRect.getX(), boardRect.getRight());

    // escala dB a la derecha
    g.setFont (uiFont (21.0f, true));
    g.setColour (pal.textLight);
    g.drawText ("dB", boardScale, juce::Justification::centredLeft, false);
    const int marks[7] = { 15, 10, 5, 0, -5, -10, -15 };
    for (int m : marks)
    {
        const float y = boardRect.getY() + (15.0f - (float) m) / 30.0f * boardRect.getHeight();
        g.drawText (juce::String (m), juce::Rectangle<float> (scaleColumn.getX() + 8.0f, y - 12.0f, 62.0f, 24.0f), juce::Justification::centredLeft, false);
        g.drawHorizontalLine ((int) y, boardRect.getRight() + 12.0f, boardRect.getRight() + 24.0f);
    }

    // cuadro de valor
    g.setColour (pal.boxFill);
    g.fillRoundedRectangle (valueBox, 4.0f);
    g.setColour (pal.boxLine.withAlpha (0.7f));
    g.drawRoundedRectangle (valueBox, 4.0f, 2.0f);
    g.setColour (pal.textLight);
    g.setFont (uiFont (25.0f, true));
    g.drawText (readDb, valueBox, juce::Justification::centred, false);
}

void LibreFilterEditor::paintAnalyzer (juce::Graphics& g)
{
    g.saveState();
    g.reduceClipRegion (plotRect.toNearestInt());

    // rejilla
    g.setColour (pal.gridLine);
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
    g.setColour (pal.textLight);
    g.drawText ("dB", juce::Rectangle<float> (scaleColumn.getX() + 8.0f, plotRect.getY() - 2.0f, 62.0f, 26.0f), juce::Justification::centredLeft, false);
    for (int d = 0; d >= -60; d -= 20)   // el +20 y el -80 quedan para las etiquetas 'dB' y 'kHz'
        g.drawText (juce::String (d), juce::Rectangle<float> (scaleColumn.getX() + 8.0f, dbToY ((float) d) - 13.0f, 62.0f, 26.0f),
                    juce::Justification::centredLeft, false);
    const float axisY = plotRect.getBottom() - 28.0f;
    g.drawText ("kHz", juce::Rectangle<float> (scaleColumn.getX() + 8.0f, axisY, 62.0f, 26.0f), juce::Justification::centredLeft, false);
    g.setColour (pal.textLight.withAlpha (0.65f));
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
        g.fillRect (l.x, legendLineY, 34.0f, 4.0f);
        g.drawText (l.name, juce::Rectangle<float> (l.x + 44.0f, legendTextY, 110.0f, legendTextH), juce::Justification::centredLeft, false);
    }
}

void LibreFilterEditor::paint (juce::Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());
    if (showAbout) { paintAbout (g); return; }

    g.addTransform (designToScreen());

    // fila de lecturas (Morph / Hz / dB)
    {
        g.setColour (pal.textLight);
        g.setFont (uiFont (23.0f, true));
        g.drawText ("Morph %", morphLabel, juce::Justification::centredRight, false);
        g.drawText ("Hz", hzLabel, juce::Justification::centredRight, false);
        g.drawText ("dB", dbLabel, juce::Justification::centredRight, false);

        auto box = [&] (juce::Rectangle<float> r, const juce::String& txt, bool dim)
        {
            g.setColour (pal.boxFill);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (pal.boxLine.withAlpha (0.7f));
            g.drawRoundedRectangle (r, 4.0f, 2.0f);
            g.setColour (pal.textLight.withAlpha (dim ? 0.4f : 1.0f));
            g.setFont (uiFont (25.0f, true));
            g.drawText (txt, r, juce::Justification::centred, false);
        };
        // Morph 0..200 %
        const float morphPct = processor.apvts.getRawParameterValue ("morph")->load();
        const bool morphOn = processor.isMatchOn();
        g.setColour (pal.morphBg);
        g.fillRect (morphBar);
        g.setColour (morphOn ? pal.morphOn : pal.morphOff);
        g.fillRect (morphBar.withWidth (morphBar.getWidth() * morphPct / 200.0f));
        g.setColour (pal.boxLine.withAlpha (0.5f));
        g.drawRect (morphBar, 2.0f);
        g.drawVerticalLine ((int) (morphBar.getX() + morphBar.getWidth() * 0.5f), morphBar.getY(), morphBar.getBottom());
        box (morphBox, juce::String (juce::roundToInt (morphPct)), ! morphOn);
        box (hzBox, readHz, false);
        box (dbBox, readDb, false);
    }

    paintAnalyzer (g);
    paintBands (g);
    paintButtons (g);
    paintPresetField (g);

    // estado (Learn/Match/presets): dentro del analizador, arriba a la izquierda
    {
        juce::String msg = juce::Time::currentTimeMillis() < statusUntil ? statusText : idleStatus();
        if (msg.isNotEmpty())
        {
            g.setFont (uiFont (21.0f, false, true));
            g.setColour (pal.textLight.withAlpha (0.92f));
            g.drawText (msg, statusRect, juce::Justification::centredLeft, false);
        }
    }

    // version bajo el credito (el logo del creador esta en el fondo)
    g.setFont (uiFont (13.0f));
    g.setColour (pal.textLight.withAlpha (0.6f));
    g.drawText (juce::String ("v") + JucePlugin_VersionString, versionRect, juce::Justification::centredRight, false);
}

void LibreFilterEditor::paintAbout (juce::Graphics& g)
{
    skin.drawCredits (g, getLocalBounds().toFloat());
    g.addTransform (designToScreen());

    skin.drawLogoPlugin (g, juce::Rectangle<float> (170.0f, 40.0f, 600.0f, 180.0f));

    const juce::Rectangle<float> panel (90.0f, 250.0f, 760.0f, 380.0f);
    g.setColour (pal.panelDark.withAlpha (0.55f));
    g.fillRoundedRectangle (panel, 18.0f);
    g.setColour (pal.boxLine.withAlpha (0.45f));
    g.drawRoundedRectangle (panel, 18.0f, 2.0f);

    g.setFont (uiFont (26.0f));
    g.setColour (pal.textLight);
    g.drawFittedText (juce::String::fromUTF8 (
                          "Algoritmo original: ME FreeFilter (Steinberg Mastering Edition)\n"
                          "\xC2\xA9 1998-2001 Spectral Design GmbH.\n\n"
                          "Reimplementaci\xC3\xB3n en JUCE para VST3 / AU,\n"
                          "Windows x64 y macOS Apple Silicon.\n"
                          "Bandas iguales, Learn / Match / Morph y tercios de octava.\n"
                          "Contrastado con el original (ver docs/ANALISIS.md).\n\n"
                          "Clic para cerrar."),
                      panel.reduced (24.0f).toNearestInt(), juce::Justification::centred, 12);

    skin.drawLogoCreator (g, juce::Rectangle<float> (0.0f, 0.0f, 320.0f, 70.0f).withCentre ({ designW * 0.5f, 720.0f }));
    g.setFont (uiFont (13.0f));
    g.setColour (pal.textLight.withAlpha (0.6f));
    g.drawText (juce::String ("v") + JucePlugin_VersionString, versionRect, juce::Justification::centredRight, false);
}

//==============================================================================
static juce::File presetsFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("LibreFilter").getChildFile ("Presets");
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

void LibreFilterEditor::refreshPresets (const juce::String& selectName)
{
    presetFiles.clear();
    for (const auto& f : presetsFolder().findChildFiles (juce::File::findFiles, false, "*.lfcurve"))
        presetFiles.push_back (f);
    std::sort (presetFiles.begin(), presetFiles.end(),
               [] (const juce::File& a, const juce::File& b) { return a.getFileNameWithoutExtension().compareIgnoreCase (b.getFileNameWithoutExtension()) < 0; });

    presetIndex = presetFiles.empty() ? -1 : juce::jlimit (0, (int) presetFiles.size() - 1, presetIndex);
    if (selectName.isNotEmpty())
        for (size_t i = 0; i < presetFiles.size(); ++i)
            if (presetFiles[i].getFileNameWithoutExtension() == selectName) presetIndex = (int) i;
}

// Carga el preset 'index' en el lado seleccionado (Source o Dest.).
void LibreFilterEditor::presetLoadIndex (int index)
{
    refreshPresets();
    if (index < 0 || index >= (int) presetFiles.size()) return;
    presetIndex = index;
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
        setStatus ("Not a valid LibreFilter preset.");
    repaint();
}

// Recorre los presets (flechas < y >).
void LibreFilterEditor::presetStep (int delta)
{
    refreshPresets();
    if (presetFiles.empty()) { setStatus ("No presets yet. Learn a curve and press Save."); return; }
    presetLoadIndex (presetIndex < 0 ? (delta > 0 ? 0 : (int) presetFiles.size() - 1)
                                     : (presetIndex + delta + (int) presetFiles.size()) % (int) presetFiles.size());
}

// Guarda la curva aprendida del lado seleccionado con el nombre escrito (o uno automatico).
void LibreFilterEditor::presetSave()
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
    const auto file = presetsFolder().getChildFile (name + ".lfcurve");
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
void LibreFilterEditor::presetDelete()
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

// ---- tema (easter egg: clic en el logo del plugin) ----
void LibreFilterEditor::applyTheme (bool red)
{
    redTheme = red;
    skin.setRedTheme (red);
    auto C = [] (juce::uint32 v) { return juce::Colour (v); };
    if (! red)
        pal = { C (0xfff2f8ff), C (0xff0e2a4a), C (0xff010510), C (0xff7ed0ff), C (0xff2b3f5c), C (0xff071428), C (0xff3da8ff),
                C (0xff0a1424), C (0xff6aa6e8), C (0xff4a5f7a), C (0xff2aa8ff), C (0xff061426), C (0xff3df0ff), C (0xff1e5fb4),
                C (0xff7f95ad), C (0xff030c1a), C (0xff071a30) };
    else
        pal = { C (0xfffff4f0), C (0xff3a1410), C (0xff120405), C (0xffff9a80), C (0xff5c2b2b), C (0xff2a0d0b), C (0xffff6a4a),
                C (0xff240a0a), C (0xffe8806a), C (0xff7a4f4a), C (0xffff5a3a), C (0xff220807), C (0xffffb060), C (0xffb4361e),
                C (0xffad8f7f), C (0xff1a0605), C (0xff300d0a) };

    presetName.setColour (juce::TextEditor::backgroundColourId, pal.field);
    presetName.setColour (juce::TextEditor::textColourId, pal.textLight);
    presetName.setColour (juce::TextEditor::outlineColourId, pal.boxLine.withAlpha (0.45f));
    presetName.setColour (juce::TextEditor::focusedOutlineColourId, pal.focus);
    presetName.setColour (juce::TextEditor::highlightColourId, pal.highlight);
    presetName.setColour (juce::CaretComponent::caretColourId, pal.textLight);

    menuLaf.setColour (juce::PopupMenu::backgroundColourId, pal.menuBg);
    menuLaf.setColour (juce::PopupMenu::textColourId, pal.textLight);
    menuLaf.setColour (juce::PopupMenu::headerTextColourId, pal.hint);
    menuLaf.setColour (juce::PopupMenu::highlightedBackgroundColourId, pal.highlight);
    menuLaf.setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

    processor.setRedTheme (red);
    if (getWidth() > 0) rebuildBackground();
    repaint();
}

// ---- caja de presets: desplegable ----
void LibreFilterEditor::paintPresetField (juce::Graphics& g)
{
    const auto r = presetField;
    g.setColour (pal.field);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (pal.boxLine.withAlpha (0.45f));
    g.drawRoundedRectangle (r, 6.0f, 1.5f);
    if (editingName) return;                          // el campo de texto la tapa

    const auto name = presetName.getText();
    g.setFont (uiFont (r.getHeight() * 0.50f));
    g.setColour (name.isEmpty() ? pal.hint : pal.textLight);
    g.drawText (name.isEmpty() ? juce::String ("preset name") : name, r.reduced (14.0f, 0.0f).withTrimmedRight (26.0f),
                juce::Justification::centredLeft, true);               // centrado en vertical

    juce::Path arrow;                                  // triangulo del desplegable
    const auto c = juce::Point<float> (r.getRight() - 20.0f, r.getCentreY());
    arrow.addTriangle (c.x - 7.0f, c.y - 4.0f, c.x + 7.0f, c.y - 4.0f, c.x, c.y + 5.0f);
    g.setColour (pal.textLight.withAlpha (0.8f));
    g.fillPath (arrow);
}

void LibreFilterEditor::showPresetMenu()
{
    refreshPresets();
    juce::PopupMenu m;
    m.setLookAndFeel (&menuLaf);
    if (presetFiles.empty())
        m.addItem (-1, "(no presets yet)", false);
    for (size_t i = 0; i < presetFiles.size(); ++i)
        m.addItem (100 + (int) i, presetFiles[i].getFileNameWithoutExtension(), true, (int) i == presetIndex);
    m.addSeparator();
    m.addItem (1, "New name...");

    const auto area = localAreaToGlobal (presetField.transformedBy (designToScreen()).toNearestInt());
    juce::Component::SafePointer<LibreFilterEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (area).withMinimumWidth (area.getWidth()).withMaximumNumColumns (1),
                     [safe] (int id)
                     {
                         if (safe == nullptr) return;
                         if (id == 1) safe->startNameEdit();
                         else if (id >= 100) safe->presetLoadIndex (id - 100);
                     });
}

void LibreFilterEditor::startNameEdit()
{
    editingName = true;
    nameBeforeEdit = presetName.getText();
    presetName.setText ({}, false);
    presetName.setVisible (true);
    presetName.grabKeyboardFocus();
    repaint();
}

void LibreFilterEditor::endNameEdit (bool commit)
{
    if (! editingName) return;
    editingName = false;
    if (! commit || presetName.getText().trim().isEmpty()) presetName.setText (nameBeforeEdit, false);
    presetName.setVisible (false);
    repaint();
}

void LibreFilterEditor::layoutPresetEditor()
{
    const auto r = presetField.transformedBy (designToScreen()).toNearestInt();
    presetName.setBounds (r);
    presetName.applyFontToAllText (juce::Font (juce::FontOptions ((float) r.getHeight() * 0.62f)));
}

//==============================================================================
void LibreFilterEditor::timerCallback()
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
    if (! showAbout) repaint();      // la pantalla de creditos es estatica
}

//==============================================================================
void LibreFilterEditor::mouseMove (const juce::MouseEvent& e)
{
    hover = toDesign (e.position);
    readHz.clear();
    readDb.clear();
    if (boardRect.expanded (0.0f, 16.0f).contains (hover))
    {
        const int b = bandAt (hover.x);
        const float centre = processor.isThirdOctave() ? (float) lf::kThirdOctave[b]
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

void LibreFilterEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto p = toDesign (e.position);

    if (showAbout) { showAbout = false; repaint(); return; }
    if (editingName && ! presetField.contains (p)) endNameEdit (true);
    if (logoRect.contains (p)) { applyTheme (! redTheme); return; }                    // easter egg
    if (creatorRect.contains (p)) { endNameEdit (true); showAbout = true; repaint(); return; }
    if (presetField.contains (p) && ! editingName) { showPresetMenu(); return; }

    // botones
    for (int id = 0; id < NumButtons; ++id)
    {
        if (! buttonRect (id).contains (p)) continue;
        switch (id)
        {
            case ModeFree:  drawMode = DrawMode::freehand; clearLeds(); break;
            case ModeLevel: drawMode = DrawMode::level;    clearLeds(); break;
            case ModeLine:  drawMode = DrawMode::line;     clearLeds(); break;
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
        clearLeds();
        for (int b = 0; b < lf::kBands; ++b) startValues[(size_t) b] = processor.getBandValue (b);
        mouseDrag (e);
    }
}

void LibreFilterEditor::mouseDrag (const juce::MouseEvent& e)
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
            for (int i = 0; i < lf::kBands; ++i) setBand (i, v);
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

    // bolitas: en dibujo libre, todo el rango tocado; en nivel y recta (que mueven muchas bandas), solo la banda bajo el puntero
    if (drawMode == DrawMode::freehand && ! touched.empty())
        setLedRange (*touched.begin(), *touched.rbegin());
    else
    {
        clearLeds();
        led[(size_t) b] = true;
    }
    mouseMove (e);
}

void LibreFilterEditor::mouseUp (const juce::MouseEvent&)
{
    for (int b : touched)
        if (auto* prm = processor.apvts.getParameter (processor.activeBandId (b)))
            prm->endChangeGesture();
    touched.clear();
    lastBand = -1;
    draggingBoard = false;
    morphDragging = false;
}

void LibreFilterEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto p = toDesign (e.position);
    if (! boardRect.expanded (0.0f, 12.0f).contains (p)) return;
    clearLeds();
    if (drawMode == DrawMode::level)          // solo los sliders; lo aprendido y Match no se tocan (eso es Reset)
        for (int i = 0; i < lf::kBands; ++i) setBand (i, 0.0f);
    else
    {
        setBand (bandAt (p.x), 0.0f);
        led[(size_t) bandAt (p.x)] = true;
    }
    mouseUp (e);
}

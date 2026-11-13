# FreeFilter 1.3 · aspecto LibreFilter (VST3 / AU, Windows x64 + macOS Apple Silicon)

Reimplementación en JUCE 8 del ecualizador de filtro libre **ME FreeFilter** (Steinberg Mastering Edition,
2001). Formatos: **VST3**, **AU** y **Standalone**. Compila en **Windows x64** y **macOS arm64** (M1–M4)
con GitHub Actions.

El filtro se dedujo analizando el plugin original y está contrastado contra él (ver
[docs/ANALISIS.md](docs/ANALISIS.md), con lo verificado y lo no verificado).

## Aspecto (skin LibreFilter) y control de versiones

La interfaz usa el pack gráfico **LibreFilter** (`resources/librefilter/`, SVG/PNG incrustados con `juce_add_binary_data`).
Solo cambia lo visual: parámetros, DSP y manejo del ratón son los mismos. La geometría está en `src/EditorLayout.h`
(espacio de diseño 940 × 980) y el dibujo de los recursos en `src/LibreSkin.cpp`. Detalles de integración: JUCE no pinta
los filtros de brillo, el patrón `grain` ni el degradado del texto de los SVG, así que `LibreSkin` preprocesa el XML al
cargarlo (halos de capas en lugar de blur) y dibuja los logos con texto de JUCE. El nombre del producto en el plugin
sigue siendo `FreeFilter` (ID, nombre del archivo y parámetros), para no romper los proyectos ya guardados.

Para volver al aspecto anterior (panel rojo, 454 × 417):

```bash
git switch main                    # la rama principal conserva el aspecto clásico (v1.2.1)
git checkout skin-classic-v1.2.1   # o la etiqueta exacta del último commit con el aspecto clásico
```

La rama `skin-librefilter` contiene la skin nueva; `git switch skin-librefilter` vuelve a ella.

## Qué hace

* **30 bandas** de ±15 dB, interpoladas en dB y realizadas como FIR lineal.
  * **Lin** (bandas iguales): bandas lineales hasta Nyquist, FIR de 257 taps, latencia 128 muestras.
  * **Log** (tercios de octava): 25 Hz … 20 kHz, FIR de 8193 taps por convolución, latencia 4096 muestras.
    La latencia se comunica al host.
* **Learn / Match / Morph:** elige **Source** o **Dest.**, activa **Learn** y reproduce el material; al terminar,
  aprende el otro lado. Con ambos aprendidos, **Match** aplica `Morph · (Dest/Source)` en dB (Morph de 0 a 200 %)
  más los sliders, que en Match son un juego independiente.
* **Presets de curvas aprendidas** (abajo a la izquierda, sin diálogos del sistema): escribe un nombre y pulsa
  **Save** para guardar la curva aprendida del lado seleccionado (Source o Dest.); **<** y **>** recorren los presets
  y cargan el elegido en ese mismo lado; **Del** borra el seleccionado (hay que pulsarlo dos veces). Los presets son
  archivos `.ffcurve` en `Documentos/FreeFilter/Presets/` (puedes copiarlos entre equipos). Si un preset se guardó
  a otra frecuencia de muestreo, al cargarlo el espectro se adapta por frecuencias (el aviso lo indica).
  Además, lo aprendido **persiste** al parar/arrancar la reproducción y se guarda dentro del proyecto del DAW.
* **Analizador de espectro** de entrada (verde), salida (rojo) y curva del filtro (amarillo).
* Modos de dibujo: libre, nivel y recta. Doble clic en un slider lo pone a 0 dB.
  **Reset** pone todo a 0, borra lo aprendido y desactiva Learn/Match (el doble clic en modo nivel solo pone los
  sliders a 0). **Freeze** congela el analizador, el filtro actual (los sliders y Match no lo cambian) y el aprendizaje;
  el audio sigue pasando por el filtro congelado.
* Clic en el título → créditos.

## Compilar

Requisitos: CMake ≥ 3.22 y un compilador C++17 (MSVC o Xcode). JUCE se descarga solo.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Los binarios quedan en `build/FreeFilter_artefacts/Release/`.
Tests del DSP (sin JUCE): `g++ -std=c++17 tests/dsp_test.cpp -o dsp_test && ./dsp_test`.

## GitHub Actions

`.github/workflows/build.yml` ejecuta los tests y compila Windows x64 y macOS arm64 en cada *push*.
Los `.zip` salen como artefactos. Una etiqueta `v*` crea además un *release* en borrador.

## Instalar en macOS

Los binarios llevan firma *ad-hoc* (sin cuenta de desarrollador), así que Gatekeeper los pone en cuarentena
al descargarlos:

```bash
xattr -cr "FreeFilter.vst3"
cp -R "FreeFilter.vst3" ~/Library/Audio/Plug-Ins/VST3/
```

(AU: `~/Library/Audio/Plug-Ins/Components/`).

## Aviso legal

El plugin original es software comercial de terceros (© Spectral Design GmbH / Steinberg). Este repositorio
contiene **código propio**: ni la DLL original, ni código decompilado, ni sus mapas de bits se incluyen.
Las ideas y algoritmos (interpolación, ventana Hann, FIR, espectros medios) no son material con copyright, pero
el nombre y la imagen del producto original sí pueden estar protegidos: **no publiques ni vendas esto con el
nombre del original** sin permiso. Por eso el repo es privado.

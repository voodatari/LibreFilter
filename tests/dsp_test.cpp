// Tests del nucleo DSP (sin JUCE). Devuelve 0 si todo pasa.
#include "../src/LibreFilterDSP.h"

#include <algorithm>
#include <cstdio>
#include <random>

static int failures = 0;

#define CHECK(cond, ...)                                       \
    do {                                                       \
        if (!(cond)) {                                         \
            ++failures;                                        \
            std::printf ("FALLO %s:%d  ", __FILE__, __LINE__); \
            std::printf (__VA_ARGS__);                         \
            std::printf ("\n");                                \
        }                                                      \
    } while (0)

static double dbAt (const float* taps, int bin)
{
    return 20.0 * std::log10 (std::max (std::abs (lf::tapsResponse (taps, bin)), 1e-9));
}

int main()
{
    float taps[lf::kEqTaps];
    float v[lf::kBands];

    // 1) Todo a 0 dB -> delta unitaria
    std::fill (v, v + lf::kBands, 0.0f);
    lf::designTaps (v, taps);
    CHECK (std::abs (taps[lf::kEqHalf] - 1.0f) < 1e-4f, "centro = %f", taps[lf::kEqHalf]);
    for (int n = 0; n < lf::kEqTaps; ++n)
        if (n != lf::kEqHalf) CHECK (std::abs (taps[n]) < 1e-4f, "tap %d = %g", n, taps[n]);

    // 2) Simetria (fase lineal)
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> ud (-15.0f, 15.0f);
    for (auto& x : v) x = ud (rng);
    lf::designTaps (v, taps);
    for (int n = 0; n < lf::kEqHalf; ++n)
        CHECK (taps[lf::kEqHalf - 1 - n] == taps[lf::kEqHalf + 1 + n], "asimetria en %d", n);

    // 3) Todo a +6 dB -> ganancia plana de +6 dB
    std::fill (v, v + lf::kBands, 6.0f);
    lf::designTaps (v, taps);
    for (int bin = 8; bin < 505; bin += 7)
        CHECK (std::abs (dbAt (taps, bin) - 6.0) < 0.05, "+6 dB plano: bin %d da %.3f", bin, dbAt (taps, bin));

    // 4) Regresion contra el plugin original: slider 17 = +12 dB (medido en la DLL original a 44.1 kHz)
    std::fill (v, v + lf::kBands, 0.0f);
    v[17] = 12.0f;
    lf::designTaps (v, taps);
    struct Ref { int bin; double db; };
    const Ref refs[] = { { 276, -0.0002 }, { 280, 0.4046 }, { 284, 2.0853 }, { 288, 4.7109 }, { 292, 7.5631 },
                         { 296, 9.8768 },  { 300, 10.2742 }, { 304, 8.4642 }, { 308, 5.6553 }, { 312, 2.9052 },
                         { 316, 0.8017 },  { 320, 0.0427 },  { 324, -0.0039 } };
    double worst = 0;
    for (const auto& r : refs)
    {
        const double got = dbAt (taps, r.bin);
        worst = std::max (worst, std::abs (got - r.db));
        CHECK (std::abs (got - r.db) < 0.12, "bin %d: original %.4f dB, mio %.4f dB", r.bin, r.db, got);
    }
    std::printf ("regresion slider17=+12 dB: error maximo vs original = %.4f dB\n", worst);

    // 5) Respuesta de un impulso: el filtrado FIR coincide con los coeficientes
    {
        lf::FirChannel fir;
        fir.reset();
        std::fill (v, v + lf::kBands, 0.0f);
        v[5] = -9.0f; v[20] = 7.5f;
        lf::designTaps (v, taps);
        std::vector<float> out (lf::kEqTaps + 8);
        for (size_t i = 0; i < out.size(); ++i) out[i] = fir.process (i == 0 ? 1.0f : 0.0f, taps);
        for (int n = 0; n < lf::kEqTaps; ++n)
            CHECK (std::abs (out[(size_t) n] - taps[n]) < 1e-6f, "impulso en %d", n);
    }

    // 6) Match: Dest con +6 dB (potencia) en los bins 200..260, Source plano -> +6 dB en esa zona
    {
        std::vector<double> A (lf::kEqBins, 1000.0), B (lf::kEqBins, 1000.0);
        for (int k = 200; k <= 260; ++k) A[(size_t) k] *= std::pow (10.0, 0.6);
        double binDb[lf::kEqBins] = {};
        lf::applyMatch (A, B, 1.0, binDb, lf::kEqBins);
        float t[lf::kEqTaps];
        lf::designTapsFromDb (lf::Mode::Equal, binDb, t);
        for (int k = 215; k <= 245; k += 5)
            CHECK (std::abs (dbAt (t, k) - 6.0) < 0.05, "match +6 dB: bin %d da %.3f", k, dbAt (t, k));
        CHECK (std::abs (dbAt (t, 100)) < 0.05, "fuera de la zona debe ser 0 dB (%.3f)", dbAt (t, 100));
        // Morph 0.5 -> +3 dB; Morph 2 -> +12 dB
        std::fill (binDb, binDb + lf::kEqBins, 0.0);
        lf::applyMatch (A, B, 0.5, binDb, lf::kEqBins);
        lf::designTapsFromDb (lf::Mode::Equal, binDb, t);
        CHECK (std::abs (dbAt (t, 230) - 3.0) < 0.05, "morph 0.5: %.3f", dbAt (t, 230));
        std::fill (binDb, binDb + lf::kEqBins, 0.0);
        lf::applyMatch (A, B, 2.0, binDb, lf::kEqBins);
        lf::designTapsFromDb (lf::Mode::Equal, binDb, t);
        CHECK (std::abs (dbAt (t, 230) - 12.0) < 0.05, "morph 2: %.3f", dbAt (t, 230));
    }

    // 7) Aprendiz: ruido blanco de sigma 0.2 -> espectro medio ~ plano; recuento de bloques = floor((N-256)/768)
    {
        lf::SpectrumLearner lr;
        lr.configure (1024, 768);
        lr.setTarget (lf::SpectrumLearner::Dest);
        std::normal_distribution<float> nd (0.0f, 0.2f);
        const int N = 65536;
        for (int i = 0; i < N; ++i) lr.push (nd (rng));
        CHECK (lr.frames (lf::SpectrumLearner::Dest) == 85, "bloques = %d (esperado 85, como el original)",
               lr.frames (lf::SpectrumLearner::Dest));
        auto avg = lr.average (lf::SpectrumLearner::Dest);
        double mean = 0; for (int k = 10; k < 500; ++k) mean += avg[(size_t) k]; mean /= 490.0;
        CHECK (std::abs (mean / (0.04 * 1024.0) - 1.0) < 0.1, "potencia media = %g (esperado %g)", mean, 0.04 * 1024.0);
    }

    // 7b) Lo aprendido sobrevive a reiniciar el anillo (parar/arrancar la reproduccion) y a exportar/importar
    {
        lf::SpectrumLearner lr;
        lr.configure (1024, 768);
        lr.setTarget (lf::SpectrumLearner::Source);
        std::normal_distribution<float> nd (0.0f, 0.3f);
        for (int i = 0; i < 20000; ++i) lr.push (nd (rng));
        const int before = lr.frames (lf::SpectrumLearner::Source);
        const auto avgBefore = lr.average (lf::SpectrumLearner::Source);
        lr.resetRing();   // lo que hace prepareToPlay cuando el host para y arranca
        CHECK (lr.frames (lf::SpectrumLearner::Source) == before, "resetRing borro lo aprendido");
        auto avgAfter = lr.average (lf::SpectrumLearner::Source);
        for (size_t k = 0; k < avgAfter.size(); ++k)
            CHECK (avgAfter[k] == avgBefore[k], "resetRing cambio el espectro en %zu", k);

        lf::SpectrumLearner other;
        other.configure (1024, 768);
        other.importAverage (lf::SpectrumLearner::Dest, avgBefore.data(), before);
        CHECK (other.frames (lf::SpectrumLearner::Dest) == before, "importAverage: frames");
        auto imp = other.average (lf::SpectrumLearner::Dest);
        for (size_t k = 0; k < imp.size(); ++k)
            CHECK (std::abs (imp[k] - avgBefore[k]) <= 1e-9 * std::max (1.0, avgBefore[k]), "importAverage: bin %zu", k);
    }

    // 8) Modo de tercios de octava: un slider en 1 kHz sube la zona de 1 kHz, no la de 100 Hz ni la de 10 kHz
    {
        std::vector<float> t ((size_t) lf::kThTaps);
        std::vector<double> binDb ((size_t) lf::kThBins), resp ((size_t) lf::kThBins);
        float s[lf::kBands] = {};
        s[16] = 12.0f;   // 1000 Hz
        const double sr = 44100.0;
        lf::slidersToBinDb (lf::Mode::ThirdOctave, s, sr, binDb.data());
        lf::designThirdFromDb (binDb.data(), sr, t.data(), resp.data());
        auto at = [&] (double hz) { return 20.0 * std::log10 (std::max (std::abs (resp[(size_t) std::lround (hz / (sr / lf::kThFFT))]), 1e-9)); };
        CHECK (std::abs (at (1000.0) - 12.0) < 0.3, "1 kHz da %.3f dB (esperado ~12)", at (1000.0));
        CHECK (std::abs (at (100.0)) < 0.05, "100 Hz da %.3f dB", at (100.0));
        CHECK (std::abs (at (10000.0)) < 0.05, "10 kHz da %.3f dB", at (10000.0));
        CHECK (at (1125.0) > 5.0 && at (1125.0) < 7.0, "1125 Hz (a medio camino) da %.3f dB", at (1125.0));  // interpolacion lineal en Hz
        // simetria del FIR
        for (int n = 0; n < lf::kThHalf; ++n)
            if (t[(size_t) (lf::kThHalf - 1 - n)] != t[(size_t) (lf::kThHalf + 1 + n)]) { CHECK (false, "FIR de tercios asimetrico en %d", n); break; }

        // El FIR realiza la respuesta de diseno: la DFT de los taps coincide con 'resp' (truncado minimo)
        double maxErr = 0;
        for (int k = 20; k < lf::kThBins - 20; k += 97)
        {
            const double w = 2.0 * lf::kPi * k / lf::kThFFT;
            double acc = t[(size_t) lf::kThHalf];
            for (int n = 1; n <= lf::kThHalf; ++n) acc += 2.0 * t[(size_t) (lf::kThHalf + n)] * std::cos (w * n);
            maxErr = std::max (maxErr, std::abs (acc - resp[(size_t) k]));
        }
        CHECK (maxErr < 5e-3, "FIR de tercios vs respuesta de diseno: error %g", maxErr);

        // Banda aguda: slider 25 (8 kHz) a +12 dB -> el original mide +11.36 dB de pico (suavizado de 257 taps)
        float s2[lf::kBands] = {};
        s2[25] = 12.0f;
        lf::slidersToBinDb (lf::Mode::ThirdOctave, s2, sr, binDb.data());
        lf::designThirdFromDb (binDb.data(), sr, t.data(), resp.data());
        CHECK (std::abs (at (8000.0) - 11.36) < 0.25, "8 kHz da %.3f dB (original 11.36)", at (8000.0));
        CHECK (std::abs (at (7127.0) - 5.88) < 0.25, "7127 Hz da %.3f dB (original 5.88)", at (7127.0));
        CHECK (std::abs (at (9243.0) - 4.57) < 0.25, "9243 Hz da %.3f dB (original 4.57)", at (9243.0));
    }

    if (failures == 0)
        std::printf ("OK: todos los tests pasan\n");
    return failures == 0 ? 0 : 1;
}

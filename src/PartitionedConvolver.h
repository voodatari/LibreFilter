// Convolucion particionada (overlap-save) con cambio de respuesta sin chasquidos, para el modo Log.
//
// juce::dsp::Convolution monta un motor nuevo cada vez que se carga una respuesta: el motor nuevo empieza sin
// historial de entrada y, al dibujar sobre los sliders (muchas cargas seguidas), se oyen saltos. Aqui el historial
// (la linea de retardo de espectros de entrada) es unico y compartido; al llegar una respuesta nueva se calcula,
// durante un bloque, la salida con la vieja y con la nueva sobre ese mismo historial y se hace un fundido lineal.
//
//  * Bloque de 256 muestras, FFT de 512, 33 particiones (>= 8193 taps).
//  * Retardo propio: kBlock muestras (se suma al retardo de fase lineal de la respuesta).
//  * Sin reservas de memoria ni bloqueos en el hilo de audio: las respuestas se pasan por casillas fijas.
#pragma once

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <complex>
#include <vector>

namespace lf
{
class PartitionedConvolver
{
public:
    static constexpr int kBlock = 256;
    static constexpr int kFft = 2 * kBlock;
    static constexpr int kParts = 33;                 // 33 * 256 = 8448 >= 8193 taps
    static constexpr int kChannels = 2;
    static constexpr int kSlots = 4;
    using Cx = std::complex<float>;

    PartitionedConvolver() : fft (9)                  // 2^9 = 512 = kFft
    {
        for (auto& s : slots) s.spec.assign ((size_t) kParts * kFft, Cx());
        for (auto& c : chans)
        {
            c.fdl.assign ((size_t) kParts * kFft, Cx());
            c.inFrame.fill (0.0f);
            c.inNew.fill (0.0f);
            c.out.fill (0.0f);
        }
    }

    // Hilo de mensajes, una vez (antes de procesar). 'identityTap' = posicion del impulso de la respuesta inicial.
    void prepare (int identityTap)
    {
        if (! prepared)
        {
            std::vector<float> delta ((size_t) kParts * kBlock, 0.0f);
            delta[(size_t) identityTap] = 1.0f;
            fillSlot (slots[0], delta.data(), (int) delta.size());
            slots[0].state.store (3);
            cur = 0;
            prepared = true;
        }
        reset();
    }

    // Borra historial y salidas (no la respuesta). Hilo de audio o con el audio parado.
    void reset()
    {
        for (auto& c : chans)
        {
            std::fill (c.fdl.begin(), c.fdl.end(), Cx());
            c.inFrame.fill (0.0f);
            c.inNew.fill (0.0f);
            c.out.fill (0.0f);
        }
        pos = 0;
        head = 0;
    }

    // Hilo de diseno: publica una respuesta nueva (se aplicara con fundido en el siguiente bloque).
    bool setImpulse (const float* taps, int numTaps)
    {
        for (int tries = 0; tries < 50; ++tries)
        {
            for (auto& s : slots)
            {
                int expected = 0;
                if (s.state.compare_exchange_strong (expected, 1))
                {
                    fillSlot (s, taps, numTaps);
                    s.state.store (2);
                    const int idx = (int) (&s - slots.data());
                    const int old = latest.exchange (idx);
                    if (old >= 0) slots[(size_t) old].state.store (0);   // nadie llego a usar la anterior
                    return true;
                }
            }
            juce::Thread::sleep (1);
        }
        return false;
    }

    // Hilo de audio. Procesa en sitio; la salida llega con kBlock muestras de retardo.
    void process (float* const* data, int numCh, int n)
    {
        numCh = juce::jmin (numCh, kChannels);
        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& c = chans[(size_t) ch];
                c.inNew[(size_t) pos] = data[ch][i];
                data[ch][i] = c.out[(size_t) pos];
            }
            if (++pos == kBlock)
            {
                pos = 0;
                processBlock (numCh);
            }
        }
    }

private:
    struct Slot
    {
        std::vector<Cx> spec;                 // kParts particiones de kFft bins
        std::atomic<int> state { 0 };         // 0 libre, 1 escribiendose, 2 lista, 3 en uso
    };
    struct Channel
    {
        std::vector<Cx> fdl;                  // kParts espectros de entrada (el mas reciente en 'head')
        std::array<float, kFft> inFrame {};
        std::array<float, kBlock> inNew {}, out {};
    };

    void fillSlot (Slot& s, const float* taps, int numTaps)
    {
        std::array<Cx, kFft> in {}, outSpec {};
        for (int p = 0; p < kParts; ++p)
        {
            for (int i = 0; i < kFft; ++i)
            {
                const int t = p * kBlock + i;
                in[(size_t) i] = Cx (i < kBlock && t < numTaps ? taps[t] : 0.0f, 0.0f);
            }
            fft.perform (in.data(), outSpec.data(), false);
            std::copy (outSpec.begin(), outSpec.end(), s.spec.begin() + (ptrdiff_t) p * kFft);
        }
    }

    // Suma de productos particion x espectro de entrada para la respuesta de la casilla 's' (solo bins 0..kBlock).
    void accumulate (const Slot& s, const std::vector<Cx>& fdl, std::array<Cx, kFft>& y) const
    {
        for (int k = 0; k <= kBlock; ++k) y[(size_t) k] = Cx();
        for (int p = 0; p < kParts; ++p)
        {
            const Cx* h = s.spec.data() + (size_t) p * kFft;
            const Cx* x = fdl.data() + (size_t) ((head - p + kParts) % kParts) * kFft;
            for (int k = 0; k <= kBlock; ++k)
            {
                const float hr = h[k].real(), hi = h[k].imag(), xr = x[k].real(), xi = x[k].imag();
                y[(size_t) k] += Cx (hr * xr - hi * xi, hr * xi + hi * xr);
            }
        }
        for (int k = 1; k < kBlock; ++k) y[(size_t) (kFft - k)] = std::conj (y[(size_t) k]);   // simetria hermitica
    }

    void processBlock (int numCh)
    {
        int target = -1;
        if (latest.load() >= 0)
        {
            target = latest.exchange (-1);
            if (target >= 0) slots[(size_t) target].state.store (3);
        }

        std::array<Cx, kFft> xt {}, xs {}, yc {}, yn {}, tc {}, tn {};
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& c = chans[(size_t) ch];
            std::copy (c.inFrame.begin() + kBlock, c.inFrame.end(), c.inFrame.begin());
            std::copy (c.inNew.begin(), c.inNew.end(), c.inFrame.begin() + kBlock);
            for (int i = 0; i < kFft; ++i) xt[(size_t) i] = Cx (c.inFrame[(size_t) i], 0.0f);
            fft.perform (xt.data(), xs.data(), false);
            std::copy (xs.begin(), xs.end(), c.fdl.begin() + (ptrdiff_t) head * kFft);

            accumulate (slots[(size_t) cur], c.fdl, yc);
            fft.perform (yc.data(), tc.data(), true);
            if (target < 0)
            {
                for (int i = 0; i < kBlock; ++i) c.out[(size_t) i] = tc[(size_t) (kBlock + i)].real();
            }
            else
            {
                accumulate (slots[(size_t) target], c.fdl, yn);
                fft.perform (yn.data(), tn.data(), true);
                for (int i = 0; i < kBlock; ++i)
                {
                    const float t = ((float) i + 0.5f) / (float) kBlock;
                    c.out[(size_t) i] = (1.0f - t) * tc[(size_t) (kBlock + i)].real() + t * tn[(size_t) (kBlock + i)].real();
                }
            }
        }
        head = (head + 1) % kParts;

        if (target >= 0)
        {
            slots[(size_t) cur].state.store (0);
            cur = target;
        }
    }

    juce::dsp::FFT fft;
    std::array<Slot, kSlots> slots;
    std::atomic<int> latest { -1 };
    std::array<Channel, kChannels> chans;
    int cur = 0, pos = 0, head = 0;
    bool prepared = false;
};
} // namespace lf

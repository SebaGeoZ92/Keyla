#include "Chromagram.h"

#include <algorithm>
#include <cmath>

namespace keyla::core
{

namespace
{
    constexpr double kTwoPi = 6.283185307179586476925286766559;

    double pitchToHertz (int pitch) noexcept
    {
        return 440.0 * std::pow (2.0, (static_cast<double> (pitch) - 69.0) / 12.0);
    }
}

void ChromaAnalyser::prepare (double sampleRate, Options options)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    opts = options;
    opts.lowestPitch = std::max (12, opts.lowestPitch);
    opts.highestPitch = std::max (opts.lowestPitch + 11, opts.highestPitch);
    opts.hop = std::max (64, opts.hop);
    opts.maxWindow = std::max (1024, opts.maxWindow);

    bins.clear();

    for (int pitch = opts.lowestPitch; pitch <= opts.highestPitch; ++pitch)
    {
        const double frequency = pitchToHertz (pitch);

        if (frequency >= rate * 0.45)
            break;                       // por encima de Nyquist no hay nada que mirar

        Bin bin;
        bin.pitch = pitch;
        bin.length = static_cast<int> (std::min (opts.cyclesPerBin * rate / frequency,
                                                 static_cast<double> (opts.maxWindow)));
        bin.length = std::max (64, bin.length);

        bin.cosKernel.resize (static_cast<std::size_t> (bin.length));
        bin.sinKernel.resize (static_cast<std::size_t> (bin.length));

        // Hann por la exponencial compleja. Sin ventana, una nota que no cae
        // justo en el centro del bin reparte energía por todo el espectro y el
        // cromagrama se convierte en puré.
        //
        // Se normaliza por la longitud para que un La1 con ventana de 32 768 y
        // un La5 con ventana de 1 090 den la misma cifra ante la misma
        // amplitud. Sin esto el grave saldría treinta veces más alto que el
        // agudo y todos los acordes parecerían tener la fundamental abajo.
        const double increment = kTwoPi * frequency / rate;
        const double norm = 2.0 / bin.length;

        for (int i = 0; i < bin.length; ++i)
        {
            const double window = 0.5 - 0.5 * std::cos (kTwoPi * i / (bin.length - 1));
            const double phase = increment * i;

            bin.cosKernel[static_cast<std::size_t> (i)] = static_cast<float> (window * std::cos (phase) * norm);
            bin.sinKernel[static_cast<std::size_t> (i)] = static_cast<float> (window * std::sin (phase) * norm);
        }

        bins.push_back (std::move (bin));
    }

    // La ventana más larga más el margen de atraso: el anillo tiene que poder
    // guardar a la vez lo que mira el fotograma actual y lo que todavía no se
    // ha analizado.
    opts.maxBacklog = std::max (opts.hop, opts.maxBacklog);
    ring.assign (static_cast<std::size_t> (opts.maxWindow + opts.maxBacklog), 0.0f);
    profile.assign (bins.size(), 0.0);
    writePos = 0;
    pending = 0;
    filled = 0;
}

void ChromaAnalyser::push (const float* samples, int numSamples)
{
    if (ring.empty() || samples == nullptr || numSamples <= 0)
        return;

    const auto capacity = static_cast<int> (ring.size());

    for (int i = 0; i < numSamples; ++i)
    {
        ring[static_cast<std::size_t> (writePos)] = samples[i];

        if (++writePos >= capacity)
            writePos = 0;
    }

    pending += numSamples;
    filled = std::min (capacity, filled + numSamples);
}

bool ChromaAnalyser::popFrame (Chroma& result)
{
    if (bins.empty())
        return false;

    const auto capacity = static_cast<int> (ring.size());
    const int longest = bins.front().length;

    // No se analiza hasta tener la ventana más larga llena. Si no, los primeros
    // fotogramas miran ceros y producen un acorde inventado.
    if (filled < longest)
        return false;

    // Hasta dónde se puede mirar hacia atrás: ni más allá del margen de atraso
    // —el anillo ya lo habría pisado— ni más atrás de lo que se lleva oído.
    // Si el atraso pasa de ahí, ese audio se perdió y lo honesto es saltar
    // hasta donde los datos siguen siendo buenos, no devolver un fotograma
    // calculado sobre memoria vieja.
    const int furthestBack = std::min (opts.maxBacklog, filled - longest);
    pending = std::min (pending, furthestBack + opts.hop);

    if (pending < opts.hop)
        return false;

    // Este fotograma consume un salto. Lo que quede pendiente es audio más
    // reciente que su ventana, así que la ventana no termina en la última
    // muestra escrita sino ese tanto por detrás.
    pending -= opts.hop;
    const int endOffset = pending;

    result = Chroma {};
    double peak = 0.0;
    double totalEnergy = 0.0;

    for (std::size_t b = 0; b < bins.size(); ++b)
    {
        const auto& bin = bins[b];

        // La ventana termina en "ahora": se recorre hacia atrás desde la última
        // muestra escrita. Todas las notas quedan alineadas al presente, aunque
        // cada una mire un trozo de pasado distinto.
        int index = writePos - endOffset - bin.length;

        while (index < 0)
            index += capacity;

        double re = 0.0;
        double im = 0.0;

        for (int i = 0; i < bin.length; ++i)
        {
            const double sample = ring[static_cast<std::size_t> (index)];

            re += sample * bin.cosKernel[static_cast<std::size_t> (i)];
            im -= sample * bin.sinKernel[static_cast<std::size_t> (i)];

            if (++index >= capacity)
                index = 0;
        }

        const double magnitude = std::sqrt (re * re + im * im);

        profile[b] = magnitude;
        totalEnergy += magnitude;
        peak = std::max (peak, magnitude);

        result.bins[static_cast<std::size_t> (((bin.pitch % 12) + 12) % 12)] += magnitude;
    }

    result.energy = totalEnergy / static_cast<double> (bins.size());

    double chromaPeak = 0.0;

    for (auto value : result.bins)
        chromaPeak = std::max (chromaPeak, value);

    if (chromaPeak > 0.0)
        for (auto& value : result.bins)
            value /= chromaPeak;

    return true;
}

double ChromaAnalyser::windowSeconds() const noexcept
{
    if (bins.empty())
        return 0.0;

    return bins.front().length / rate;
}

} // namespace keyla::core

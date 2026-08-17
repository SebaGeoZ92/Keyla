#pragma once

// Primitivas de comunicación entre dominios de ejecución (doc 02 §1).
//
// Todo lo de este fichero está pensado para que el lado que corre en el hilo de
// audio no asigne memoria, no tome locks y no bloquee jamás (invariante 1).

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace keyla::probe
{

// ── Cola SPSC lock-free ─────────────────────────────────────────────────────
//
// Un único productor y un único consumidor. Capacidad potencia de dos.
// Si se llena, se descartan las escrituras nuevas y se cuentan: perder un
// evento es preferible a bloquear el hilo que lo produce.

template <typename T, std::size_t Capacity>
class SpscQueue
{
    static_assert (std::is_trivially_copyable_v<T>, "T debe ser POD: se copia en el hilo de audio");
    static_assert ((Capacity & (Capacity - 1)) == 0, "Capacity debe ser potencia de dos");

public:
    // Lado productor.
    bool push (const T& item) noexcept
    {
        const auto w = writePos.load (std::memory_order_relaxed);
        const auto r = readPos.load (std::memory_order_acquire);

        if (w - r >= Capacity)
        {
            dropped.fetch_add (1, std::memory_order_relaxed);
            return false;
        }

        storage[w & (Capacity - 1)] = item;
        writePos.store (w + 1, std::memory_order_release);
        return true;
    }

    // Lado consumidor.
    bool pop (T& out) noexcept
    {
        const auto r = readPos.load (std::memory_order_relaxed);

        if (r == writePos.load (std::memory_order_acquire))
            return false;

        out = storage[r & (Capacity - 1)];
        readPos.store (r + 1, std::memory_order_release);
        return true;
    }

    std::uint64_t droppedCount() const noexcept { return dropped.load (std::memory_order_relaxed); }

private:
    std::array<T, Capacity> storage {};
    std::atomic<std::uint64_t> writePos { 0 };
    std::atomic<std::uint64_t> readPos { 0 };
    std::atomic<std::uint64_t> dropped { 0 };
};

// ── Seqlock de publicación ──────────────────────────────────────────────────
//
// El hilo de audio publica un snapshot inmutable; la consola hace polling
// (invariante 5). El escritor nunca espera. El lector reintenta si le pillan
// una escritura a medias.

template <typename T>
class SnapshotPublisher
{
    static_assert (std::is_trivially_copyable_v<T>, "el snapshot debe ser POD");

public:
    void publish (const T& value) noexcept
    {
        const auto s = seq.load (std::memory_order_relaxed);
        seq.store (s + 1, std::memory_order_relaxed);           // impar: escritura en curso
        std::atomic_thread_fence (std::memory_order_release);
        data = value;
        std::atomic_thread_fence (std::memory_order_release);
        seq.store (s + 2, std::memory_order_relaxed);           // par: estable
    }

    T read() const noexcept
    {
        T copy {};

        for (;;)
        {
            const auto before = seq.load (std::memory_order_relaxed);

            if ((before & 1u) != 0u)
                continue;

            std::atomic_thread_fence (std::memory_order_acquire);
            copy = data;
            std::atomic_thread_fence (std::memory_order_acquire);

            if (seq.load (std::memory_order_relaxed) == before)
                return copy;
        }
    }

private:
    mutable T data {};
    std::atomic<std::uint64_t> seq { 0 };
};

// ── Estadística incremental (Welford) ───────────────────────────────────────
//
// Media y varianza sin guardar la muestra ni asignar memoria. Se actualiza en
// el hilo de audio.

struct RunningStats
{
    void add (double x) noexcept
    {
        ++n;
        const double delta = x - mean;
        mean += delta / static_cast<double> (n);
        m2 += delta * (x - mean);

        if (x > maximum) maximum = x;
        if (x < minimum) minimum = x;
    }

    double variance() const noexcept { return n > 1 ? m2 / static_cast<double> (n - 1) : 0.0; }
    double stddev()   const noexcept { return std::sqrt (variance()); }

    std::uint64_t n { 0 };
    double mean { 0.0 };
    double m2 { 0.0 };
    double maximum { -1e300 };
    double minimum { 1e300 };
};

// ── Percentiles ─────────────────────────────────────────────────────────────
//
// Se acumulan fuera del hilo de audio (aquí sí se puede asignar memoria).

class Percentiles
{
public:
    void add (double x) { samples.push_back (x); sorted = false; }

    void reserve (std::size_t n) { samples.reserve (n); }

    std::size_t count() const noexcept { return samples.size(); }

    bool empty() const noexcept { return samples.empty(); }

    double value (double p)
    {
        if (samples.empty())
            return 0.0;

        ensureSorted();

        const auto idx = static_cast<std::size_t> (p * static_cast<double> (samples.size() - 1) + 0.5);
        return samples[idx];
    }

    double maximum()
    {
        if (samples.empty())
            return 0.0;

        ensureSorted();
        return samples.back();
    }

    double mean() const
    {
        if (samples.empty())
            return 0.0;

        double acc = 0.0;
        for (auto s : samples) acc += s;
        return acc / static_cast<double> (samples.size());
    }

private:
    void ensureSorted()
    {
        if (! sorted)
        {
            std::sort (samples.begin(), samples.end());
            sorted = true;
        }
    }

    std::vector<double> samples;
    bool sorted { true };
};

} // namespace keyla::probe

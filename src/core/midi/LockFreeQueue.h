#pragma once

// Lo único que puede cruzar la frontera entre dominios de ejecución, junto con
// los snapshots inmutables (doc 02 §1, invariantes 1 y 5).

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace keyla::core
{

/** Cola lock-free de un productor y un consumidor, capacidad fija.

    El caso al que sirve es `hilo MIDI → hilo de audio` (invariante 4) y
    `hilo de audio → dominio de sesión`. En los dos, uno de los dos extremos
    tiene prohibido bloquearse.

    Si se llena, **descarta y cuenta**. Es deliberado: bloquear al productor
    para no perder un evento significaría bloquear el hilo de audio o el del
    driver MIDI, y un dropout audible es mucho peor que una nota perdida. Que
    el contador no sea cero es una señal de que algo va mal aguas abajo, y por
    eso se reporta en vez de esconderse.
*/
template <typename T, std::size_t Capacity>
class LockFreeQueue
{
    static_assert (std::is_trivially_copyable_v<T>,
                   "T se copia en el hilo de audio: tiene que ser POD");
    static_assert (Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                   "Capacity debe ser potencia de dos");

public:
    /** Sólo desde el hilo productor. Devuelve false si estaba llena. */
    bool push (const T& item) noexcept
    {
        const auto w = writePos.load (std::memory_order_relaxed);
        const auto r = readPos.load (std::memory_order_acquire);

        if (w - r >= Capacity)
        {
            rejected.fetch_add (1, std::memory_order_relaxed);
            return false;
        }

        storage[w & (Capacity - 1)] = item;
        writePos.store (w + 1, std::memory_order_release);
        return true;
    }

    /** Sólo desde el hilo consumidor. Devuelve false si estaba vacía. */
    bool pop (T& out) noexcept
    {
        const auto r = readPos.load (std::memory_order_relaxed);

        if (r == writePos.load (std::memory_order_acquire))
            return false;

        out = storage[r & (Capacity - 1)];
        readPos.store (r + 1, std::memory_order_release);
        return true;
    }

    /** Aproximado: el otro extremo puede estar moviéndose mientras se lee. */
    std::size_t approximateSize() const noexcept
    {
        const auto w = writePos.load (std::memory_order_acquire);
        const auto r = readPos.load (std::memory_order_acquire);
        return static_cast<std::size_t> (w - r);
    }

    /** Cuántos `push` han sido rechazados por cola llena.

        En los caminos reales del proyecto nadie reintenta —ni el hilo de audio
        ni el del driver MIDI pueden quedarse esperando— así que ahí un rechazo
        *es* un evento perdido, y distinto de cero es un fallo que hay que
        reportar. Si algún día alguien reintenta desde un hilo que sí puede
        permitírselo, este contador seguirá contando intentos, no pérdidas. */
    std::uint64_t rejectedCount() const noexcept { return rejected.load (std::memory_order_relaxed); }

    static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    std::array<T, Capacity> storage {};
    std::atomic<std::uint64_t> writePos { 0 };
    std::atomic<std::uint64_t> readPos { 0 };
    std::atomic<std::uint64_t> rejected { 0 };
};

/** Publicación de snapshots inmutables del dominio de tiempo real hacia la UI
    (invariante 5): la UI hace polling, nunca se la notifica desde RT.

    Es un seqlock. El escritor no espera jamás — publica y sigue. El lector
    reintenta si le pillan una escritura a medias, que a 60 Hz contra un
    escritor que publica cada pocos milisegundos es rarísimo y cuesta un
    reintento.
*/
template <typename T>
class SnapshotPublisher
{
    static_assert (std::is_trivially_copyable_v<T>, "el snapshot tiene que ser POD");

public:
    /** Desde el hilo de tiempo real. No bloquea ni asigna. */
    void publish (const T& value) noexcept
    {
        const auto s = seq.load (std::memory_order_relaxed);
        seq.store (s + 1, std::memory_order_relaxed);           // impar: escritura en curso
        std::atomic_thread_fence (std::memory_order_release);
        data = value;
        std::atomic_thread_fence (std::memory_order_release);
        seq.store (s + 2, std::memory_order_relaxed);           // par: estable
    }

    /** Desde cualquier otro hilo. */
    T read() const noexcept
    {
        for (;;)
        {
            const auto before = seq.load (std::memory_order_relaxed);

            if ((before & 1u) != 0u)
                continue;

            std::atomic_thread_fence (std::memory_order_acquire);
            T copy = data;
            std::atomic_thread_fence (std::memory_order_acquire);

            if (seq.load (std::memory_order_relaxed) == before)
                return copy;
        }
    }

private:
    T data {};
    std::atomic<std::uint64_t> seq { 0 };
};

} // namespace keyla::core

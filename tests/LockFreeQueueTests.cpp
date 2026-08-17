// La cola es lo único que cruza la frontera entre dominios (doc 02 §1). Un
// fallo aquí no se manifiesta como un bug reproducible: se manifiesta como un
// clic esporádico que nadie sabe de dónde sale.

#include <core/midi/LockFreeQueue.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <vector>

using namespace keyla::core;

namespace
{
    struct Payload
    {
        int index { 0 };
        double value { 0.0 };
    };
}

TEST_CASE ("Lo que entra sale, y en el mismo orden", "[lockfree]")
{
    LockFreeQueue<Payload, 8> queue;

    Payload out {};
    CHECK (! queue.pop (out));           // vacía

    for (int i = 0; i < 5; ++i)
        CHECK (queue.push (Payload { i, i * 0.5 }));

    CHECK (queue.approximateSize() == 5);

    for (int i = 0; i < 5; ++i)
    {
        REQUIRE (queue.pop (out));
        CHECK (out.index == i);
        CHECK (out.value == i * 0.5);
    }

    CHECK (! queue.pop (out));
    CHECK (queue.rejectedCount() == 0);
}

TEST_CASE ("Cuando se llena descarta y lo cuenta, en vez de bloquear", "[lockfree]")
{
    LockFreeQueue<Payload, 4> queue;

    for (int i = 0; i < 4; ++i)
        CHECK (queue.push (Payload { i, 0.0 }));

    // La quinta no cabe. Bloquear aquí sería bloquear el hilo de audio.
    CHECK (! queue.push (Payload { 99, 0.0 }));
    CHECK (! queue.push (Payload { 98, 0.0 }));
    CHECK (queue.rejectedCount() == 2);

    // Y lo que sí entró sigue intacto: se pierde lo nuevo, no lo viejo.
    Payload out {};
    REQUIRE (queue.pop (out));
    CHECK (out.index == 0);
}

TEST_CASE ("El índice da la vuelta sin corromper nada", "[lockfree]")
{
    LockFreeQueue<Payload, 4> queue;
    Payload out {};

    // Muchas más vueltas que la capacidad, para pasar por el enmascarado.
    for (int i = 0; i < 1000; ++i)
    {
        REQUIRE (queue.push (Payload { i, 0.0 }));
        REQUIRE (queue.pop (out));
        CHECK (out.index == i);
    }

    CHECK (queue.rejectedCount() == 0);
}

TEST_CASE ("Un productor y un consumidor de verdad, en hilos distintos", "[lockfree][threads]")
{
    // No demuestra ausencia de carreras —eso no lo demuestra ningún test— pero
    // sí caza el error de ordenación de memoria más común, y con ThreadSanitizer
    // o bajo carga se vuelve bastante bueno.
    LockFreeQueue<Payload, 64> queue;

    constexpr int total = 200000;
    std::atomic<bool> producerDone { false };
    std::atomic<std::uint64_t> rejections { 0 };

    std::thread producer ([&]
    {
        std::uint64_t localRejections = 0;

        for (int i = 0; i < total; ++i)
        {
            // Este productor sí puede permitirse reintentar. El hilo de audio y
            // el del driver MIDI no, y por eso allí un rechazo es una pérdida.
            while (! queue.push (Payload { i, static_cast<double> (i) }))
            {
                ++localRejections;
                std::this_thread::yield();
            }
        }

        rejections.store (localRejections, std::memory_order_relaxed);
        producerDone.store (true, std::memory_order_release);
    });

    int expected = 0;
    bool ordered = true;
    bool valuesMatch = true;

    while (expected < total)
    {
        Payload out {};

        if (queue.pop (out))
        {
            if (out.index != expected)   ordered = false;
            if (out.value != static_cast<double> (out.index)) valuesMatch = false;
            ++expected;
        }
        else if (producerDone.load (std::memory_order_acquire))
        {
            // El productor terminó: sólo queda drenar lo que haya.
            if (! queue.pop (out))
                break;
        }
    }

    producer.join();

    CHECK (ordered);
    CHECK (valuesMatch);
    CHECK (expected == total);

    // Nada se perdió pese a los rechazos, porque aquí se reintentó. Y el
    // contador cuadra exactamente con los rechazos observados, que es la
    // propiedad que de verdad interesa comprobar.
    CHECK (queue.rejectedCount() == rejections.load());
}

TEST_CASE ("El publicador de snapshots devuelve siempre algo coherente", "[lockfree][snapshot]")
{
    struct Snapshot
    {
        int a { 0 };
        int b { 0 };
        int c { 0 };
    };

    SnapshotPublisher<Snapshot> publisher;

    CHECK (publisher.read().a == 0);

    publisher.publish (Snapshot { 1, 1, 1 });
    CHECK (publisher.read().a == 1);

    // El contrato del seqlock: el lector nunca ve una mezcla de dos escrituras.
    // Aquí se comprueba con un escritor que publica sin parar tripletes
    // coherentes; cualquier lectura con los tres campos distintos sería un
    // desgarro.
    std::atomic<bool> stop { false };
    std::atomic<int> torn { 0 };

    std::thread writer ([&]
    {
        for (int i = 2; ! stop.load (std::memory_order_relaxed); ++i)
            publisher.publish (Snapshot { i, i, i });
    });

    for (int i = 0; i < 200000; ++i)
    {
        const auto s = publisher.read();

        if (s.a != s.b || s.b != s.c)
            torn.fetch_add (1, std::memory_order_relaxed);
    }

    stop.store (true, std::memory_order_relaxed);
    writer.join();

    CHECK (torn.load() == 0);
}

// Test runner + offline demo renderer.
//   MysteryBoxTests                 run all tests
//   MysteryBoxTests --render out.wav   render a demo phrase to a WAV file
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <chrono>
#include <cstdlib>
#include <new>

#include "TestHarness.h"

// ---- global allocation tracking (only counts while a test enables it) ----
void* operator new (std::size_t size)
{
    if (mbx::test::tracking.load (std::memory_order_relaxed))
        mbx::test::allocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size) { return operator new (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace mbx::test
{
int renderDemo (const juce::File& file);                 // ProcessorTests.cpp
int screenshot (const juce::File& file, int tab, int osc, const juce::String& modTab); // ProcessorTests.cpp
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc >= 3 && std::string (argv[1]) == "--render")
        return mbx::test::renderDemo (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));

    if (argc >= 3 && std::string (argv[1]) == "--screenshot")
        return mbx::test::screenshot (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]),
                                      argc >= 4 ? std::atoi (argv[3]) : 0, argc >= 5 ? std::atoi (argv[4]) : 0,
                                      argc >= 6 ? juce::String (argv[5]) : juce::String());

    const std::string filter = argc >= 2 ? argv[1] : "";
    int passed = 0, failed = 0;
    for (const auto& c : mbx::test::registry())
    {
        if (! filter.empty() && std::string (c.name).find (filter) == std::string::npos)
            continue;
        const auto t0 = std::chrono::steady_clock::now();
        try
        {
            c.fn();
            const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
            std::printf ("[ PASS ] %-48s %8.1f ms\n", c.name, ms);
            ++passed;
        }
        catch (const std::exception& e)
        {
            mbx::test::tracking = false;
            std::printf ("[ FAIL ] %s\n         %s\n", c.name, e.what());
            ++failed;
        }
    }
    std::printf ("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

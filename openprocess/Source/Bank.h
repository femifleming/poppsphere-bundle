// The browser's 8x8 sound bank: built-in loops, files and "CD-skipped" fragments.
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>
#include <memory>
#include <vector>

struct Sound
{
    juce::AudioBuffer<float> audio;   // always 2 channels
    double sampleRate = 44100.0;
    juce::String name;
    double seconds() const { return audio.getNumSamples() / sampleRate; }
};

// How a cell's sound was made, so a saved session can rebuild it.
struct CellSource
{
    enum Kind { Builtin = 0, File = 1, Skipped = 2 };
    Kind kind = Builtin;
    juce::String path;        // File / Skipped
    double offset = 0.0;      // seconds into the file
    double length = 0.0;      // seconds (File: max length, Skipped: loop length)
    float intensity = 0.0f;   // Skipped: 0 = clean chunk, 1 = heavy skipping
    juce::uint32 seed = 0;

    juce::ValueTree toTree() const;
    static CellSource fromTree (const juce::ValueTree&);
};

namespace bank
{
    constexpr int kRows = 8, kCols = 8, kCells = 64;
    constexpr double kRate = 44100.0;

    juce::Colour outerColour (int cell);
    juce::Colour innerColour (int cell);
    juce::String familyName (int row);

    // built-in loop for a cell; seed 0 is the canonical bank, other seeds re-skip it
    std::shared_ptr<Sound> makeBuiltin (int cell, juce::uint32 seed);

    // rebuild from a source description (nullptr if a file cannot be read)
    std::shared_ptr<Sound> make (int cell, const CellSource&, juce::AudioFormatManager&);

    // chop src into a loop of targetSeconds from tiny fragments, the way a skipping CD does
    void cdSkip (const juce::AudioBuffer<float>& src, juce::AudioBuffer<float>& out,
                 double sampleRate, double targetSeconds, float intensity, juce::uint32 seed);

    // audio files inside files/folders (recursive), at most maxFiles
    juce::Array<juce::File> collectAudioFiles (const juce::Array<juce::File>&, juce::AudioFormatManager&, int maxFiles = 256);

    // plan all 64 cells from a set of audio files
    std::vector<CellSource> planFromFiles (const juce::Array<juce::File>& audioFiles, juce::AudioFormatManager&, juce::uint32 seed);

    double loopSeconds (int cell);
}

#pragma once

// TCI (Transceiver Control Interface, ExpertSDR TCI 1.9) helpers that do not
// need Qt: text command splitting/parsing and the binary stream frame format.
//
// Text: commands are "name:arg1,arg2,...;" (or "name;"), several may arrive
// in one WebSocket message. Names are case-insensitive.
//
// Binary: a 64-byte header of 16 little-endian uint32 followed by samples.
//   receiver, sampleRate, format, codec, crc, length, type, channels, reserved[8]
// length = number of samples (floats) in the data field, all channels counted.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tci
{

constexpr size_t kHeaderBytes = 64;
constexpr uint32_t kMaxSamples = 4096; // per frame, all channels

enum class SampleType : uint32_t
{
    Int16 = 0,
    Int24 = 1,
    Int32 = 2,
    Float32 = 3,
    Float64 = 4
};

enum class StreamType : uint32_t
{
    Iq = 0,
    RxAudio = 1,
    TxAudio = 2,
    TxChrono = 3,
    LineOut = 4
};

struct Header
{
    uint32_t receiver = 0;
    uint32_t sampleRate = 0;
    uint32_t format = 0;
    uint32_t codec = 0;
    uint32_t crc = 0;
    uint32_t length = 0;
    uint32_t type = 0;
    uint32_t channels = 0;
};

struct Command
{
    std::string name; // lower case
    std::vector<std::string> args; // trimmed
};

// Split one WebSocket text message into commands.
std::vector<Command> parseCommands(const std::string &text);

// Build "name:a,b,c;".
std::string makeCommand(const std::string &name, const std::vector<std::string> &args = {});

// Read the header of a binary frame. False if the frame is shorter than a header.
bool readHeader(const uint8_t *data, size_t size, Header &h);

// Samples in a frame as float32 (int16/int24/int32/float32/float64 are converted).
// Returns false if the frame is truncated or the format is unknown.
bool readSamples(const uint8_t *data, size_t size, const Header &h, std::vector<float> &out);

// Channel count to use for an audio frame. wfview 3.00 announces
// "audio_stream_channels:2" and sends interleaved stereo, but writes 1 in every
// RX audio header; the announced count is trusted over the header when the
// server is known to do this (announcedChannels > 0 and preferAnnounced).
int effectiveChannels(const Header &h, int announcedChannels, bool preferAnnounced);

// Mono from interleaved samples: the first channel (TCI's left = receiver audio).
void firstChannel(const std::vector<float> &interleaved, int channels, std::vector<float> &mono);

// Build a TX audio frame (float32, interleaved) with the given samples.
std::vector<uint8_t> makeTxAudioFrame(
    uint32_t receiver, uint32_t sampleRate, uint32_t channels, const float *samples, size_t sampleCount);

// TCI frequency/mode helpers.
bool toBool(const std::string &s);
std::string toLower(std::string s);
std::string toUpper(std::string s);

} // namespace tci

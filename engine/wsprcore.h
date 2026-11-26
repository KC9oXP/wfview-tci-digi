#pragma once

// WSPR (Weak Signal Propagation Reporter), Qt-free.
//
// Receive: wsprd by Joe Taylor K1JT and Steven Franke K9AN (WSJT-X, GPL v3),
// in the library form by Guenael Jouchet VA2GKA (rtlsdr-wsprd), with the
// sequential Fano decoder by Phil Karn KA9Q. Vendored in engine/wsprd/,
// FFTW replaced by a small built-in FFT.
//
//   audio (any rate) --Recorder (12 kHz)--> slot audio --toBaseband--> 375 Hz I/Q
//       --Decoder::decode--> spots
//
// Transmit: Modulator sends the 162 channel symbols of a message as 4-FSK,
// 1.4648 Hz apart, 0.6827 s each (110.6 s), starting 1 s into an even minute.
//
// FreeFinder keeps what was heard in the 200 Hz WSPR window over the last
// slots (decoded spots and energy in the averaged spectrum) and suggests clear
// transmit offsets. HopPlan picks the band for each 2-minute slot.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace wspr
{

constexpr int kSymbols = 162;
constexpr double kAudioRate = 12000.0; // rate of the slot recording
constexpr double kBasebandRate = 375.0; // what the decoder wants
constexpr double kToneSpacing = 12000.0 / 8192.0; // 1.4648 Hz
constexpr double kSymbolSec = 8192.0 / 12000.0; // 0.6827 s
constexpr double kTxSeconds = kSymbols * kSymbolSec; // 110.6 s
constexpr double kCentreHz = 1500.0; // audio centre of the window (USB dial + 1500 Hz)
constexpr double kWindowHalfHz = 100.0; // decoded and sent: 1400..1600 Hz audio
constexpr double kTxStartSec = 1.0; // transmit starts this long after the even minute
constexpr double kRecordLeadSec = 1.0; // the recording starts this long before it
constexpr double kRecordSec = 115.0; // recorded per slot (decode starts at 0:01:54)
constexpr double kSignalWidthHz = 4.0 * kToneSpacing; // ~6 Hz

// ---- messages --------------------------------------------------------------

bool validDbm(int dbm); // 0, 3, 7, 10 ... 60
int nearestDbm(int dbm);
// Standard call that fits a Type 1 message: up to 6 characters, a digit in the
// 2nd or 3rd place (K1ABC, KC9OXP, G4X, 2E0ABC).
bool standardCall(const std::string &call);
// A call with one '/': prefix of 1-3 characters (PJ4/K1ABC) or a suffix of one
// character or two digits (K1ABC/P, K1ABC/12).
bool compoundCall(const std::string &call);
bool validGrid(const std::string &grid); // 4 or 6 characters (EN71 / EN71ar)

// What a beacon sends, in turn: one message (standard call + 4-character grid)
// or two (Type 1 + Type 3 for a 6-character grid; Type 2 + Type 3 for a
// compound call). Empty with err set when the call / grid / power cannot be sent.
std::vector<std::string> beaconMessages(const std::string &call, const std::string &grid, int dbm,
                                        std::string *err = nullptr);
// 162 channel symbols (0..3) of a message; false if it cannot be encoded.
bool encode(const std::string &message, std::array<std::uint8_t, kSymbols> &symbols);

// ---- transmit --------------------------------------------------------------

class Modulator
{
public:
    // centreHz: audio centre of the 4 tones (1400..1600).
    void start(const std::array<std::uint8_t, kSymbols> &symbols, double centreHz);
    void stop()
    {
        m_active = false;
    }
    bool active() const
    {
        return m_active;
    }
    double progress() const; // 0..1
    int symbolIndex() const; // symbol on the air, 0..161
    double centreHz() const
    {
        return m_centre;
    }
    // Writes n samples (amplitude 1); zeros once the message is out.
    void render(float *out, size_t n, double sampleRate);

private:
    std::array<std::uint8_t, kSymbols> m_sym {};
    double m_centre = kCentreHz;
    double m_t = 0.0; // seconds sent
    double m_phase = 0.0;
    bool m_active = false;
};

// ---- receive ---------------------------------------------------------------

// 12 kHz real audio of one slot -> 375 Hz complex, centred on 1500 Hz audio.
void toBaseband(const std::vector<float> &audio12k, std::vector<float> &i, std::vector<float> &q);

struct Spot
{
    std::string message; // as decoded: "K1ABC FN42 37", "PJ4/K1ABC 37", "<K1ABC> FN42AB 37"
    std::string call; // "<...>" when a Type 3 hash was not known yet
    std::string grid; // 4 or 6 characters, empty for Type 2
    int dbm = 0;
    double snr = 0.0; // dB in 2500 Hz
    double dt = 0.0; // s, start relative to 1 s past the even minute
    double drift = 0.0; // Hz per minute
    double audioHz = 0.0; // audio centre (1400..1600)
    std::int64_t rfHz = 0; // dial + audioHz
    int cycles = 0; // Fano decoder work (lower = cleaner)
};

class Decoder
{
public:
    Decoder();
    // i/q from toBaseband (the slot's recording). dialHz for the reported RF.
    // passes: 1 quick, 2 normal (with signal subtraction), 3 deep.
    std::vector<Spot> decode(const std::vector<float> &i, const std::vector<float> &q, std::int64_t dialHz,
                             int passes = 2);
    // Calls heard with Type 1/2 messages, kept so a later Type 3 "<hash>" resolves.
    // Text form: one "index call grid" per line (wsprd's hashtable.txt).
    std::string saveHashes() const;
    void loadHashes(const std::string &text);
    size_t knownCalls() const;

private:
    std::vector<char> m_hash, m_loc;
};

// Averaged power spectrum of the window, 0.73 Hz per bin, from the baseband.
struct WindowSpectrum
{
    double binHz = 0.0;
    double firstHz = 0.0; // audio Hz of bin 0
    std::vector<float> db; // average power, dB (relative)
};
WindowSpectrum windowSpectrum(const std::vector<float> &i, const std::vector<float> &q);

// ---- free offsets ----------------------------------------------------------

class FreeFinder
{
public:
    // Forget everything (band changed).
    void clear();
    // After each received slot (slotIndex = minutes since the epoch / 2).
    void addSlot(std::int64_t slotIndex, const std::vector<Spot> &spots, const WindowSpectrum &sp);
    // Audio offsets in use (centres, Hz), merged from the remembered slots.
    struct Busy
    {
        double lowHz, highHz; // audio range taken
        bool decoded; // a decoded station (else only energy seen)
        std::string call;
    };
    std::vector<Busy> busy(std::int64_t nowSlot) const;
    // Up to 'count' clear centres in 1410..1590 Hz, best first, at least
    // minApartHz apart; each with how far it is from the nearest busy edge.
    struct Pick
    {
        int hz;
        double clearanceHz;
    };
    std::vector<Pick> suggest(std::int64_t nowSlot, int count = 3, double minApartHz = 12.0) const;
    // Distance from a signal at centreHz to the nearest busy range (big if clear).
    double clearance(std::int64_t nowSlot, double centreHz) const;
    int slotsSeen(std::int64_t nowSlot) const;

    static constexpr int kKeepSlots = 10; // 20 minutes
    static constexpr double kGuardHz = 3.0; // extra space each side of a signal
    static constexpr double kEnergyDb = 1.5; // above the median of the window

private:
    struct Slot
    {
        std::int64_t index;
        std::vector<Busy> ranges;
    };
    std::vector<Slot> m_slots;
};

// ---- band hopping ----------------------------------------------------------

struct Band
{
    const char *name; // "20 m"
    std::int64_t dialHz; // USB dial
};
// The conventional WSPR dials, 160 m .. 2 m (60 m: 5.2872 MHz; 5.3647 MHz is a
// second 60 m entry). Index order is the order shown.
const std::vector<Band> &bands();
int bandIndex(const std::string &name); // -1 if unknown
int bandForDial(std::int64_t dialHz); // the band whose window holds dialHz, -1 if none

// WSJT-X's coordinated hopping: every station hopping goes to the same band in
// the same slot. Slot k of the hour (k = minute / 2) is on band k % 10 of
// 160, 80, 60, 40, 30, 20, 17, 15, 12, 10 m.
int coordinatedBand(int minuteOfHour); // index into bands()

struct HopPlan
{
    enum Mode
    {
        Off = 0,
        Coordinated = 1, // the WSJT-X table, skipping bands not ticked
        Rotate = 2, // the ticked bands in turn, one per slot
    };
    Mode mode = Off;
    std::vector<bool> rx; // per band: hop to it
    std::vector<bool> tx; // per band: beacon allowed there
    // Band for the slot starting at minuteOfDay (even), or -1 to stay where we are.
    int bandFor(int minuteOfDay) const;
};

// ---- transmit schedule -----------------------------------------------------

// Picks transmit slots so that about 'percent' of slots are sent, never two in
// a row (like WSJT-X), and at least one in every 2 * (100 / percent) slots.
class TxScheduler
{
public:
    void setPercent(int percent)
    {
        m_percent = percent;
    }
    int percent() const
    {
        return m_percent;
    }
    // random01: a uniform random number 0..1 for this decision.
    bool decide(std::int64_t slotIndex, double random01);
    void reset()
    {
        m_lastTx = -1000000;
    }

private:
    int m_percent = 20;
    std::int64_t m_lastTx = -1000000;
};

// WSPRnet "post" query strings (sent as http://wsprnet.org/post/?<query>).
std::string wsprnetSpotQuery(const std::string &rcall, const std::string &rgrid, std::int64_t rxDialHz,
                             const Spot &s, int yymmdd, int hhmm, const std::string &version);
std::string wsprnetStatusQuery(const std::string &rcall, const std::string &rgrid, std::int64_t rxDialHz,
                               int txPercent, std::int64_t txDialHz, int dbm, const std::string &version);

} // namespace wspr

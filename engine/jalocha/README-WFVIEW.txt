Olivia / Contestia MFSK library by Pawel Jalocha (SP9VRC), (c) 1999-2004,
with changes by Dave Freese (W1HKJ) for fldigi.

Copied unchanged from fldigi (https://github.com/w1hkj/fldigi, commit
61b97f4133c488063f3de1795c894d22d5032e8a), src/include/jalocha/. fldigi is
distributed under the GNU General Public License version 3 or later; this
application is GPLv3 as well.

Used by engine/oliviacore.cpp. Same transmitter, receiver, FEC code
(Walsh-Hadamard with scrambling and interleave) and sync search as fldigi,
so the two interoperate.

One change for MSVC: pj_mfsk.h, MFSK_Demodulator, "Type SymbolProb[Carriers]"
(a variable-length array, which MSVC does not accept) became
"Type SymbolProb[256]" (the library limits Tones to 256).

wfview TCI Digi change (0.9.20), receiver only, marked "wfview TCI Digi" in
pj_mfsk.h: MFSK_Receiver keeps each decoder's block signal before the sync
integrators in RawSignalPipe (same length and pointer as DecodePipe, so it
stays in step with the blocks being output) and gives the S/N of the block
being output, BlockSignalToNoiseRatio(), and a count of output decisions,
BlockCount(). Nothing the library decodes or outputs is changed; the app's
squelch uses these (the averaged SignalToNoiseRatio() falls only slowly after
a transmission ends, which let junk characters through).

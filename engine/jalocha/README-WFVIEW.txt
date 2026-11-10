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

MT63 modem by Pawel Jalocha (SP9VRC), (c) 1999-2004, with changes by
Dave Freese (W1HKJ) for fldigi, (c) 2007-2011.

Copied from fldigi (https://github.com/w1hkj/fldigi, commit
61b97f4133c488063f3de1795c894d22d5032e8a): src/mt63/dsp.cxx,
src/mt63/mt63base.cxx, src/mt63/symbol.dat, src/mt63/mt63intl.dat,
src/include/dsp.h and src/include/mt63base.h. fldigi is distributed under the
GNU General Public License version 3 or later; this application is GPLv3 as
well.

Used by engine/mt63core.cpp. Same transmitter, receiver, FEC and interleave as
fldigi, so the two interoperate.

Changes:
- dsp.cxx, mt63base.cxx: "#include <config.h>" (fldigi's build configuration)
  removed.
- dsp.h: M_PI defined if <cmath> did not (MSVC defines it only with
  _USE_MATH_DEFINES).

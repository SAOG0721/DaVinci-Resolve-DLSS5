// SPDX-License-Identifier: MIT
#pragma once
// The FidelityFX backend dynamically resolves PIX calls; only the color macro
// is needed to build its optional instrumentation without PIX headers/runtime.
#define PIX_COLOR(r,g,b) (0xff000000ull | ((r)*1ull<<16) | ((g)*1ull<<8) | (b))

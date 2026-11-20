// Frame rate (60 / 30): hands GT5's half-rate switch to the runtime.
//
// The half-rate mod (GT5\mods\half-rate, scripts\mods.py) makes the lifted frame pacer read a plain global,
// g_mod_half_rate, at every frame instead of the game's own refresh interval: 1 = wait two refreshes a frame
// (GT5 then steps its simulation twice: real speed at 30 frames a second). The runtime's Frame rate setting
// (ps3recomp libs\video\rsx_frame_rate.c) sets it from GT5_SPEED while the game runs: no re-lift, nothing in the
// generated code or in the mod changes. Its start-up value is the mod's switch (GT5_MOD_HALF_RATE); Frame rate 30,
// or any change of it, sets it from the first vblank beat on.
//
// GT5_HAVE_HALF_RATE comes from CMakeLists.txt when src\gen\mods.cpp defines g_mod_half_rate (the mod applies
// to this EBOOT and the tree has it). Without it nothing is registered and the menu has no Frame rate row.

extern "C" void rsx_frame_rate_set_switch(int* half_rate);   // ps3recomp libs\video\rsx_frame_rate.h

#if defined(GT5_HAVE_HALF_RATE) && GT5_HAVE_HALF_RATE
extern "C" int g_mod_half_rate;

namespace {
const int s_registered = (rsx_frame_rate_set_switch(&g_mod_half_rate), 0);
}  // namespace
#endif

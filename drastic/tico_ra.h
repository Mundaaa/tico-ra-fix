/* RetroAchievements for Drastic: rcheevos driven from the host's frame loop,
 * reading the DS main RAM straight out of the memory the host gave Drastic. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Once the ROM is known: logs in with the session's account, if any. */
void tico_ra_start(const char *rom_path);
/* Once per emulated frame, on the host thread. */
void tico_ra_frame(int running);
/* Before the host tears the core down: sends what is queued, then stops. */
void tico_ra_stop(void);

#ifdef __cplusplus
}
#endif

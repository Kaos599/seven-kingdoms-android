/*
 * Seven Kingdoms Android port: LAN multiplayer helpers (see docs/multiplayer/README.md).
 *
 *   - WifiManager.MulticastLock, held only while the game's MultiPlayer object is initialised
 *     (called from MultiPlayer::init / deinit, patches/7kaa/0012-android-multiplayer.patch);
 *   - the phone's own IPv4 address(es) for the "Your address" line on the multiplayer screens;
 *   - the last address that was joined successfully this run (default of the Enter Address box);
 *   - flushing ENet before the main thread blocks in the background (sk_lifecycle.cpp).
 */
#ifndef SK_NET_H
#define SK_NET_H

#ifdef __ANDROID__

#include <stddef.h>

void sk_net_multicast_acquire(void);   // idempotent
void sk_net_multicast_release(void);   // idempotent, no-op when not held

// "Your address: 192.168.1.23" (several addresses are separated by ", "), or a "not connected" line.
// The result is cached for about two seconds; the pointer stays valid until the next call.
const char *sk_net_address_line(void);

// Last address joined successfully in this run ("" if none).
void sk_net_remember_address(const char *addr);
void sk_net_last_address(char *out, size_t out_size);

// Send everything ENet has queued (no-op outside multiplayer).
void sk_net_flush(void);

#endif // __ANDROID__

#endif // SK_NET_H

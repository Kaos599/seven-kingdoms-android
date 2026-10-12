package com.sevenkfans.sevenkingdoms;

import android.content.Context;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.util.Collections;

/**
 * Thin SDLActivity subclass. The game itself is native (libmain.so, entry point SDL_main).
 *
 * SDL already sets sticky immersive fullscreen when the game asks for a fullscreen window
 * (SDL_androidwindow.c -> setWindowStyle). What it does not do is keep the screen on, and it does
 * not re-apply the flags when the window regains focus after a status-bar swipe, a notification
 * shade or an app switch, so both are handled here.
 */
public class SevenKingdomsActivity extends SDLActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Window flag: honoured while the activity is visible, so no pause/resume handling is needed.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        // Own the whole screen including the camera cutout, so the game's letterbox maths and the
        // finger coordinates refer to one rectangle.
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        } else if (Build.VERSION.SDK_INT >= 28) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
        applyImmersive();
    }

    @Override
    protected void onResume() {
        super.onResume();
        applyImmersive();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyImmersive();
        }
    }

    /**
     * Immersive fullscreen. API 30+ (and Android 15+ edge-to-edge, which ignores the old flags) uses
     * WindowInsetsController; API 24-29 keeps the SYSTEM_UI_FLAG_* set. A swipe from the edge shows
     * the bars transiently and they hide again.
     */
    @SuppressWarnings("deprecation") // View.SYSTEM_UI_FLAG_* is deprecated on API 30+, still needed for minSdk 24-29.
    private void applyImmersive() {
        Window window = getWindow();
        if (window == null) {
            return;
        }
        if (Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController c = window.getInsetsController();
            if (c != null) {
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                c.hide(WindowInsets.Type.systemBars());
            }
            return;
        }
        window.getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    // ----- LAN multiplayer (called from native code, port/android/sk_net.cpp, on the SDL thread) -----

    private static WifiManager.MulticastLock sMulticastLock;

    /** Lets the phone receive the host's UDP broadcast (port 19383). Held only while the game's MultiPlayer object exists. */
    public static synchronized void acquireMulticastLock() {
        try {
            if (sMulticastLock != null && sMulticastLock.isHeld()) {
                return;
            }
            Context ctx = getContext();
            if (ctx == null) {
                return;
            }
            WifiManager wm = (WifiManager) ctx.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wm == null) {
                return;
            }
            if (sMulticastLock == null) {
                sMulticastLock = wm.createMulticastLock("7kaa-lan");
                sMulticastLock.setReferenceCounted(false);
            }
            sMulticastLock.acquire();
        } catch (RuntimeException e) {
            android.util.Log.w("7kaa", "multicast lock: " + e);
        }
    }

    public static synchronized void releaseMulticastLock() {
        try {
            if (sMulticastLock != null && sMulticastLock.isHeld()) {
                sMulticastLock.release();
            }
        } catch (RuntimeException e) {
            android.util.Log.w("7kaa", "multicast lock release: " + e);
        }
    }

    /** Fallback for the "Your address" line when getifaddrs returns nothing: non-loopback IPv4 addresses, comma separated. */
    public static String getLocalIpv4Addresses() {
        StringBuilder sb = new StringBuilder();
        try {
            for (NetworkInterface ni : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                if (!ni.isUp() || ni.isLoopback()) {
                    continue;
                }
                for (InetAddress a : Collections.list(ni.getInetAddresses())) {
                    if (a instanceof Inet4Address && !a.isLoopbackAddress()) {
                        String h = a.getHostAddress();
                        if (h != null && sb.indexOf(h) < 0) {
                            if (sb.length() > 0) {
                                sb.append(", ");
                            }
                            sb.append(h);
                        }
                    }
                }
            }
        } catch (Exception e) {
            android.util.Log.w("7kaa", "local addresses: " + e);
        }
        return sb.toString();
    }

    @Override
    protected String[] getLibraries() {
        // libopenal.so is a dependency of libmain.so; list it explicitly so the load order is deterministic.
        return new String[] {
            "SDL2",
            "openal",
            "main"
        };
    }
}

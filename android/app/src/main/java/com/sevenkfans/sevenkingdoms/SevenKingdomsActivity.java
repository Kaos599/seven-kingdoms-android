package com.sevenkfans.sevenkingdoms;

import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

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

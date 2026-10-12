package com.sevenkfans.sevenkingdoms;

import android.app.Activity;
import android.content.res.AssetManager;
import android.graphics.Typeface;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.ViewGroup;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/**
 * Shows the licence texts shipped in the APK (CONTRACT 4): the GPL v2 for the game
 * (gamedata/COPYING) and the music notice (gamedata/COPYING-Music.txt). Reached from the
 * launcher's long-press shortcut; the game UI is not involved.
 */
public class LicencesActivity extends Activity {
    private static final String[] LICENCE_FILES = {
        "gamedata/COPYING",
        "gamedata/COPYING-Music.txt"
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        TextView text = new TextView(this);
        text.setTypeface(Typeface.MONOSPACE);
        text.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        text.setTextIsSelectable(true);
        int pad = (int) (16 * getResources().getDisplayMetrics().density);
        text.setPadding(pad, pad, pad, pad);
        text.setText(loadAll());

        ScrollView scroll = new ScrollView(this);
        scroll.addView(text, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(scroll);
        setTitle(getString(R.string.licences_title));
    }

    /** Concatenates the licence files, separated by rules. A read failure is shown inline, not hidden. */
    private String loadAll() {
        AssetManager assets = getAssets();
        StringBuilder out = new StringBuilder();
        for (String name : LICENCE_FILES) {
            if (out.length() > 0) {
                out.append("\n\n========================================================================\n\n");
            }
            out.append("== ").append(name).append(" ==\n\n");
            try {
                out.append(readAsset(assets, name));
            } catch (IOException e) {
                out.append("Could not read ").append(name).append(": ").append(e);
            }
        }
        return out.toString();
    }

    private static String readAsset(AssetManager assets, String name) throws IOException {
        try (InputStream in = assets.open(name)) {
            ByteArrayOutputStream buf = new ByteArrayOutputStream();
            byte[] chunk = new byte[8192];
            int n;
            while ((n = in.read(chunk)) != -1) {
                buf.write(chunk, 0, n);
            }
            return new String(buf.toByteArray(), StandardCharsets.UTF_8);
        }
    }
}

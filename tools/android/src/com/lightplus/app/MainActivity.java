package com.lightplus.app;

import android.app.Activity;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.view.Gravity;
import android.view.Menu;
import android.view.MenuItem;
import android.view.ViewGroup;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

public class MainActivity extends Activity {
    private static final String PREFS = "lightplus";
    private WebView web;
    private EditText ipField;

    private SharedPreferences prefs() {
        return getSharedPreferences(PREFS, MODE_PRIVATE);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        showForm();
        String ip = prefs().getString("ip", "");
        if (!ip.isEmpty()) connect(ip);
    }

    private void showForm() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xFF0B0D12);
        root.setPadding(48, 96, 48, 48);

        TextView title = new TextView(this);
        title.setText("LightPlus");
        title.setTextColor(0xFFE8EAF0);
        title.setTextSize(26);
        title.setGravity(Gravity.CENTER_HORIZONTAL);
        root.addView(title);

        TextView hint = new TextView(this);
        hint.setText("Enter the lamp's IP address (check your router or Home Assistant)");
        hint.setTextColor(0xFF8B93A7);
        hint.setTextSize(14);
        hint.setGravity(Gravity.CENTER_HORIZONTAL);
        hint.setPadding(0, 16, 0, 32);
        root.addView(hint);

        ipField = new EditText(this);
        ipField.setHint("192.168.0.7");
        ipField.setText(prefs().getString("ip", ""));
        ipField.setTextColor(0xFFE8EAF0);
        ipField.setHintTextColor(0xFF6E7681);
        root.addView(ipField, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        Button btn = new Button(this);
        btn.setText("Connect");
        btn.setOnClickListener(v -> {
            String ip = ipField.getText().toString().trim();
            if (ip.isEmpty()) {
                Toast.makeText(this, "Enter an IP address", Toast.LENGTH_SHORT).show();
                return;
            }
            prefs().edit().putString("ip", ip).apply();
            connect(ip);
        });
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = 32;
        root.addView(btn, lp);

        setContentView(root);
    }

    private void connect(String ip) {
        if (web == null) {
            web = new WebView(this);
            WebSettings s = web.getSettings();
            s.setJavaScriptEnabled(true);
            s.setDomStorageEnabled(true);
            s.setMixedContentMode(WebSettings.MIXED_CONTENT_ALWAYS_ALLOW);
            web.setWebViewClient(new WebViewClient() {
                @Override
                public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
                    if (request.isForMainFrame()) {
                        Toast.makeText(MainActivity.this,
                                "Cannot reach the lamp - check the IP", Toast.LENGTH_LONG).show();
                    }
                }
            });
        }
        setContentView(web);
        web.loadUrl("http://" + ip + "/");
    }

    @Override
    public void onBackPressed() {
        if (web != null && web.canGoBack()) {
            web.goBack();
            return;
        }
        super.onBackPressed();
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        menu.add(0, 1, 0, "Change device IP");
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        if (item.getItemId() == 1) {
            showForm();
            return true;
        }
        return super.onOptionsItemSelected(item);
    }
}

package cn.swordforge.game;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;
import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;

/** Only packaged local assets can be displayed. No network or storage permission is needed. */
public final class MainActivity extends Activity {
    private static final String ORIGIN = "https://appassets.androidplatform.net/";
    private WebView web;
    private static boolean initialized;
    private static final Object GAME_LOCK = new Object();
    static { System.loadLibrary("swordforge"); }
    private static native boolean nativeInit(String directory);
    private static native String nativeState();
    private static native String nativeAction(String body);
    private static native boolean nativeSave();

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        synchronized (GAME_LOCK) {
            if (!initialized) {
                File saves = new File(getFilesDir(), "saves");
                if ((!saves.isDirectory() && !saves.mkdirs()) || !nativeInit(saves.getAbsolutePath())) {
                    new AlertDialog.Builder(this).setTitle("剑炉物语").setMessage("无法创建游戏存档，请检查手机可用空间。").setPositiveButton("关闭", (d,w)->finish()).show();
                    return;
                }
                initialized = true;
            }
        }
        FrameLayout container = new FrameLayout(this);
        container.setBackgroundColor(Color.rgb(19,21,24));
        container.setOnApplyWindowInsetsListener((view,insets)-> {
            view.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            return insets.consumeSystemWindowInsets();
        });
        web = new WebView(this);
        web.setBackgroundColor(Color.rgb(19,21,24));
        WebSettings settings = web.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setAllowFileAccessFromFileURLs(false);
        settings.setAllowUniversalAccessFromFileURLs(false);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        settings.setSupportZoom(false);
        settings.setBuiltInZoomControls(false);
        settings.setDisplayZoomControls(false);
        settings.setTextZoom(100);
        settings.setMediaPlaybackRequiresUserGesture(true);
        web.addJavascriptInterface(new GameBridge(), "SwordForgeAndroid");
        web.setWebChromeClient(new WebChromeClient());
        web.setWebViewClient(new WebViewClient() {
            @Override public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                return !isLocal(request.getUrl());
            }
            @Override public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
                if (!isLocal(request.getUrl()) || !"GET".equals(request.getMethod())) return denied();
                String path = request.getUrl().getPath(), file, mime;
                if ("/".equals(path) || "/index.html".equals(path)) { file="index.html"; mime="text/html"; }
                else if ("/scene.js".equals(path)) { file="scene.js"; mime="text/javascript"; }
                else if ("/dungeon.js".equals(path)) { file="dungeon.js"; mime="text/javascript"; }
                else if ("/vendor/rot.js".equals(path)) { file="vendor/rot.js"; mime="text/javascript"; }
                else if ("/vendor/ROT-LICENSE.txt".equals(path)) { file="vendor/ROT-LICENSE.txt"; mime="text/plain"; }
                else if ("/app.js".equals(path)) { file="app.js"; mime="text/javascript"; }
                else if ("/styles.css".equals(path)) { file="styles.css"; mime="text/css"; }
                else if ("/assets/forge.svg".equals(path)) { file="assets/forge.svg"; mime="image/svg+xml"; }
                else return denied();
                try {
                    Map<String,String> headers=new HashMap<>();
                    headers.put("Content-Security-Policy", "default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; script-src 'self'; object-src 'none'; frame-src 'none'");
                    headers.put("X-Content-Type-Options", "nosniff");
                    return new WebResourceResponse(mime,"UTF-8",200,"OK",headers,getAssets().open("web/"+file));
                } catch (IOException error) { return denied(); }
            }
        });
        // A debug build exposes WebView to browser tests; the delivered release is not debuggable.
        WebView.setWebContentsDebuggingEnabled((getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0);
        container.addView(web,new FrameLayout.LayoutParams(-1,-1));
        setContentView(container);
        container.requestApplyInsets();
        web.loadUrl(ORIGIN);
    }
    private static boolean isLocal(Uri url) {
        return "https".equals(url.getScheme()) && "appassets.androidplatform.net".equals(url.getHost()) && url.getPort()==-1;
    }
    private static WebResourceResponse denied() {
        return new WebResourceResponse("text/plain","UTF-8",404,"Not Found",null,
                new ByteArrayInputStream("Not found".getBytes(StandardCharsets.UTF_8)));
    }
    private final class GameBridge {
        @JavascriptInterface public String state() { synchronized(GAME_LOCK) { return nativeState(); } }
        @JavascriptInterface public String action(String body) {
            synchronized(GAME_LOCK) {
                if (body == null || body.length()>1024) return nativeAction("");
                return nativeAction(body);
            }
        }
        @JavascriptInterface public void exit() { runOnUiThread(()->finish()); }
    }
    @Override public void onPause() {
        synchronized(GAME_LOCK) { if(initialized) nativeSave(); }
        if(web!=null) web.onPause();
        super.onPause();
    }
    @Override public void onResume() {
        super.onResume();
        if(web!=null) {web.onResume(); web.evaluateJavascript("if(typeof connect==='function')connect();",null);}
    }
    @Override public void onBackPressed() {
        if(web==null) {finish();return;}
        web.evaluateJavascript("window.androidBack && window.androidBack()", result->{
            if(!"true".equals(result)) new AlertDialog.Builder(this).setTitle("暂歇炉火？")
                .setMessage("进度会保存。下次打开游戏继续冒险。")
                .setNegativeButton("继续游戏",null).setPositiveButton("保存退出",(d,w)->{
                    synchronized(GAME_LOCK) {nativeSave();} finish();
                }).show();
        });
    }
    @Override public void onDestroy() {
        if(web!=null) {web.removeJavascriptInterface("SwordForgeAndroid");web.destroy();web=null;}
        super.onDestroy();
    }
}

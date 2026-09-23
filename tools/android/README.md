# LightPlus Android app

A minimal Android wrapper (WebView) around the LightPlus dashboard. On first
launch it asks for the lamp's IP address, then loads `http://<ip>/` fullscreen —
WebSocket, scenes and animations all work exactly like in a browser.

The menu (⋮) has **Change device IP** to point at a different lamp or network.

## Build (no Android Studio required)

Requirements:

- JDK 17 — `https://aka.ms/download-jdk/microsoft-jdk-17-windows-x64.zip`
- Android command-line tools — `https://dl.google.com/android/repository/commandlinetools-win-11076708_latest.zip`
- SDK packages: `sdkmanager "platform-tools" "platforms;android-34" "build-tools;34.0.0"`

Expected layout:

```
%USERPROFILE%\android-sdk\
├── jdk17\
└── sdk\
    ├── build-tools\34.0.0\
    └── platforms\android-34\
```

Build:

```powershell
powershell -ExecutionPolicy Bypass -File tools/android/build.ps1 -Jdk "$env:USERPROFILE\android-sdk\jdk17"
```

Output: `dist/LightPlus.apk` (debug-signed — fine for sideloading, not for Play Store).

## Install

1. Copy `LightPlus.apk` to the phone (USB, Drive, etc.)
2. Tap it and allow "install from unknown sources" if prompted
3. Open the app, enter the lamp's IP (e.g. `192.168.0.7`), Connect

## Notes

- `android:usesCleartextTraffic="true"` is required so the app may talk to the
  lamp over plain HTTP/WS on the local network
- The app is a thin shell — no lamp logic lives here, updates to the dashboard
  ship via the lamp's filesystem, not via the APK
- Package `com.lightplus.app`, minSdk 24 (Android 7+), targetSdk 34

# Shorter video click arbitration

The user asks to shorten the perceived delay for a video single click. Version 2.2.4 waits for the entire system double-click interval. Keep the existing pending-click contract, cap the application interval at 200 ms, and respect a shorter system interval. Do not change Windows mouse preferences.

Both timeout dispatch and video double-click acceptance must use the same interval. If a native WM_LBUTTONDBLCLK arrives after that interval, route its second press through the ordinary single-click path, including when the timer has not yet dispatched. This avoids a pause followed by unintended fullscreen. Keep title-bar, divider, control-button and long-hold behavior.

Alternatives considered: immediate single-click plus rollback recreates the transient play/pause bug; changing only the timeout leaves slow native double-click messages inconsistent. A shared bounded interval plus late-message fallback is the smallest consistent change.

Back up source and 2.2.4 user/release identity first; reproduce delayed dispatch on unchanged production source with the new native message test. Test both play states, early/late double clicks, timer ordering, EOF, cancellation, button immediacy, configured long hold and real playback. Stage and package 2.2.5 with the fixed UCRT toolchain and unchanged DLL/model payload; verify the exact EXE, preserve user data, atomically publish and remove only this run's staging copies.

The existing UI timer samples every 50 ms. The 200 ms decision threshold is not a guarantee of exact wall-clock response under OS scheduling or a blocked UI thread. Tests use controlled event timing and separately report real elapsed dispatch.

Context7 was invoked first but its transport failed. The official Win32 documentation confirms the native down/up/double-click/up sequence and system interval semantics: [WM_LBUTTONDBLCLK](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-lbuttondblclk), [GetDoubleClickTime](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getdoubleclicktime).

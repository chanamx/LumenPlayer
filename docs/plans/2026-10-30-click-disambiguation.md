# Video click disambiguation

The user reported that a video double click first executes play/pause. The current 2.2.2 path toggles on the first release and calls Play/Pause again to undo it when WM_LBUTTONDBLCLK arrives. Fix this shared input path without changing playback backends.

- Keep a video single click pending until GetDoubleClickTime expires; a double click cancels it and only changes fullscreen. Remove the previous-state rollback.
- Keep bottom-bar buttons immediate, including the second activation carried by WM_LBUTTONDBLCLK. Distinct ordinary video clicks must each retain their single-click meaning.
- Cancel pending clicks when focus, capture, settings, media or another explicit control action changes. Queue a completed single click after normal ReleaseCapture, because ReleaseCapture itself sends WM_CAPTURECHANGED.
- Add one native regression mode using actual window messages and a counting backend, so a transient Play/Pause call fails even if the final state is restored. Cover playing, paused, EOF, single timeout, cancellation, independent clicks, repeated double clicks, triple clicks and rapid control buttons. Update the existing real-media assertion to the new contract.
- Preserve the 2.2.2 sources, EXE and all current user files. Demonstrate failure on unchanged production source, then run the candidate's targeted and full regressions. Build/package 2.2.3 with the existing fixed UCRT/runtime and publish only verified file replacements.

Official event ordering: [WM_LBUTTONDBLCLK](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-lbuttondblclk), [GetDoubleClickTime](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getdoubleclicktime), [WM_CAPTURECHANGED](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-capturechanged). Context7 was queried first but returned unrelated OLE entries; the exact Microsoft pages were then read directly.

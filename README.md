<div align="center">
	<img src="icon.png" style="width: 64px;">
	<h1>UWPSpy</h1>
</div>

An inspection tool for UWP and WinUI 3 applications. Seamlessly view and
manipulate UI elements and their properties in real time.

[🏠 Homepage](https://ramensoftware.com/uwpspy)

![Screenshot](screenshot.png)

## Usage

- Download the latest release from
  [here](https://ramensoftware.com/downloads/uwpspy.zip).
- Run **UWPSpy.exe** and select the UWP/WinUI 3 application you want to spy on.
- A window with the application's UI elements will appear for each UI thread of
  the target application.

## Watch subtree changes (watcher branch)

Right-click an element and choose **Watch and export changes...**, then enter
an existing output folder in the modeless dialog and click **Start**. The folder
defaults to the process temporary folder. A shell folder picker is deliberately
avoided on the inspected UI thread. Close the dialog or click **Stop /
Close** to stop. One watcher can run per inspection window. Closing/hiding the
inspection window also stops its watcher.

After a two-second delay, the watcher exports the selected subtree with properties, then
reads it again every two seconds after the previous capture finishes. Only
changed snapshots are saved, as UTF-8 text in a unique `UWPSpy-{GUID}` session
folder. Filenames contain a sequence number and UTC timestamp. Element handles
and zero-based child indices help identify replacements and changes in order.
Enable **Export + screenshot** before clicking Start to save a matching PNG
whenever the text snapshot changes (including the first snapshot). The PNG
captures the selected element's visible screen rectangle, clipped to the desktop,
and temporarily hides this inspection window's yellow selection outline. It
captures covering windows too: keep the selected element unobscured. Visible
flyouts can be captured; the initial two-second delay gives time to open one.
PNG bytes count toward the 100 MiB session limit. Capture is limited to 16 million
pixels; missing/empty bounds or a capture/write failure stop the watcher. A
failed pair may leave its text file without a PNG. Images and properties are
captured sequentially, not atomically. Image-only changes do not trigger exports.

The clipboard is untouched. An empty flyout root can be watched before opening
its flyout, provided the root itself remains alive.

Select a small subtree such as an app button's `IconPanel`. Property queries
and file writes run on the inspected UI thread, so large trees or slow/network
folders can make the target less responsive. Polling can miss changes between
samples, and a capture is not an atomic snapshot of the UI or a screenshot.
Changes to property text, geometry, or visual states also trigger an export.

Recording stops when the selected element or an ancestor is removed, on an
export error, or after 200 saved snapshots / 100 MiB. A single output is limited
to 10 MiB and watched traversal to 256 levels. It does not retarget a replacement
element. Start a new watch after closing the status window. Existing snapshots
are never deleted; a failed write may leave a `.partial` file for diagnosis.
Because UWPSpy runs inside the inspected process, process exit closes the
watcher as well; completed files remain in the session folder.

For the notification-badge investigation, begin watching the app's IconPanel
while its badge looks correct, let a new notification arrive, and compare the
last good and first bad snapshots. Keep the inspection window open overnight.

## Demo

[![Demo video](screenshot-video.png)](https://youtu.be/Zxgk_BOVpfk)
*Click on the image to view the demo video on YouTube.*

## Supported Windows versions

UWPSpy uses the [XAML Diagnostic
APIs](https://learn.microsoft.com/en-us/windows/win32/api/xamlom/nf-xamlom-initializexamldiagnosticsex)
which were added in Windows 10, version 1703. Earlier versions of Windows are
not supported.

## Dark mode support

UWPSpy supports dark mode on Windows 11 24H2 (build 26100.6899+) and 25H2 (build
26200.6899+). When the system dark mode setting is enabled, UWPSpy will
automatically use a dark theme.

To disable dark mode (e.g. if you use custom theming or encounter visual
issues), set the environment variable `UWPSPY_DISABLE_DARK_MODE=1` before
launching.

## References

- The
  [`ExplorerTAP`](https://github.com/TranslucentTB/TranslucentTB/tree/release/ExplorerTAP)
  part of the [TranslucentTB](https://github.com/TranslucentTB/TranslucentTB)
  project. That's the only usage example of the XAML Diagnostic APIs I could
  find on the internet.
- [Ahmed Walid (ahmed605)](https://github.com/ahmed605), thanks for helping with
  many UWP-related questions.

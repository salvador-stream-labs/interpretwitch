# OBS browser panel interface

`browser-panel.hpp` is unmodified upstream source from obsproject/obs-browser,
commit `3f0a2cdf378939ebe3c6f9ab36d4ea100c25aac2` (the submodule pinned by OBS 32.2.2):

https://github.com/obsproject/obs-browser/blob/3f0a2cdf378939ebe3c6f9ab36d4ea100c25aac2/panel/browser-panel.hpp

OBS Browser is GPL-2.0-or-later; see the plugin's `../../LICENSE`.
Only the interface header is included. No browser binary or CEF implementation
is bundled. The experimental adapter requires browser panel ABI version 3 and
an existing widget that identifies itself as `QCefWidget` before invoking it.
It does not include OBS's internal BrowserDock or Twitch authentication classes.

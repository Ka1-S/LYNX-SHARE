LYNX plugins @LABEL@ (@OS@)
==========================

LYNX SHARE's own effects and instruments as plugins for other hosts: FL Studio and any other CLAP or VST3 host. This is an
optional download: LYNX SHARE itself doesn't need it.

What's inside
-------------
  CLAP/LYNX Plugins.clap   every plugin in PLUGINS.txt, in one file
  VST3/*.vst3              the plugins marked "VST3" in PLUGINS.txt, one bundle each
  PLUGINS.txt              the list
  LICENSE, THIRD_PARTY.md  licences (AGPL-3.0; the source is at https://github.com/Ka1-S/LYNX-SHARE)

Use the CLAP if your host loads CLAP plugins: it holds every plugin. Each VST3 carries the whole LYNX engine, so only a
short list comes as VST3.

Install
-------
Windows   Right-click install.cmd and choose "Run as administrator". It copies the CLAP to
          C:\Program Files\Common Files\CLAP and the VST3 bundles to C:\Program Files\Common Files\VST3.
          By hand: copy the same files into those folders.
macOS     Open Terminal in this folder and run:  sh install.sh
          It copies them to ~/Library/Audio/Plug-Ins/CLAP and ~/Library/Audio/Plug-Ins/VST3 and clears the download
          quarantine. The bundles are signed ad hoc only (no Apple Developer ID yet).
Linux     Run:  sh install.sh
          It copies them to ~/.clap and ~/.vst3.

Then rescan the plugins in your host. FL Studio: Options > Manage plugins > Find installed plugins (FL Studio finds
VST3 only in the system folder above). Uninstall: run the same script with --uninstall (Windows: install.cmd /uninstall).

Check the download
------------------
Compare the archive's SHA-256 with the line in LYNX-Plugins-@LABEL@-SHA256SUMS.txt from the same release:
  Windows (PowerShell)  Get-FileHash <file> -Algorithm SHA256
  macOS                 shasum -a 256 <file>
  Linux                 sha256sum <file>

More: https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGIN_EXPORT.md

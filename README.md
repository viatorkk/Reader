# Rish

Rish is a PC-only derivative of Reader focused on local reading.

## Scope

- Keep local reading for TXT and EPUB.
- Keep display settings, bookmarks, table of contents, shortcuts, auto page, tray behavior, and local reading cache.
- Add display controls for global window opacity and shortcut-driven opaque text over a transparent local-reading background.
- Add an embedded WeRead WebView page for `https://weread.qq.com/` with a configurable refresh shortcut.
- Remove online novel/book-source entry points from the default build.
- Remove network book-source dependencies from the Visual Studio project.

## Removed From The Default Build

- Online novel search.
- Book source management and `bs.json` import/export UI.
- `.ol` online book creation/opening path.
- Online chapter fetching and update checking.
- Proxy and version-update network dependencies.

## Build

Open `Rish.sln` in Visual Studio 2019 or newer and build `Debug|x86`, `Release|x86`, `Debug|x64`, or `Release|x64`.

The main project is `Rish/Rish.vcxproj`. The legacy `Reader.vcxproj` filename is kept as a compatibility copy and uses the same no-online-source build settings.

The embedded WeRead page uses Microsoft Edge WebView2. The SDK is vendored under `packages/Microsoft.Web.WebView2`; the target machine still needs the Microsoft Edge WebView2 Runtime installed.

## Default Shortcuts

| Action | Shortcut |
| --- | --- |
| Hide/show border | `F1` |
| Toggle opaque text with transparent reading background | `F2` |
| Refresh WeRead WebView | `F5` |
| Open file | `Ctrl + O` |

## Notes

This first cut intentionally keeps the original cache structure so existing local reading settings and file history remain easier to migrate. Online book-source fields may still exist in internal structs and legacy resource IDs, but they are not exposed through the Rish UI and are not compiled through `ENABLE_NETWORK`.

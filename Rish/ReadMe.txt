# Rish

Rish is a PC-only derivative of Reader focused on local TXT/EPUB reading, with a small WebView-based online bookstore entry.

## Scope

- Keep local reading for TXT and EPUB.
- Keep display settings, bookmarks, table of contents, shortcuts, auto page, tray behavior, and local reading cache.
- Keep WebView entries for WeRead and user-defined URLs under the online bookstore menu.
- Remove legacy online novel/book-source dependencies from the Visual Studio project.

## Removed From The Default Build

- Online novel search.
- Book source management and `bs.json` import/export UI.
- `.ol` online book creation/opening path.
- Online chapter fetching and update checking.
- Proxy and version-update network dependencies.

## WebView Online Bookstore

- The online bookstore menu contains built-in entries such as WeRead and user-added URL entries.
- User-added URLs store a display name, URL, and zoom level.
- URLs without a protocol first try HTTPS and then fall back to HTTP.
- Left click opens a custom URL entry. Right click deletes custom entries. Built-in entries cannot be deleted.
- F5 and the WebView toolbar refresh the active online bookstore page.

## Display Settings

- The default body font is Chinese No.5, 10.5pt. The title font remains larger for chapter headings.
- Background color, body text color, and chapter text color can be sampled with picker controls in the display settings dialog.
- Legacy default 10pt/12pt/16pt Microsoft YaHei UI body fonts are normalized to the new 10.5pt default during cache loading.

## Parsing Hardening

- EPUB loading applies size/count limits before extracting entries.
- JSON/config string copies use bounded buffers.
- URL decoding accepts explicit input lengths to avoid reading past malformed input.

## Build

Open `Rish.sln` in Visual Studio 2019 or newer and build `Debug|x86`, `Release|x86`, `Debug|x64`, or `Release|x64`.

The main project is `Rish/Rish.vcxproj`. The legacy `Reader.vcxproj` filename is kept as a compatibility copy and uses the same no-online-source build settings.

## Notes

The original cache structure is intentionally kept so existing local reading settings and file history remain easier to migrate. Legacy online book-source fields may still exist in internal structs and resource IDs, but they are not exposed through the Rish UI and are not compiled through `ENABLE_NETWORK`.

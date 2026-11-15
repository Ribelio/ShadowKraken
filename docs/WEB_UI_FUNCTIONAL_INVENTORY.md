# Web UI functional inventory and redesign parity ledger

**Inventory source:** `web/index.html`, `include/sm_api_protocol.h`, `src/sm_api_service.c`, `include/sm_config_file.h`, `include/sm_types.h`, and `include/sm_config_mount.h` at `ff6c7cc49ed70c7cfd0ec1040afcfee500b98f4d` (the current branch base when this inventory was prepared). The working tree contains an in-progress visual redesign; the ledger below treats that work as presentation only and checks it against this baseline.

This document records the existing capabilities before the redesign and the surface they must retain. The design reference changes layout and visual hierarchy; it does not authorize removing behavior or replacing production API calls with mock data.

## 1. Existing application surfaces

### Library

- Fetches application version and game inventory from the live API; shows connection state and reports API errors.
- Searches by game name, title ID, content ID, or source path. Filters by all games, folders, and images; sorts by default order, name, or recent play time.
- Refreshes inventory and storage, scans now, optionally requests size measurements, and presents total/image/folder/PKG/mounted counts and available storage.
- Uses source metadata for card artwork and title metadata. Selects a title by mouse hover/click, keyboard, or controller; the detail view is a separate, full-fidelity action surface.
- Details include platform, source/image type, mount status, title/content IDs, version, source/runtime/backport paths, install and last-launch times, system/source/patch/DLC/save/download size information, and installed patch/DLC data when available.
- Runtime actions: mount, unmount, and mount-mode selection where supported. Replacing another mounted title requires confirmation. Mount/remount errors and platform busy reasons are surfaced.
- Source actions: copy, move, unpack image, delete source, and uninstall installed title. These actions preserve separate physical-source deletion vs. application uninstall semantics. Unpack offers an explicit delete-source-after-success option. Destructive actions have confirmation dialogs.
- Patch and DLC management: installed patch removal; DLC listing with content ID/version/size and per-item removal; base-game uninstall. Backport install/delete is available through Installation, not removed from game details.
- Per-title fakelib controls: effective/default/override mode selection and exclusion from global fakelib. Changes are persisted and explicitly apply on the next launch.
- Long-running source operations expose state, progress, bytes, speed, elapsed/remaining time, completion/failure/cancellation, and cancellation where supported. A recent-operation list is persisted in browser storage.

### Installation

- Lists local package roots and supported destination storage, including external and configured SMB destinations; reports capacity, filesystem, read-only status, and busy/unavailable errors.
- Browses folders and package files with breadcrumb/path, root/home, up, refresh, pagination/truncation, and icon metadata.
- Installs local PKG files and supports browser-to-console PKG streaming. Browser selection validates basic package structure/metadata, queues base packages before patches and DLC, and shows specific invalid/base-missing/duplicate/queue-full/source-lost failures.
- Provides a persistent installation queue, queue count, clear, start, and per-entry operations. Queue capacity is 32. Running queue and streaming state disables conflicting operations.
- Exposes install/replace/update and add-to-queue actions for base games, patches, and DLC based on package metadata and current install state.
- Installs and deletes backports. Browsed source entries support install/delete with confirmation where destructive. Package icons are served by the API.
- Installation runs through progress/background-job reporting and shares the storage-operation exclusion and error reporting used elsewhere.

### Settings / Console

- **Overview:** console/internal and external storage capacity plus recent operation history.
- **System:** debug logging, quiet mode, local network/API reachability, and fan target.
- **Library:** default fakelib mode, direct plaintext debug FPKG feature toggle, fakelib emulator update, AMPR update, unavailable-icon cleanup and delay, and optional size calculation.
- **Sources:** recursive scan-root management and per-title manual source management, path browser integration, forced scan, and one-shot attempt-counter reset.
- **SMB:** list, activate/deactivate, edit/create/remove, connection check, guest/authenticated credentials, saved-password retention/clear, read-only mode, and confirmation/error states.
- **Logs:** view/refresh/copy debug log and view kernel/system diagnostic log.
- Settings save is explicit, shows dirty/clean state, validates inputs, saves settings and scan paths together, and reports save/reload status. API responses/errors are translated to user-facing messages.
- Language selection offers 31 locales and persists the choice client-side. Existing base and extended translation catalogs, interpolated messages, labels, validation, confirmation, progress, and error strings are part of the UI contract.

## 2. Production HTTP API surface

JSON operations use `POST` with JSON request bodies and structured status/error responses. The two PKG range routes have dedicated request/chunk behavior; chunk upload uses a binary request body. `GET` serves the page, favicon, title artwork and package icons. `OPTIONS` supports preflight. Unsupported methods are rejected.

| Method and exact route | Existing capability / UI consumer |
|---|---|
| `GET /` | Serves the web application. |
| `GET /favicon.png` | Serves the ShadowKraken launcher/web icon. |
| `POST /api/v1/version` | API/build version and connection state. |
| `POST /api/v1/storage` | Storage inventory/capacity and remote-share capacity. |
| `POST /api/v1/storage/eject` | Safely eject eligible external storage. |
| `POST /api/v1/images` | Mounted/known image inventory. |
| `POST /api/v1/games` | Library inventory; optionally includes size measurements. |
| `POST /api/v1/games/info` | Expanded installed-title, patch, DLC, and size metadata. |
| `POST /api/v1/games/fakelib` | Per-title fakelib mode/global-overlay exclusion. |
| `GET /api/v1/games/icon` | `sce_sys/icon0.png` card/detail artwork (`title_id`, optional thumbnail query). |
| `GET /api/v1/games/background` | Selected-title `sce_sys/pic1.png` background (`title_id`, version query). |
| `POST /api/v1/games/mount` | Runtime mount lifecycle. |
| `POST /api/v1/games/unmount` | Runtime unmount lifecycle. |
| `POST /api/v1/games/uninstall` | Installed title uninstall request. |
| `POST /api/v1/games/content/uninstall` | Installed patch or DLC removal. |
| `POST /api/v1/games/move` | Move a managed game source. |
| `POST /api/v1/games/copy` | Copy a managed game source. |
| `POST /api/v1/games/delete` | Delete a managed physical source. |
| `POST /api/v1/games/unpack` | Unpack a supported image source. |
| `POST /api/v1/games/storage/status` | Poll background storage-operation progress/state. |
| `POST /api/v1/games/storage/cancel` | Cancel an eligible background storage operation. |
| `POST /api/v1/manual/list` | List manual library sources. |
| `POST /api/v1/manual/add` | Add a manual source. |
| `POST /api/v1/manual/remove` | Remove a manual source. |
| `POST /api/v1/settings` | Read settings exposed to the web UI. |
| `POST /api/v1/settings/update` | Atomically save web-managed settings and scan roots. |
| `POST /api/v1/smb` | List, check, add, edit, remove, activate, or deactivate an SMB connection. |
| `POST /api/v1/debug-log` | Read the bounded debug-log tail. |
| `POST /api/v1/kernel-log` | Read kernel/system diagnostics. |
| `POST /api/v1/scan` | Request a scan, optionally resetting retry state. |
| `POST /api/v1/packages` | Browse installation roots and package/source folders. |
| `POST /api/v1/files` | Browse destination/path-picker roots and folders. |
| `POST /api/v1/files/mkdir` | Create a directory in an eligible destination. |
| `POST /api/v1/sources/install` | Install/register browsed source content. |
| `POST /api/v1/sources/delete` | Delete browsed source content. |
| `POST /api/v1/backports/install` | Install or replace a recognized backport. |
| `POST /api/v1/backports/delete` | Delete a recognized backport. |
| `POST /api/v1/packages/install` | Install a local package. |
| `POST /api/v1/packages/queue` | Read/control the package installation queue. |
| `GET /api/v1/packages/icon` | Serve a package-browser icon. |
| `POST /api/v1/packages/stream` | Create a browser-to-console PKG streaming installation. |
| `POST /api/v1/packages/stream/request` | Request the next browser file range for a stream session. |
| `POST /api/v1/packages/stream/chunk` | Upload one requested binary file chunk. |

The UI's API wrapper preserves HTTP/status/error reason information, translates known operation-block reasons, and shows fallback messages for unknown failures. API calls and returned job state remain the business-logic boundary; the redesigned cards/rows are not an alternate implementation.

## 3. Configuration inventory

### Web-managed settings

The existing Settings UI and `/api/v1/settings/update` manage: `debug`, `quiet_mode`, `update_emulators`, `auto_update_ampr`, `fpkg_direct_mount`, `auto_remove_missing_games`, `auto_remove_missing_delay_seconds`, `allow_lan_access`, `fan_target_temperature`, `fakelib_default_mode`, and the list of custom `scan_paths`. On this branch, `fpkg_direct_mount` defaults to enabled and remains user-toggleable. `include_size` is a per-request UI preference for inventory refresh, not a persisted backend config key. Language is stored by the browser UI. Per-title fakelib and global-overlay exclusion are saved through the title-specific API, not the global settings form. SMB connections have their own API/config flow.

### Configuration keys supported by the config layer

The config migration/reader recognizes these canonical `config.ini` section/key pairs (legacy aliases are accepted by the migration layer):

- `[config] version`
- `[general] debug`, `quiet_mode`, `library_sync_notification`, `language`
- `[api] enabled`, `bind_address`, `port`
- `[scan] depth`, repeated `path`, `interval_seconds`, `stability_wait_seconds`
- `[mount] mode`, `force`, `persistent_images`, `exfat_backend`, `ufs_backend`, `nested_pfs_index_cache`
- `[library] install_all`, `remove_missing`, `remove_with_dlc`, `remove_delay_seconds`
- `[fpkg] direct_mount`
- `[fakelib] enabled`, `default_mode`, `global_enabled`, `global_path`, `global_priority`
- `[emulators] update_enabled`, `path`, `auto_update_ampr`, `ampr_update_url`
- `[fan] target_temperature`
- `[sectors] lvd_exfat`, `lvd_ufs`, `lvd_pfs`, `md_exfat`, `md_ufs`

The web form intentionally manages only its existing subset. Advanced API bind/port, scan timing/depth, mount/backend/persistence, global fakelib routing, emulator paths, AMPR URL, and sector tuning remain configuration-file/platform settings. Saving the web subset preserves unrelated keys and comments. A redesign must not imply these advanced keys are newly editable in the UI.

### Per-title, per-image, and SMB configuration

- `profiles.ini` `[game:<TITLE_ID>]`: `fakelib` (`default`, `full`, `emulators`, or `disabled`) and `ignore_global_fakelib` (boolean or `default`). The web UI edits these through the per-title details controls.
- `profiles.ini` `[image:<filename>]`: `mount_mode` (`ro`, `rw`, or `default`) and `sector_size` (a power of two from 512 bytes through 1 MiB); these are profile-file settings, not global Settings controls.
- `config.ini` `[smb:<name>]`: `path`, `username`, `password`, `active`, and `read_only`. The SMB Settings section edits these through the separate SMB API flow; a blank password while editing retains the saved credential unless the explicit clear-password action is selected.

The config reader also accepts legacy key aliases during migration. They are compatibility inputs, not additional independent settings.

## 4. Dialogs, confirmations, and errors

Existing modal workflows: game details/actions; copy/move/unpack destination selection and custom path browsing; path picker with storage roots, parent navigation, create-folder and paging; logs with refresh/copy; language selection; and generic confirmation. Transfer progress is a persistent operation panel rather than a modal. Toast/status messages report success and failure.

Confirmation coverage includes remounting over an existing title, application uninstall, physical source deletion, patch/DLC removal, source-manifest deletion, SMB removal, storage eject, package replacement/install as applicable, and deleting an image after successful unpack. Operation errors distinguish busy/runtime/scanner/install/storage/USB and PKG-specific reasons when the API returns them. Error state, disabled/busy controls, stale source rejection, queue conflicts and fallback API messages must remain reachable and visible.

## 5. Keyboard and PS5 controller behavior

- Pointer, standard tab focus, and arrow/Enter/Space controls coexist. Focus movement updates the selected game and its hero without requiring activation; details and runtime actions remain available from the selected title.
- The PS browser maps D-pad/left stick to directional movement, Cross to accept, Circle/Escape to back/close, and L1/R1 (browser history) to switch among Library, Installation, and Settings. It avoids stealing native text-editor/search input, suppresses browser-native key behavior where needed, and pauses/restarts the polling loop across visibility/page lifecycle changes.
- The control bar communicates the active controller legend. Modal focus/back handling, operation cancellation, path paging, queue entries, game rows, and settings controls must remain navigable. New rails, insight rows, and menus must join the same focus model and use real buttons with accessible names/state.

## 6. Artwork and caching

The backend already publishes versioned game `icon_url` and `background_url`. `icon0.png` is resolved for cards/details; the selected game's `pic1.png` supplies the Library hero when available. The front end caches/falls back for card images and cross-fades between two hero image layers. Artwork is resolved from the title's tracked/runtime source using fixed artwork names; API handling streams the image instead of embedding image bytes into game JSON. Missing/invalid artwork falls back to a neutral cover/gradient. Keep the existing API/cache contract and avoid rescanning a package per paint/render.

## 7. Redesign parity ledger

| Baseline capability | Intended new surface | Parity invariant |
|---|---|---|
| Library search, filter, sort, scan, refresh, size and storage | Library toolbar, quick filters, status/format rows | All baseline controls remain live and act on the same inventory |
| Selected-game hero, artwork, metadata and runtime actions | Compact hero/banner | Mouse, keyboard, or controller focus changes title/art/meta/actions together; missing art leaves a stable fallback |
| Details, storage actions, install management, fakelib, patch/DLC | Context menu and detail view | Every baseline action remains reachable; same production APIs, confirmation and error paths |
| PKG folder browser, upload/stream, queue and backports | Installation sidebar/content/queue | No installation path or queue operation is removed or reduced to visual-only controls |
| Storage, settings sections, paths, SMB, logs and diagnostics | Settings navigation/content panels | Every input and action remains present, validated, saveable, localized, and controller-focusable |
| Dialogs, confirmations, job progress, operation errors | Contextual dialog/overlay | Same confirmation points, operation lifecycle, translated errors, and cancellation behavior |
| Card/hero artwork endpoints | `icon0.png` cards; `pic1.png` selected hero | Actual per-title artwork is used where present; no remote/AI artwork; neutral missing-art fallback |

**Intentional presentation changes only:** persistent sidebar in place of the compact top navigation; a contextual selected-game hero; grouped Library insights; compact Installation heading that shows the current directory on the right; Settings section navigation; styling, spacing, and transition changes. These changes must not alter API contracts, configuration values, operation semantics, translation coverage, or destructive-action confirmations.

**Unpack safety:** direct FPKG titles inside an image do not offer image unpacking. Their runtime source is a package file, not an extracted game directory; the API rejects this operation and rechecks the runtime source type before copying or deleting anything. Ordinary image-backed directory titles retain unpack support.

# Inspector IPC client

The inspector exposes one local named pipe per inspected process while its
existing GUI inspection windows are open. Attach manually using UWPSpy; the
launcher does not expose IPC in this version. No separate service is required.
The Python 3 client uses only the standard library and can be imported as a
library (`Client`, `endpoints`, and `parse_dump`).

Build the solution as Release/x64 with Visual Studio 2022. If an older UWPSpy DLL
is already loaded in the target process, end that inspection session and ensure
the new DLL is loaded before connecting. The current development build is in
`x64/WatcherIpc/`.

Run `x64/WatcherIpc/UWPSpyLauncher.exe`, attach to Explorer, and leave the
inspection windows open. Discover endpoints and inspect the trees:

```powershell
python .\tools\uwspy_cli.py endpoints
# Copy a literal line above, or select explicitly from the returned lines:
# $endpoint = @(python .\tools\uwspy_cli.py endpoints)[0]
$endpoint = '\\.\pipe\UWPSpy-1234-{paste-the-actual-session-guid}'
python .\tools\uwspy_cli.py trees --endpoint $endpoint
python .\tools\uwspy_cli.py find --endpoint $endpoint --contains Thunderbird
```

Copy the `tree`, `handle`, and `generation` from a matching element:

```powershell
python .\tools\uwspy_cli.py get --endpoint $endpoint --tree 1 --handle 123456 --generation 87
python .\tools\uwspy_cli.py capture --endpoint $endpoint --tree 1 --handle 123456 --generation 87 --label before-badge --output C:\captures --screenshots
python .\tools\uwspy_cli.py watch --endpoint $endpoint --tree 1 --handle 123456 --generation 87 --label waiting --output C:\captures --screenshots
python .\tools\uwspy_cli.py label --endpoint $endpoint --tree 1 --label badge-recreated
python .\tools\uwspy_cli.py events --endpoint $endpoint --follow
python .\tools\uwspy_cli.py stop --endpoint $endpoint --tree 1
```

Output folders must already exist. `get` writes no files and returns the dump
plus parsed nodes. Local and other property-source values remain separate;
`other` values must not be mistaken for effective values when a local override
exists. Version 1 selectors are `--type`, exact `--name` (AutomationProperties.Name),
and `--contains`, optionally scoped to a tree. A general path/query grammar is
not implemented. Finding across trees returns every match; it never silently
chooses the first. References are scoped to the endpoint session, tree, handle,
and generation. A replaced element requires another find.

## Protocol and lifecycle

The endpoint is `\\.\pipe\UWPSpy-<PID>-<session GUID>`. Its DACL permits the
inspected process's user only, and remote pipe clients are rejected. Use the
same user and compatible integrity level. The pipe worker performs all IPC I/O;
it posts requests to the selected inspector's UI thread. XAML access and the
existing export/screenshot work still execute on that UI thread. Thus large
captures or slow output storage can stall the target: use small subtrees and
local folders. This first version does not move file encoding/writing out of
the existing GUI exporter.

Requests and responses are UTF-8 JSON, prefixed with a little-endian uint32 byte
length. Maximum request is 64 KiB; response is 16 MiB. After reading a response,
the client sends one acknowledgement byte, then closes. The supplied client
opens one connection per request; inspection state persists across connections.
A single worker serializes requests. I/O and UI dispatch deadlines are 15 s.
A timed-out queued request is canceled; an already running UI operation can
still finish. Do not blindly retry capture/watch mutations after a timeout.

Process-level operations: `trees`, `events` (with decimal-string `after`).
Tree operations: `find`, `get`, `capture`, `watch`, `label`, `stop`.
Every normal response echoes `request_id` and endpoint; UI responses include
capture start/end UTC timestamps and the current label. Tree IDs distinguish
inspection-window lifetimes, and element generations reject handle reuse.

`events` is cursor-based streaming via bounded polling, not unsolicited socket
writes. The GUI adds `started`, `snapshot`, `step`, and `stopped` records to a
256-event history. Responses contain `cursor` and `gap`; slow/disconnected
clients cannot block recording. CLI `--follow` polls this history every 250 ms.
Labels apply when processed on the UI thread. Snapshot events identify saved
files; capture files also have a JSON label sidecar. Recordings are not atomic
across properties, screenshots, or different UI threads.

Closing a watcher stops it. Closing/hiding the inspection window retains the
existing UWPSpy GUI behavior; hiding the last inspector ends inspection. A
client disconnect alone does not stop a watcher. Manual recording limits still
apply (200 snapshots / 100 MiB). Old endpoint and element references must be
rediscovered after reattachment or Explorer restart.


`endpoints` prints literal pipe paths, one per line, suitable for PowerShell
assignment. Use `endpoints --json` only when a JSON array is needed.

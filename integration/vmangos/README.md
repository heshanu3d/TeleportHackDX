# TeleportHackDX teleport API

This copy of TeleportManager.lua extends the server's existing TELEPORTUI
protocol. The new xyz action accepts a map ID, coordinates and optional facing
from any player, without requiring the hearthstone. It is intentionally not
an authorization boundary: any client able to send the command can use it.
Combat remains blocked. Each player is limited to one request per 1.5 seconds
per Eluna map state; switching map state resets that local limit. The server
validates numeric values and delegates map validity and the actual teleport to
Player:Teleport. Do not install this feature if arbitrary player travel is not
acceptable on the server.

The original catalog actions, item check, and addon remain unchanged. The
server must load this script and its existing Teleport_vmangos.lua registration.
Back up the existing file before replacement and reload the scripts or restart
the server using the server's normal procedures.

The injected overlay's checkbox affects only double-clicking a favourite.
It defaults off. When enabled, a known current and destination map ID are
required; same-map destinations use the original movement path and only
cross-map destinations use this API. The current implementation is
restricted to the inspected 1.12.3 build 6141 executable with SHA-256
43371a05d66284268bb3f8b063fa50fcbbe4d4e6b59e59e59b44cd818c13a177.
It executes on the game window thread; other builds and the desktop clients
are unsupported. The game chat shows the server's result as
[Teleport API] token: status. An ok means the server accepted the request,
not that the client has completed loading the destination.

Local protocol regression test (Python with lupa's Lua 5.1 runtime):

    python tests/teleport_api_server_tests.py build/bin/teleport_api_tests.exe

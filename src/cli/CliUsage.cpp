#include "CliUsage.h"

#include <iostream>

namespace ComputerCpp::Cli {

void PrintUsage() {
    std::cout << R"(computer.cpp

Usage:
  computer.cpp [--session name] [--control-session token] [--json] <command> [args]

Daemon:
  daemon [--session name]              Run daemon in foreground
  open                                 Start/reuse daemon
  close                                Stop daemon
  ping                                 Check daemon
  capabilities                         Machine-readable feature list
  schema                               Machine-readable request/response shapes
  batch                                Run JSON array of daemon steps from stdin
  run [--owner n --purpose t] <script.lua> [--var k=v]
                                      Run a Lua RPA script, optionally under a lease
  app run <app.lua> <command> [options]
                                      Run a semantic Lua app command
  app operation get|result|cancel <app.lua> <operation-id>
                                      Inspect async semantic app operations
	  app serve <app.lua> [--listen 127.0.0.1:8787] [--auth-token-env ENV]
	          [--allowed-origin https://host]
	                                      Serve a semantic Lua app over HTTP and MCP at /mcp
	  config path                         Print user config.toml path
	  config init [--force]               Create default user config.toml
	  config show                         Show redacted model/provider config
	  config set-provider <name> ...      Configure an LLM endpoint and API key
	  config set-profile <name> ...       Configure model and sampling defaults
	  config import-env                   One-time import from legacy LLM env vars
	  config test [profile] [--live]      Validate or test configured inference

Control Session:
  session acquire [--owner n] [--purpose t] [--ttl 10m] [--wait 2m] [--max 4h]
                                      Acquire exclusive desktop lease
  session resume [--owner n] [--purpose t] [--ttl 10m] [--wait 2m] [--max 4h]
                                      Resume same owner/purpose lease or acquire one
  session renew <token> [--ttl 10m]   Extend an active lease
  session release <token>             Release an active lease
  session release-active [--scope s] [--owner n] [--purpose t] [--reason text]
                                      Debug/audit release of current active lease
  session status [--scope scope]       Show current lease holder
  session metrics [--prometheus]       Show lease metrics for Prometheus/Grafana
  session events [--limit n]           Show recent lease lifecycle events
  session run [--ttl 10m] [--wait 2m] [--max 4h] -- <command>
                                      Run command while renewing lease
  session exec [--ttl 10m] [--wait 2m] [--max 4h] [--release] -- <command>
                                      Run command in a persistent named lease

Observation:
  app active                           Show active app
  app launch <name|bundle-id>          Launch or activate app
  app activate <name|bundle-id>        Alias for app launch
  app activate-pid <pid>               Activate app by process id
  open url <http-url> [--browser app] [--new-window|--no-new-window] [--new-instance]
                                      Open a URL in a browser
  observe events [limit]               List recent observed input events
  observe frames [@evN|last] [limit]   List frames captured for an event
  target find role <role> [name]       Find semantic accessibility targets
  target resolve <target>              Resolve a click/action target
  target explain <target>              Explain target resolution
  get <@ref> [text|value|bounds|all]   Inspect latest snapshot ref
  wait --frontmost <app>               Wait for active app
  wait --stable-screen <ms>            Wait for screen stability
  permissions [--request]              Check/request macOS permissions
  permissions open-settings [pane]     Open macOS privacy settings pane
  doctor                               Run local diagnostics
  state                                Show current desktop state
  window bounds <x> <y> <w> <h> [--pid n]
                                      Resize/move frontmost or pid window
  window close [id|--frontmost]       Close a window by id or the frontmost window
  snapshot [-i] [--with-bounds] [--with-actions]
                                      Accessibility snapshot
  screenshot [path|--output path|--path path] [--frontmost-window] [--max-dim n] [--region x y w h]
                                      Capture screenshot, optionally downscaled
  image info <path>                   Show PNG/JPEG dimensions
  image split <path> [--out-dir dir|--output-dir dir] [--chunk-height n] [--overlap n] [--prefix p]
                                      Split a tall image into overlapping chunks

Input:
  click <target> [--button b] [--count n] [--duration-ms n] [--steps n] [--motion m]
        [--hover-safe] [--park-before-click|--no-park-before-click] [--park-x-fraction n] [--park-y-fraction n]
        [--park-duration-ms n] [--park-steps n] [--rect-click-x-fraction n] [--rect-click-y-fraction n]
        [--click-hold-ms n] [--pre-click-settle-ms n] [--instant]
                                      Smooth natural click by default
  mouse move <x> <y> [--duration-ms n] [--steps n] [--observe]
  mouse drag <from-x> <from-y> <to-x> <to-y> [--button b] [--duration-ms n] [--steps n] [--observe]
  mouse click <x> <y> [--button b] [--count n] [--duration-ms n] [--steps n] [--motion m|--instant]
  mouse down|up [button] [--count n]
  scroll <dy> [dx] [--at target] [--at-offset dx dy] [--no-anchor] [--center-anchor|--no-center-anchor] [--focus app] [--duration-ms n] [--steps n] [--jitter n] [--max-gesture-delta n] [--no-humanize] [--observe] [--samples n]
  scroll read [down|up] [--at target] [--no-anchor] [--focus app] [--observe]
                                      Comfortable reading-paced scroll
  press "Cmd+L" [--hold-ms n]
  type "text" [--target target] [--paste] [--hold-ms n]

Clipboard:
  clipboard read
  clipboard write "text"
  clipboard paste
)";
}

void PrintRunUsage() {
    std::cout << R"(computer.cpp run

Usage:
  computer.cpp run <script.lua> [--var key=value] [--dry-run] [--agent-stdio] [-- script-args...]
  computer.cpp run --owner n --purpose t [--ttl 10m] [--wait 2m] [--max 4h] <script.lua> [-- script-args...]
  computer.cpp --json run <script.lua> [options]

Lua scripts receive `ac` / `require("computer_cpp")` helpers. GUI actions
still go through the local daemon's JSON primitive surface. When --owner or
--purpose is supplied, computer.cpp acquires the desktop lease, renews it for
the script lifetime, closes managed resources, and releases it.
)";
}

} // namespace ComputerCpp::Cli

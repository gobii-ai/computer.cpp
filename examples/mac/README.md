# macOS Reminders API Example

`reminders.lua` turns the native macOS Reminders app into a typed
`computer.cpp` API. It is a reference for building APIs around any desktop
app using screenshots, accessibility targets, controlled input, and visual
verification.

## What It Does

The example exposes four commands:

| Command | Purpose |
| --- | --- |
| `list-reminders` | Read visible reminder rows from a named list. |
| `add-reminder` | Create one reminder with an optional notes field and verify it is visible. |
| `complete-reminder` | Mark a reminder complete by title and verify it is no longer visible as an incomplete row. |
| `summarize-list` | Read visible reminders and return a short summary. |

The `due` field is present in the schema but must be empty.

## How It Works

The file returns an `ac.app.define` object named `mac.reminders`. Each
`app:command` declares:

- `description`: human-readable command purpose.
- `input`: typed fields accepted by `computer.cpp app run` and HTTP requests.
- `output`: typed fields returned by the command.
- `handler`: Lua code that drives and verifies the desktop workflow.

The handlers use `ac.desktop.vision_task` to keep Reminders frontmost, capture
the frontmost window, ask a bounded vision agent to identify visible controls,
and expose only narrow tools such as `click_toolbar_plus`,
`type_requested_reminder_text`, `complete_reminder_checkbox`, and visual
confirmation tools. The tool handlers validate coordinates and state before
issuing real input through the local daemon.

## Requirements

- macOS with the Reminders app available.
- Accessibility and Screen Recording permissions for the built `ComputerCpp`
  app or CLI process.
- An LLM provider configured with `computer.cpp config` or the tray Settings
  window. Use `computer.cpp config path` to locate the editable `config.toml`.

From the tray app, open the tray menu and choose `Permissions`. Click `Request`
and then `Test` for Accessibility, repeat for Screen Recording, and use
`Restart ComputerCpp` if macOS asks for a restart. If Screen Recording does not
add the app automatically, use the `+` button in System Settings and select the
running `build/debug/ComputerCpp.app`.

From the CLI, check or request permissions with:

```sh
computer.cpp permissions --request
```

To configure the model from the tray app, choose `Settings...`, edit `Providers`
and `Profiles`, click `Set Active` on the profile to use, then click
`Save Changes`. The same settings are stored in the TOML file printed by
`computer.cpp config path`.

## CLI API

Build the project, then run the example with `computer.cpp app run`:

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/debug
./build/debug/computer.cpp app run examples/mac/reminders.lua
```

Print command-specific help:

```sh
./build/debug/computer.cpp app run examples/mac/reminders.lua add-reminder --help
```

List visible reminders:

```sh
./build/debug/computer.cpp --json app run examples/mac/reminders.lua \
  list-reminders --list Today --limit 10
```

Add a reminder:

```sh
./build/debug/computer.cpp --json app run examples/mac/reminders.lua \
  add-reminder --list Today --title "Review release notes" --notes "Before publishing"
```

Complete a reminder:

```sh
./build/debug/computer.cpp --json app run examples/mac/reminders.lua \
  complete-reminder --list Today --title "Review release notes"
```

Summarize a list:

```sh
./build/debug/computer.cpp --json app run examples/mac/reminders.lua \
  summarize-list --list Today --limit 25
```

## HTTP API

The same Lua app can be served locally as REST and MCP:

```sh
./build/debug/computer.cpp app serve examples/mac/reminders.lua --listen 127.0.0.1:8787
```

Inspect the generated schema:

```sh
curl http://127.0.0.1:8787/schema
```

Call a command:

```sh
curl -X POST http://127.0.0.1:8787/commands/add-reminder \
  -H 'Content-Type: application/json' \
  -d '{"list":"Today","title":"Review release notes","notes":"Before publishing"}'
```

List the generated MCP tools:

```sh
curl -X POST http://127.0.0.1:8787/mcp \
  -H 'Accept: application/json, text/event-stream' \
  -H 'Content-Type: application/json' \
  -H 'MCP-Protocol-Version: 2025-11-25' \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}'
```

When binding outside localhost, `app serve` requires `--auth-token-env` so the
HTTP and MCP APIs are not exposed without a bearer token. When serving through
Caddy or another HTTPS reverse proxy, also pass `--allowed-origin` for the
public origin that should be allowed to reach `/mcp`.

## Command Schemas

### `list-reminders`

Input:

- `list` string, required: Reminders list name.
- `limit` integer, default `25`: maximum visible rows to return.

Output:

- `list`
- `count`
- `reminders[]` with `title`, `completed`, `due`, and `notes`.

### `add-reminder`

Input:

- `list` string, required: Reminders list name.
- `title` string, required: reminder title.
- `notes` string, default `""`: optional notes text.
- `due` string, default `""`: unsupported and must be empty.

Output:

- `created`
- `list`
- `title`
- `notes`
- `due`
- `visible_title`
- `visible_notes`
- `evidence`

### `complete-reminder`

Input:

- `title` string, required: reminder title to complete.
- `list` string, default `""`: optional list to select first.

Output:

- `completed`
- `title`
- `matched_title`
- `evidence`

### `summarize-list`

Input:

- `list` string, required: Reminders list name.
- `limit` integer, default `50`: maximum visible rows to include.

Output:

- `list`
- `count`
- `summary`
- `reminders[]`

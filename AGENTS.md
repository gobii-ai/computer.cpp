# Agent Instructions
- Never perform app/browser input by mutating the DOM from JavaScript. Do not call JavaScript `click()`, dispatch input/change/keyboard/mouse events, set form values, or otherwise change UI state from injected JavaScript.
- JavaScript may be used only to inspect/read the DOM and compute target rectangles. All user-like interaction must go through the project’s native desktop mouse and keyboard APIs.
- Prefer conditional polling for visible UI/page state over fixed sleeps or waits. Short waits are acceptable only as a fallback when there is no concrete state to observe.
- Combine related desktop control actions into one control session when practical, especially when a workflow repeatedly focuses the same app, clicks, types, and verifies the next state.

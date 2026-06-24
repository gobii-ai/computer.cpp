# Agent Instructions
- Never perform app/browser input by mutating the DOM from JavaScript. Do not call JavaScript `click()`, dispatch input/change/keyboard/mouse events, set form values, or otherwise change UI state from injected JavaScript.
- JavaScript may be used only to inspect/read the DOM and compute target rectangles. All user-like interaction must go through the project’s native desktop mouse and keyboard APIs.

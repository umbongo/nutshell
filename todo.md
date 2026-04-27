# Nutshell TODO

- [x] Replace context bar token estimation with actual token counts from the API response (`usage` field in Anthropic/OpenAI responses)
- [x] Bug: AI reports command was "blocked by the read-only security policy" and asks user to enable permit-write, but permit-write is already enabled
- [x] Render markdown in AI responses (e.g. tables, headers, bold) — currently displayed as raw text instead of formatted
- [x] Use a validator to ensure that all special characters in JSON files are escaped. Use it on all JSON messages to the AI provider.
- [ ] Screen rendering bug: when an app triggers a screen clear on exit (e.g. nano, man), the screen content stays and only the prompt moves to the top of the window. Each subsequent newline then clears correctly.
- [ ] add a web browsing tool to the ai panel.

## Design / UI polish

- [x] Redesign the icons used in the app. The current set lives in [src/ui/icons.h](src/ui/icons.h) / [src/ui/icons.c](src/ui/icons.c) as a custom vector op-stream on a 16-unit grid. Pay particular attention to the **ai-assist icon** (`NS_ICON_AI`, currently an "acorn silhouette") — it should read clearly at small sizes and feel cohesive with the rest of the icon set.
- [x] Review the spacing, look, and feel of the tabs in both the terminal area and the AI panel ([src/ui/tabs.c](src/ui/tabs.c), [src/ui/tabs.h](src/ui/tabs.h)). Goals: equal spacing between tabs, polished appearance (padding, separators, active/hover states), and reliable hit-testing / rendering across DPI scales.

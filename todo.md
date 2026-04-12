# Nutshell TODO

- [x] Replace context bar token estimation with actual token counts from the API response (`usage` field in Anthropic/OpenAI responses)
- [x] Bug: AI reports command was "blocked by the read-only security policy" and asks user to enable permit-write, but permit-write is already enabled
- [x] Render markdown in AI responses (e.g. tables, headers, bold) — currently displayed as raw text instead of formatted
- [x] Use a validator to ensure that all special characters in JSON files are escaped. Use it on all JSON messages to the AI provider.

# Contributing to LYNX SHARE

Thanks for wanting to help! LYNX SHARE is an alpha, so bug reports, ideas and pull requests are all very welcome.

## Reporting bugs and asking for features
- Use the [issue templates](https://github.com/Ka1-S/LYNX-SHARE/issues/new/choose): *Bug report*, *Feature request* or *Sound / plugin difference*.
- Say which LYNX SHARE version and OS you use (Help › About). For crashes, attach the crash file LYNX SHARE offers to send, or `%APPDATA%\LYNX\autosave\crash*.txt` (Windows) / `~/.config/LYNX/autosave/` (Linux).
- Screenshots and short videos help a lot, especially side by side with FL Studio.

## Pull requests
1. Build LYNX SHARE as described in [README › Building](README.md#building) and run the tests (`scripts/build-local.sh`). They must pass with 0 failures.
2. Keep each pull request focused on one change, and add a test for logic changes.
3. Code, UI texts, comments and docs are in **English** (US spelling). A new UI text needs a line in every table in `resources/i18n/`; run `python3 tools/i18n_tool.py lint` and `python3 tools/make_i18n.py`. If you can't translate a language, copy the English text there and say so in the PR.
4. Follow the style of the file you're editing (C++20, JUCE 8).

## AI-assisted contributions
LYNX SHARE itself is developed with AI assistance, and AI-assisted pull requests are welcome on the same terms as any other:
- **You tested it.** Build it and use the change yourself on real hardware (say which OS, and for audio or plugins which device). For platforms we can't test here, such as macOS, that testing is the most valuable part of the PR.
- **You understand it.** Explain in the PR what changed and why, in your own words, and answer review questions about it. You are responsible for the code you submit, however it was written.
- **It meets the usual bar:** tests pass with 0 failures, a test for logic changes, behaviour that matches FL Studio where LYNX mirrors it (say how you checked), and the clean-room rule below.
- Mentioning that you used an AI tool is optional but appreciated, since it helps review.

## Clean-room rule
LYNX SHARE recreates the FL Studio *workflow*, not its code or artwork.
- Don't copy code, graphics, presets, samples or names from FL Studio or any third-party plugin.
- Third-party plugins are studied as black boxes (host them, feed signals, measure). Since 2026-11, by the project owner's decision, some stand-ins were also informed by clean-room static analysis of the third-party binaries (documented in [`docs/analise/REA_*.md`](docs/analise)); that is done only by the owner's decision, and nothing found that way is copied — no code, tables, presets or other content. Never patch a third-party plugin or bypass its licence protection.

## License
By contributing you agree that your contribution is licensed under the [GNU AGPL v3](LICENSE), like the rest of LYNX SHARE.

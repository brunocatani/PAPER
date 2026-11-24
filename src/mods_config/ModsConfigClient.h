#pragma once

/*
 * PAPER's client of Mods Config FO4 VR's native API. ModsConfigFO4Api.h beside it
 * is Mods Config's own client header, copied as is, as Mods Config asks of every
 * mod: refresh it from Mods Config's src/api/ when its API grows, never edit it.
 *
 * The client connects under "PAPER", the modName of PAPER's config spec
 * (data/mod/ModsConfigFO4/Config/PAPER/spec.json). Mods Config edits PAPER.toml
 * itself; the client gives PAPER live preview: each change of a `live` setting
 * becomes a preview value in PaperConfig, applied at the next frame without
 * writing the file. Its Discard or the menu closing ends it, and a Save ends
 * every preview and reads PAPER.toml at once. The modVersion state gives the
 * page its version line. Without Mods Config nothing is called.
 *
 * Threads: Mods Config calls on the game's main thread, as connect runs; see
 * PaperConfig.h for how the previews reach the frame.
 */

namespace paper::mods_config_client
{
    // From F4SE's kPostLoad: adds PAPER's entries and connects.
    void connect() noexcept;
}

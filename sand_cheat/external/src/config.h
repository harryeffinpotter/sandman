// config.h — persistent settings for sand_external.
#pragma once

namespace config {

// Load settings from external_config.ini into state::g.
// Silent no-op if file missing.
void load();

// Save state::g settings to external_config.ini.
void save();

} // namespace config

#pragma once

// Settings baked into a locally built firmware (scripts/gen_provisioning.py,
// from the gitignored provision.local.json): Wi-Fi networks, the vault's Git
// remote, settings.json keys and whole files. Applied once per provisioning
// build right after the SD card mounts, before any store loads; later changes
// made on the device are kept until a firmware with different provisioning.
namespace Provisioning {

void apply();

}  // namespace Provisioning

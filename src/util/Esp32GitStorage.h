#pragma once

// Bridges esp32-git's storage port onto HalStorage so the whole object model
// runs against the SD card. Register once during micromarkd bring-up.

void registerEsp32GitStorage();

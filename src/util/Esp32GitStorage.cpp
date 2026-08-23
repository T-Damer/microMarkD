#include "Esp32GitStorage.h"

#include <esp32_git.h>

#include <HalStorage.h>

namespace {

int64_t fs_size(const char *path) {
  HalFile f;
  if (!Storage.openFileForRead("e32g", path, f)) return -1;
  return (int64_t)f.size();
}

int fs_read(const char *path, uint8_t *buf, size_t cap, size_t *out_len) {
  HalFile f;
  if (!Storage.openFileForRead("e32g", path, f)) return -1;
  *out_len = f.read(buf, cap);
  return 0;
}

int fs_write(const char *path, const uint8_t *data, size_t len) {
  HalFile f;
  if (!Storage.openFileForWrite("e32g", path, f)) return -1;
  const size_t wrote = f.write(data, len);
  f.flush();  // explicit flush; destructor closes (DESTRUCTOR_CLOSES_FILE=1)
  return wrote == len ? 0 : -1;
}

int fs_exists(const char *path) { return Storage.exists(path) ? 1 : 0; }

int fs_make_dirs(const char *chain) {
  if (Storage.mkdir(chain, /*pFlag=*/true)) return 0;
  return Storage.exists(chain) ? 0 : -1;
}

const esp32git_fs_port kPort = {fs_size, fs_read, fs_write, fs_exists, fs_make_dirs};

}  // namespace

void registerEsp32GitStorage() {
  static bool registered = false;
  if (registered) return;
  esp32git_fs_register(&kPort);
  registered = true;
}

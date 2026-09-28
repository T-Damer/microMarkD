"""
PlatformIO pre-build script: bakes provision.local.json into the firmware.

The device applies it once after the SD card mounts (src/Provisioning.cpp), so a
freshly flashed reader starts with Wi-Fi, the vault's Git remote and settings in
place. Without the file the generated header is empty and nothing is applied.

provision.local.json (gitignored; it holds secrets, so never share the built
binary):

    {
      "wifi": [{"ssid": "Home", "password": "..."}],
      "git": {"url": "https://github.com/owner/vault", "token": "github_pat_..."},
      "settings": {"language": "RU", "fontSize": 16},
      "files": {"/.crosspoint/koreader.json": {"username": "me", "password": "...",
                                               "serverUrl": "https://sync.koreader.rocks"}}
    }

"settings" keys are merged into /.crosspoint/settings.json; "files" are written
as given (objects as JSON, strings verbatim). Passwords may be plaintext: the
stores re-save them obfuscated with the device key on first load.
"""

import hashlib
import json
import os
import sys

HEADER = "src/Provisioning.generated.h"
SOURCE = "provision.local.json"
MAX_BYTES = 16 * 1024


def fail(message):
    print(f"ERROR [gen_provisioning.py]: {message}", file=sys.stderr)
    sys.exit(1)


def placeholders(value, path=""):
    """Yields paths of values still holding the example's placeholders."""
    if isinstance(value, dict):
        for key, item in value.items():
            yield from placeholders(item, f"{path}.{key}" if path else key)
    elif isinstance(value, list):
        for index, item in enumerate(value):
            yield from placeholders(item, f"{path}[{index}]")
    elif isinstance(value, str) and ("..." in value or value in ("Имя сети", "пароль")):
        yield path


def validate(config):
    unfilled = list(placeholders(config))
    if unfilled:
        fail(f"fill in {SOURCE} first (placeholders left in: {', '.join(unfilled)})")
    if not isinstance(config, dict):
        fail(f"{SOURCE} must hold a JSON object")
    unknown = set(config) - {"wifi", "git", "settings", "files"}
    if unknown:
        fail(f"unknown keys in {SOURCE}: {', '.join(sorted(unknown))}")
    for network in config.get("wifi", []):
        if not isinstance(network, dict) or not network.get("ssid"):
            fail("every wifi entry needs an ssid")
        if len(network.get("password", "")) > 64:
            fail(f"wifi password for {network['ssid']} is longer than 64 bytes")
    git = config.get("git")
    if git is not None:
        if not str(git.get("url", "")).startswith("https://github.com/"):
            fail("git.url must start with https://github.com/")
        if len(git.get("url", "")) > 256 or len(git.get("token", "")) > 160:
            fail("git.url or git.token is too long for the device")
    if not isinstance(config.get("settings", {}), dict):
        fail("settings must be an object")
    for path in config.get("files", {}):
        if not path.startswith("/") or ".." in path.split("/"):
            fail(f"files path must be absolute on the SD card: {path}")


def render(payload):
    if not payload:
        return '#pragma once\n\nconstexpr char PROVISIONING_JSON[] = "";\nconstexpr char PROVISIONING_ID[] = "";\n'
    ident = hashlib.sha256(payload.encode()).hexdigest()[:16]
    # Raw string delimiter that cannot occur in JSON output of json.dumps.
    return ("#pragma once\n\n// Generated from provision.local.json by scripts/gen_provisioning.py. Holds secrets.\n"
            f'constexpr char PROVISIONING_JSON[] = R"PROVISION({payload})PROVISION";\n'
            f'constexpr char PROVISIONING_ID[] = "{ident}";\n')


def main(project_dir):
    source = os.path.join(project_dir, SOURCE)
    payload = ""
    if os.path.exists(source):
        with open(source, encoding="utf-8") as f:
            try:
                config = json.load(f)
            except json.JSONDecodeError as error:
                fail(f"{SOURCE}: {error}")
        validate(config)
        payload = json.dumps(config, ensure_ascii=False, separators=(",", ":"))
        if len(payload.encode()) > MAX_BYTES:
            fail(f"{SOURCE} is larger than {MAX_BYTES} bytes")
        print(f"[gen_provisioning.py] provisioning {', '.join(sorted(config))} from {SOURCE}")
    header = os.path.join(project_dir, HEADER)
    text = render(payload)
    old = open(header, encoding="utf-8").read() if os.path.exists(header) else None
    if old != text:
        with open(header, "w", encoding="utf-8") as f:
            f.write(text)


try:
    Import("env")  # noqa: F821 - provided by PlatformIO
    main(env.subst("$PROJECT_DIR"))  # noqa: F821
except NameError:
    main(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

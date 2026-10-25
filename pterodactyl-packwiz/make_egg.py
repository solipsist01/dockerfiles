"""Build egg-packwiz-minecraft.json; the install script embeds packwiz-sync.sh.

Run after changing packwiz-sync.sh:  python3 make_egg.py
"""

import json
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
SYNC = (HERE / "packwiz-sync.sh").read_text()
BOOTSTRAP_URL = ("https://github.com/packwiz/packwiz-installer-bootstrap/releases/latest/"
                 "download/packwiz-installer-bootstrap.jar")

INSTALL_SCRIPT = f"""#!/bin/bash
# Puts packwiz-sync.sh and the packwiz bootstrap on the server. Minecraft and
# the loader are installed by packwiz-sync.sh on the first start (this
# installer image has no Java), and kept in line with the pack on every start.
mkdir -p /mnt/server
cd /mnt/server
cat > packwiz-sync.sh <<'PACKWIZ_SYNC'
{SYNC}PACKWIZ_SYNC
chmod +x packwiz-sync.sh
curl -fsSL -o packwiz-installer-bootstrap.jar {BOOTSTRAP_URL}
echo "Installed. Minecraft, the loader and the mods are set up on the first start from: ${{PACKWIZ_URL}}"
"""

# The yolks entrypoint expands the startup line with `eval echo` and then runs
# the result without a shell, so the sync step has to live inside $( ... ).
STARTUP = ('$( bash packwiz-sync.sh "{{PACKWIZ_URL}}" 1>&2 ) java -Xms128M -Xmx{{SERVER_MEMORY}}M '
           '-Dterminal.jline=false -Dterminal.ansi=true '
           '$( [[ ! -f unix_args.txt ]] && printf %s "-jar server.jar" || printf %s "@unix_args.txt" )')

egg = {
    "_comment": "Minecraft server that follows a packwiz modpack: Minecraft version, loader and mods "
                "all come from the pack URL. Import via Pterodactyl admin panel -> Nests -> Import Egg.",
    "meta": {"version": "PTDL_v2", "update_url": None},
    "exported_at": datetime.now(timezone.utc).isoformat(),
    "name": "Minecraft (packwiz modpack)",
    "author": "wesselingronald@gmail.com",
    "description": "Give it a packwiz pack URL (…/pack.toml) and it installs the Minecraft version and loader "
                   "(Forge, NeoForge, Fabric or Quilt) the pack asks for, then syncs the server-side mods. "
                   "On every start it checks the pack again: a new loader version is installed automatically "
                   "and mods are updated. Pick the Docker image with the Java version your Minecraft version "
                   "needs (26.1+: Java 25, 1.20.5-1.21.x: Java 21, 1.18-1.20.4: Java 17).",
    "features": ["eula", "java_version", "pid_limit"],
    # The first one is the default for new servers.
    "docker_images": {
        "Java 21": "ghcr.io/ptero-eggs/yolks:java_21",
        "Java 25": "ghcr.io/ptero-eggs/yolks:java_25",
        "Java 24": "ghcr.io/ptero-eggs/yolks:java_24",
        "Java 23": "ghcr.io/ptero-eggs/yolks:java_23",
        "Java 22": "ghcr.io/ptero-eggs/yolks:java_22",
        "Java 17": "ghcr.io/ptero-eggs/yolks:java_17",
        "Java 16": "ghcr.io/ptero-eggs/yolks:java_16",
        "Java 11": "ghcr.io/ptero-eggs/yolks:java_11",
        "Java 8": "ghcr.io/ptero-eggs/yolks:java_8",
    },
    "file_denylist": [],
    "startup": STARTUP,
    "config": {
        "files": json.dumps({
            "server.properties": {
                "parser": "properties",
                "find": {
                    "server-ip": "0.0.0.0",
                    "server-port": "{{server.build.default.port}}",
                    "query.port": "{{server.build.default.port}}",
                },
            }
        }, indent=4),
        "startup": json.dumps({"done": ")! For help, type "}, indent=4),
        "logs": "{}",
        "stop": "stop",
    },
    "scripts": {
        "installation": {
            "script": INSTALL_SCRIPT,
            "container": "ghcr.io/ptero-eggs/installers:debian",
            "entrypoint": "bash",
        }
    },
    "variables": [
        {
            "name": "Pack URL",
            "description": "The packwiz pack.toml URL, e.g. https://packwiz.ofnie.org/packs/mtr/pack.toml. "
                           "Minecraft version, loader and mods all come from this pack.",
            "env_variable": "PACKWIZ_URL",
            "default_value": "",
            "user_viewable": True,
            "user_editable": True,
            "rules": "required|url",
            "field_type": "text",
        }
    ],
}

(HERE / "egg-packwiz-minecraft.json").write_text(json.dumps(egg, indent=4) + "\n")
print("Wrote egg-packwiz-minecraft.json")

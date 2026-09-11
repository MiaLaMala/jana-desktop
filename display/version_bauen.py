"""Version aus der Git-Historie, gleiche Regel wie Mia OS.

``MAJOR.MINOR.<Anzahl Commits>``. Major und Minor stehen in ``version.txt``
und werden von Hand gesetzt, wenn sich etwas Grundsätzliches ändert. Der
dritte Teil zählt sich selbst hoch.

Warum dieselbe Regel wie in Mia OS: Die beiden Geräte und der Server
gehören zusammen, und wer „0.1.7" auf dem Display liest, soll das ohne
Umrechnen neben „0.2.108" in der Oberfläche halten können. Zwei Schemata
für dasselbe System wären eine Quelle für Missverständnisse.

Läuft als PlatformIO-Skript vor dem Bauen und schreibt die Nummer als
Compilerschalter in die Firmware. Das Gerät weiß dadurch selbst, welche
Fassung es ist, und kann sie gegen die vom Server vergleichen.
"""

from __future__ import annotations

import subprocess
from pathlib import Path

Import("env")  # noqa: F821  # PlatformIO stellt das bereit

# PlatformIO fuehrt dieses Skript ohne __file__ aus, deshalb kommt der Pfad
# aus der Bauumgebung. Mit __file__ bricht der Build mit NameError ab.
WURZEL = Path(env["PROJECT_DIR"])  # noqa: F821


def _basis() -> str:
    """MAJOR.MINOR aus version.txt. Der Rest zählt sich selbst."""
    datei = WURZEL / "version.txt"
    if datei.exists():
        text = datei.read_text(encoding="utf-8").strip()
        if text:
            return text
    return "0.0"


def _version() -> str:
    try:
        anzahl = subprocess.run(
            ["git", "rev-list", "--count", "HEAD"],
            cwd=WURZEL,
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        # Kein Git zur Hand: lieber eine ehrlich unvollständige Nummer als
        # ein abgebrochener Build. Auf dem Display steht dann 0.1.0.
        anzahl = "0"
    return f"{_basis()}.{anzahl}"


fassung = _version()
print(f"Jana Desktop Display: Version {fassung}")
env.Append(CPPDEFINES=[("FIRMWARE_VERSION", env.StringifyMacro(fassung))])  # noqa: F821

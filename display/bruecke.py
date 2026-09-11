#!/usr/bin/env python3
"""Eine schmale Bruecke, damit das Display an Mia OS kommt.

Mia OS steht bewusst nicht im Internet. Es ist ueber das Heimnetz erreichbar
und sonst nirgends, und Mias MacBook kommt gerade nur ueber WireGuard dran.
Ein ESP32 kann kein WireGuard.

Diese Bruecke laeuft auf dem MacBook, nimmt Anfragen aus dem WLAN an und
reicht **genau einen** Pfad weiter: ``/api/briefing``, nur GET, nur lesend.
Alles andere bekommt 404, ohne dass es den Server ueberhaupt erreicht.

Das ist ausdruecklich fuer die Zeit am Schreibtisch gedacht, nicht als
Dauerloesung. Steht das Display spaeter im Heimnetz, spricht es direkt mit
Mia OS und diese Datei wird nicht mehr gebraucht.
"""

from __future__ import annotations

import json
import socket
import sys
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ZIEL = "http://172.16.30.230:8080"
PORT = 8899

# Nur dieser eine Pfad. Eine Liste statt eines Praefixvergleichs: ``/api/`` als
# Praefix haette auch die Dokumente durchgelassen, und in denen stehen
# Behoerdenpost und Arztbriefe.
ERLAUBT = {"/api/briefing"}


class Bruecke(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self) -> None:  # noqa: N802
        if self.path not in ERLAUBT:
            self.send_error(404, "Nicht freigegeben")
            return
        try:
            with urllib.request.urlopen(ZIEL + self.path, timeout=8) as antwort:
                daten = antwort.read()
        except (urllib.error.URLError, TimeoutError, OSError) as fehler:
            self.send_error(502, f"Mia OS nicht erreichbar: {fehler}")
            return

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(daten)))
        self.end_headers()
        self.wfile.write(daten)

    def do_POST(self) -> None:  # noqa: N802
        """Schreiben gibt es hier nicht, auch nicht versehentlich."""
        self.send_error(405, "Nur lesen")

    def log_message(self, format: str, *args: object) -> None:
        print(f"[bruecke] {self.address_string()} {format % args}", flush=True)


def eigene_ip() -> str:
    """Die Adresse im WLAN, nicht die im Tunnel."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.168.60.1", 80))
        return str(s.getsockname()[0])
    finally:
        s.close()


if __name__ == "__main__":
    try:
        with urllib.request.urlopen(ZIEL + "/api/briefing", timeout=8) as a:
            json.load(a)
    except Exception as fehler:  # noqa: BLE001
        print(f"Mia OS ist von hier nicht erreichbar: {fehler}", file=sys.stderr)
        print("Laeuft WireGuard?", file=sys.stderr)
        raise SystemExit(1) from fehler

    ip = eigene_ip()
    print(f"[bruecke] laeuft auf http://{ip}:{PORT}/api/briefing", flush=True)
    print("[bruecke] genau dieser Pfad, nur GET, alles andere 404", flush=True)
    ThreadingHTTPServer(("0.0.0.0", PORT), Bruecke).serve_forever()

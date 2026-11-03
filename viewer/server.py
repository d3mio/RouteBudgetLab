#!/usr/bin/env python3
"""
RouteBudget Lab - Phase 8 Offline Replay Viewer Server
Lightweight, local-only HTTP server with zero external dependencies.
Serves the HTML5 Canvas replay viewer and versioned replay JSON datasets.
"""

import sys
import os
import argparse
from pathlib import Path
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler

REPO_ROOT = Path(__file__).resolve().parent.parent
VIEWER_DIR = REPO_ROOT / "viewer"
REPLAYS_DIR = REPO_ROOT / "results" / "phase8" / "replays"

class ReplayRequestHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(REPO_ROOT), **kwargs)

    def translate_path(self, path):
        path = path.split('?', 1)[0].split('#', 1)[0]
        if path in ("/", "/index.html"):
            return str(REPO_ROOT / "viewer" / "index.html")
        elif path in ("/viewer.css", "/viewer.js"):
            return str(REPO_ROOT / "viewer" / path.lstrip("/"))
        elif path == "/api/replays/index":
            return str(REPO_ROOT / "results" / "phase8" / "replays" / "replays_index.json")
        elif path.startswith("/api/replays/"):
            filename = path[len("/api/replays/"):]
            return str(REPO_ROOT / "results" / "phase8" / "replays" / filename)
        return super().translate_path(path)

    def end_headers(self):
        # Prevent aggressive caching during interactive exploration
        self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
        self.send_header("Access-Control-Allow-Origin", "*")
        super().end_headers()

    def log_message(self, format, *args):
        # Filter noisy GET requests from terminal output
        if args and str(args[1]) in ("200", "304"):
            return
        super().log_message(format, *args)

def main():
    parser = argparse.ArgumentParser(description="RouteBudget Lab Offline Replay Viewer Server")
    parser.add_argument("--port", type=int, default=8080, help="Port to bind (default: 8080)")
    parser.add_argument("--host", type=str, default="127.0.0.1", help="Host interface (default: 127.0.0.1)")
    args = parser.parse_args()

    port = args.port
    max_tries = 10
    httpd = None

    for i in range(max_tries):
        try:
            httpd = ThreadingHTTPServer((args.host, port), ReplayRequestHandler)
            break
        except OSError as e:
            if i == max_tries - 1:
                print(f"Error: Unable to bind server on {args.host}:{port}: {e}", file=sys.stderr)
                sys.exit(1)
            port += 1

    print("=" * 80)
    print("ROUTEBUDGET LAB - OFFLINE MAPF REPLAY VIEWER (PHASE 8)")
    print("=" * 80)
    print(f"Local Server URL:   http://{args.host}:{port}")
    print(f"Viewer Directory:   {VIEWER_DIR}")
    print(f"Replays Directory:  {REPLAYS_DIR}")
    print("Mode:               Offline Replay of Verified Trajectories (Solver execution not attached)")
    print("=" * 80)
    print(f"Open http://{args.host}:{port} in your browser to inspect validated replays.")
    print("Press Ctrl+C to terminate.")

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nServer terminated cleanly.")
        httpd.server_close()

if __name__ == "__main__":
    main()

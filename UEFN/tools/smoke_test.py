#!/usr/bin/env python3
"""
Standalone smoke-test client for the UEFN MCP listener (run from a normal terminal,
NOT inside UEFN). Discovers the listener, then calls ping + get_capabilities +
get_listener_status and prints the JSON responses.

Usage:
    python UEFN/tools/smoke_test.py [--port PORT] [--token TOKEN]

Token resolution (if --token omitted): env UEFN_MCP_TOKEN, else ~/.uefn-mcp/token.
Stdlib only.
"""
import argparse
import json
import os
import sys
import urllib.error
import urllib.request

DEFAULT_PORT = 8765
PORT_SPAN = 8


def resolve_token(cli_token):
    if cli_token:
        return cli_token
    env = os.environ.get("UEFN_MCP_TOKEN")
    if env:
        return env.strip()
    path = os.environ.get("UEFN_MCP_TOKEN_FILE") or os.path.join(
        os.path.expanduser("~"), ".uefn-mcp", "token")
    try:
        with open(path, "r", encoding="utf-8") as fh:
            return fh.read().strip()
    except FileNotFoundError:
        sys.exit(f"No token given and none found at {path}. Pass --token or set UEFN_MCP_TOKEN.")


def find_listener(port):
    ports = [port] if port else range(DEFAULT_PORT, DEFAULT_PORT + PORT_SPAN + 1)
    for p in ports:
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{p}/health", timeout=2) as r:
                data = json.loads(r.read().decode())
            if data.get("backend") == "uefn":
                return p
        except (urllib.error.URLError, OSError):
            continue
    return None


def call(port, token, command, params=None):
    body = json.dumps({"command": command, "params": params or {}}).encode()
    req = urllib.request.Request(f"http://127.0.0.1:{port}/", data=body, method="POST")
    req.add_header("Content-Type", "application/json")
    req.add_header("X-MCP-Token", token)
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read().decode())
        finally:
            e.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=None)
    ap.add_argument("--token", default=None)
    args = ap.parse_args()

    port = find_listener(args.port)
    if port is None:
        sys.exit("No UEFN MCP listener found on 127.0.0.1:8765-8773. "
                 "Start it in UEFN via Execute Python Script (UEFN/execute_in_uefn.py).")
    token = resolve_token(args.token)
    print(f"Listener found on 127.0.0.1:{port}\n")

    ok = True
    for cmd in ("ping", "get_listener_status", "get_capabilities"):
        status, data = call(port, token, cmd)
        print(f"### {cmd}  (HTTP {status})")
        print(json.dumps(data, indent=2))
        print()
        ok = ok and status == 200 and data.get("success")

    print("SMOKE TEST:", "PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()

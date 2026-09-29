"""Bounded, read-only comparison with Google's official StreamList Python demo.

Run only during an explicitly authorised live diagnostic. Credentials stay in
memory, are used only with Google's API hosts, and never appear in output.
Optional one-time OAuth refresh is in memory only. No broadcast changes, chat
sends, configuration writes, or fallback polling are performed.
"""
import base64
import configparser
import ctypes
from ctypes import wintypes
import json
import pathlib
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

import grpc
import stream_list_pb2
import stream_list_pb2_grpc


def emit(**values):
    print(json.dumps(values), flush=True)


def credential(install, field):
    settings = configparser.ConfigParser(interpolation=None)
    settings.read(install / "config/pulseweaver/app-credentials.ini", encoding="utf-8-sig")
    stored = settings.get("youtube", field, fallback="").strip('"')
    if field == "client_id":
        return stored
    if not stored:
        return ""
    if not stored.startswith("dpapi:"):
        raise RuntimeError("Protected credential unavailable")

    class Blob(ctypes.Structure):
        _fields_ = [("size", wintypes.DWORD), ("data", ctypes.POINTER(ctypes.c_ubyte))]

    encrypted = base64.b64decode(stored[6:], validate=True)
    buffer = (ctypes.c_ubyte * len(encrypted)).from_buffer_copy(encrypted)
    incoming, outgoing = Blob(len(encrypted), buffer), Blob()
    crypt = ctypes.WinDLL("crypt32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    crypt.CryptUnprotectData.argtypes = [ctypes.POINTER(Blob), ctypes.c_void_p, ctypes.c_void_p,
                                       ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(Blob)]
    crypt.CryptUnprotectData.restype = wintypes.BOOL
    kernel.LocalFree.argtypes = [ctypes.c_void_p]
    kernel.LocalFree.restype = ctypes.c_void_p
    if not crypt.CryptUnprotectData(ctypes.byref(incoming), None, None, None, None, 1, ctypes.byref(outgoing)):
        raise RuntimeError("Credential unavailable for this Windows user")
    try:
        return ctypes.string_at(outgoing.data, outgoing.size).decode("utf-8")
    finally:
        ctypes.memset(outgoing.data, 0, outgoing.size)
        kernel.LocalFree(outgoing.data)


def refreshed_token(install):
    defaults = json.loads((install / "data/pulse-weaver/youtube-desktop-client.json").read_text(encoding="utf-8-sig"))["installed"]
    client_id = credential(install, "client_id")
    client_secret = credential(install, "client_secret")
    if not client_id or (client_id == defaults["client_id"] and not client_secret):
        client_id, client_secret = defaults["client_id"], defaults.get("client_secret", "")
    refresh = credential(install, "refresh_token")
    if not refresh:
        raise RuntimeError("Refresh credential unavailable")
    values = {"grant_type": "refresh_token", "client_id": client_id, "refresh_token": refresh}
    if client_secret:
        values["client_secret"] = client_secret
    request = urllib.request.Request("https://oauth2.googleapis.com/token", data=urllib.parse.urlencode(values).encode("utf-8"))
    try:
        with urllib.request.urlopen(request, timeout=15) as response:
            body = json.load(response)
    except urllib.error.HTTPError as error:
        emit(operation="oauth_refresh", http_status=error.code)
        return None
    emit(operation="oauth_refresh", http_status=200)
    return body.get("access_token")


def discover(token):
    url = ("https://www.googleapis.com/youtube/v3/liveBroadcasts"
           "?part=snippet,status&broadcastStatus=active&broadcastType=all&maxResults=50")
    request = urllib.request.Request(url, headers={"Authorization": "Bearer " + token})
    try:
        with urllib.request.urlopen(request, timeout=15) as response:
            body = json.load(response)
    except urllib.error.HTTPError as error:
        emit(operation="broadcast_discovery", http_status=error.code)
        return None
    chats = []
    for item in body.get("items", []):
        chat = item.get("snippet", {}).get("liveChatId")
        if chat and chat not in chats:
            chats.append(chat)
    emit(operation="broadcast_discovery", http_status=200, active_chats=len(chats))
    return chats[0] if chats else None


def probe(stub, token, chat, name, parts, max_results, overall_deadline):
    cursor = None
    for attempt in range(1, 4):
        remaining = overall_deadline - time.monotonic()
        if remaining <= 1:
            break
        arguments = {"part": parts, "live_chat_id": chat}
        if cursor:
            arguments["page_token"] = cursor
        if max_results is not None:
            arguments["max_results"] = max_results
        request = stream_list_pb2.LiveChatMessageListRequest(**arguments)
        start = time.monotonic()
        call = stub.StreamList(request, metadata=(("authorization", "Bearer " + token),))
        # No grpc-timeout header: a local guard cancels only if the test bound is reached.
        local_limit = threading.Event()
        def cancel():
            local_limit.set()
            call.cancel()
        timer = threading.Timer(min(45, remaining), cancel)
        timer.daemon = True
        timer.start()
        batches, messages = 0, 0
        first_batch_ms = None
        offline = False
        initial_cursor = cursor
        try:
            for response in call:
                batches += 1
                messages += len(response.items)
                if first_batch_ms is None:
                    first_batch_ms = round((time.monotonic() - start) * 1000)
                cursor = response.next_page_token or cursor
                if response.offline_at:
                    offline = True
                    call.cancel()
                    break
        except grpc.RpcError:
            pass  # Only numeric status is reported below; no raw details/metadata.
        finally:
            timer.cancel()
        code = call.code()
        emit(client=name, attempt=attempt, duration_ms=round((time.monotonic() - start) * 1000),
             first_batch_ms=first_batch_ms, batches=batches, messages=messages,
             grpc_status=code.value[0], local_time_limit=local_limit.is_set(), offline=offline,
             resume_sent=bool(initial_cursor), cursor_advanced=bool(cursor and cursor != initial_cursor))
        if code != grpc.StatusCode.OK or offline or local_limit.is_set():
            break
        time.sleep(1)


def main():
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] != "--refresh"):
        return 2
    install = pathlib.Path(sys.argv[1]).resolve()
    token = refreshed_token(install) if len(sys.argv) == 3 else credential(install, "access_token")
    if not token:
        return 1
    chat = discover(token)
    if not chat:
        emit(result="no_active_chat_or_discovery_rejected")
        return 1
    # Same host, persistent channel, generated stub, and response iteration as the
    # official demo. A second arm changes only request fields to match Pulse Weaver.
    deadline = time.monotonic() + 100
    with grpc.secure_channel("dns:///youtube.googleapis.com:443", grpc.ssl_channel_credentials()) as channel:
        stub = stream_list_pb2_grpc.V3DataLiveChatMessageServiceStub(channel)
        probe(stub, token, chat, "google_demo_fields", ["snippet"], 20, deadline)
        probe(stub, token, chat, "pulse_fields_python", ["id", "snippet", "authorDetails"], None, deadline)
    emit(result="diagnostic_finished", maximum_discovery_requests=1, maximum_stream_requests=6)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        emit(result="diagnostic_error", error_type=type(error).__name__)
        sys.exit(1)

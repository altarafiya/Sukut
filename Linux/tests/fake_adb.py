#!/usr/bin/env python3
"""A stand-in for adb with a pretend phone (stage 7 test).

The phone's state lives in $FAKE_ADB_STATE (JSON); every call is appended to
$FAKE_ADB_STATE.log. It answers only what Sukut's phone tools ask, in the same
text formats as adb and Android: devices -l, getprop, dumpsys package, am,
content call (the worker's status and deactivate), push, install and sh.

  fake_adb.py --allow-usb   stands in for sukut-usb-access run by pkexec: every
                            "no permissions" phone becomes usable.
"""
import json
import os
import pathlib
import sys

STATE = pathlib.Path(os.environ["FAKE_ADB_STATE"])
PACKAGE = "com.altarafiyah.sukut"


def load():
    return json.loads(STATE.read_text())


def save(state):
    STATE.write_text(json.dumps(state))


def out(text, code=0):
    sys.stdout.write(text)
    sys.exit(code)


def main(args):
    with open(str(STATE) + ".log", "a") as log:
        log.write(" ".join(args) + "\n")
    state = load()
    if args == ["--allow-usb"]:
        for d in state["devices"]:
            if d["state"] == "no permissions":
                d["state"] = "device"
        group = "already" if state.get("in_plugdev") else "added"
        state["in_plugdev"] = True
        save(state)
        out(f"SUKUT_USB_ACCESS_READY group={group}\n")
    if args[:2] == ["devices", "-l"]:
        lines = ["List of devices attached"]
        for d in state["devices"]:
            if d["state"] == "no permissions":  # Linux adb names no model when it can't open the phone
                lines.append(f"{d['serial']}\tno permissions (missing udev rules? user is in the plugdev group); "
                             "see [http://developer.android.com/tools/device.html] usb:1-2 transport_id:1")
                continue
            lines.append(f"{d['serial']}\t{d['state']} product:fake model:{d['model'].replace(' ', '_')} device:fake transport_id:1")
        out("\n".join(lines) + "\n\n")
    serial = None
    if args[:1] == ["-s"]:
        serial, args = args[1], args[2:]
    device = next((d for d in state["devices"] if d["serial"] == serial), None)
    if device is None or device["state"] != "device":
        if device is None:
            out(f"adb: device '{serial}' not found\n", 1)
        if device["state"] == "no permissions":
            out(f"adb: insufficient permissions for device: missing udev rules? user is in the plugdev group\n", 1)
        out("error: device unauthorized.\n", 1)
    installed = state.get("installed")
    if args == ["shell", "getprop"]:
        out(f"[ro.product.marketname]: [{device['model']}]\n[ro.product.model]: [{device['model']}]\n"
            f"[ro.build.version.release]: [{state.get('android', '15')}]\n")
    if args == ["shell", "dumpsys", "package", PACKAGE]:
        if not installed:
            out("")
        out(f"Packages:\n  Package [{PACKAGE}] (fake):\n    versionCode={installed['code']} minSdk=29 targetSdk=35\n"
            f"    versionName={installed['version']}\n")
    if args == ["shell", "am", "get-current-user"]:
        out(f"{state.get('user', 0)}\n")
    if args[:3] == ["shell", "content", "call"]:
        method = args[args.index("--method") + 1]
        if not installed:
            out("Error while accessing provider:com.altarafiyah.sukut.worker\n", 1)
        if method == "deactivate":
            state["worker"] = "INACTIVE"
            save(state)
            out("Result: Bundle[{state=INACTIVE, protocol=1}]\n")
        if state.get("worker") == "READY":
            out(f"Result: Bundle[{{state=READY, protocol=1, version={installed['code']}, pid=12345}}]\n")
        out(f"Result: Bundle[{{state=INACTIVE, protocol=1, version={installed['code']}}}]\n")
    if args[:1] == ["install"]:
        replace = "-r" in args
        apk = pathlib.Path(args[-1])
        if not apk.exists():
            out(f"adb: failed to stat {apk}: No such file or directory\n", 1)
        result = state.get("install_result", "Success")
        if result != "Success":
            out(f"Performing Streamed Install\nadb: failed to install {apk}: {result}\n", 1)
        if installed and not replace:
            out(f"Performing Streamed Install\nadb: failed to install {apk}: Failure [INSTALL_FAILED_ALREADY_EXISTS]\n", 1)
        version, code = pathlib.Path(str(apk) + ".version").read_text().split()
        state["installed"] = {"code": int(code), "version": version}
        state["worker"] = "INACTIVE"  # an update ends the running worker
        save(state)
        out("Performing Streamed Install\nSuccess\n")
    if args[:3] == ["shell", "am", "start"]:
        if not installed:
            out("Error: Activity class {com.altarafiyah.sukut/com.osama.cleanwave.MainActivity} does not exist.\n")
        out("Starting: Intent { cmp=com.altarafiyah.sukut/com.osama.cleanwave.MainActivity }\nStatus: ok\nLaunchState: COLD\n")
    if args[:1] == ["push"]:
        state["pushed"] = args[2]
        save(state)
        out(f"{args[1]}: 1 file pushed, 0 skipped.\n")
    if args[:3] == ["shell", "sh", "/data/local/tmp/sukut-activate-worker.sh"]:
        if state.get("pushed") != "/data/local/tmp/sukut-activate-worker.sh":
            out("sh: /data/local/tmp/sukut-activate-worker.sh: No such file\n", 1)
        if not state.get("worker_fails"):
            state["worker"] = "READY"
            save(state)
        out("SUKUT_WORKER_LAUNCHED\n")
    if args[:2] == ["shell", "tail"]:
        out("fake worker log: binder not connected\n")
    out("unknown: " + " ".join(args) + "\n", 1)


main(sys.argv[1:])

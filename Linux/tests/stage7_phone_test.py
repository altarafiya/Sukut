"""Stage 7 test: the phone tools against a stand-in adb (Linux/PLAN.md section 7, 12).

  python3 -I stage7_phone_test.py <build-bin-dir> <data-dir> <windows-phone-assets> <evidence-dir>

Runs `sukut --phone …` (the same PhoneTools the Phone page uses) with
SUKUT_ADB pointing at fake_adb.py, a pretend phone. No real phone or USB is
touched. The live check on the owner's phone is done on real Debian (stage 9).
"""
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys

BIN, DATA, WINDOWS, EVIDENCE = (pathlib.Path(a) for a in sys.argv[1:5])
EVIDENCE.mkdir(parents=True, exist_ok=True)
HERE = pathlib.Path(__file__).resolve().parent
WORK = pathlib.Path(os.environ.get("HOME", "/tmp")) / "sukut-stage7"
shutil.rmtree(WORK, ignore_errors=True)
WORK.mkdir()
STATE = WORK / "phone.json"
FAKE = WORK / "adb"
FAKE.write_text(f"#!/bin/sh\nexec {sys.executable} -I {HERE / 'fake_adb.py'} \"$@\"\n")
FAKE.chmod(0o755)
# pkexec and sukut-usb-access stand-ins: no password prompt, no root, no udev.
PKEXEC = WORK / "pkexec"
PKEXEC.write_text('#!/bin/sh\necho "pkexec $*" >> "$FAKE_ADB_STATE.log"\n[ -z "${FAKE_PKEXEC_DENY:-}" ] || exit 126\nexec "$@"\n')
PKEXEC.chmod(0o755)
HELPER = WORK / "sukut-usb-access"
HELPER.write_text(f"#!/bin/sh\nexec {sys.executable} -I {HERE / 'fake_adb.py'} --allow-usb\n")
HELPER.chmod(0o755)
PIXEL = {"serial": "R5CT10A2B3C", "state": "device", "model": "Pixel 8"}
results, log_lines = [], []


def log(text):
    print(text, flush=True)
    log_lines.append(text)


def check(name, ok, detail=""):
    results.append({"check": name, "pass": bool(ok), "detail": str(detail)})
    log(f"{name}={'PASS' if ok else 'FAIL'}{' ' + str(detail) if detail != '' else ''}")


def phone(state, **extra):
    STATE.write_text(json.dumps(dict({"devices": [PIXEL], "installed": None, "worker": "INACTIVE"}, **state, **extra)))
    pathlib.Path(str(STATE) + ".log").write_text("")


def sukut(*args, data=DATA, adb=FAKE, helper=HELPER, deny=False):
    env = dict(os.environ, SUKUT_ADB=str(adb), SUKUT_DATA_DIR=str(data), FAKE_ADB_STATE=str(STATE),
               SUKUT_PKEXEC=str(PKEXEC), SUKUT_USB_HELPER=str(helper))
    env.pop("FAKE_PKEXEC_DENY", None)
    if deny:
        env["FAKE_PKEXEC_DENY"] = "1"
    done = subprocess.run([str(BIN / "sukut"), "--phone", *args], capture_output=True, text=True, timeout=120, env=env)
    return done.returncode, (done.stdout + done.stderr).strip()


def calls():
    return pathlib.Path(str(STATE) + ".log").read_text().splitlines()


def state():
    return json.loads(STATE.read_text())


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tampered(name):
    copy = WORK / f"data-{name}"
    shutil.copytree(DATA, copy, dirs_exist_ok=True)
    (copy / "android" / f"{name}.sha256").write_text("0" * 64 + "\n")
    return copy


def main():
    # The APK is the Windows build's and the activation script Android 0.4.2's
    # (Linux/data/android), byte for byte, and both pass their own checks.
    android = DATA / "android"
    sources = [f for f in WINDOWS.iterdir() if f.name.startswith("Sukut.apk")] + list((HERE.parent / "data" / "android").iterdir())
    same = len(sources) == 5 and all(sha(android / f.name) == sha(f) for f in sources)
    apk_ok = sha(android / "Sukut.apk") == (android / "Sukut.apk.sha256").read_text().split()[0].lower()
    script_ok = sha(android / "activate-worker.sh") == (android / "activate-worker.sh.sha256").read_text().split()[0].lower()
    version = (android / "Sukut.apk.version").read_text().split()
    check("P_bundle_matches_windows", same and apk_ok and script_ok, f"version={version}")
    bundled = int(version[1])

    phone({})
    code, text = sukut("devices", adb=WORK / "missing-adb")
    check("P_no_adb", code == 1 and "Could not start ADB" in text, text)
    phone({}, devices=[])
    code, text = sukut("inspect")
    check("P_no_phone", code == 1 and "Connect your phone by USB" in text, text)
    phone({}, devices=[dict(PIXEL, state="unauthorized")])
    code, text = sukut("activate")
    check("P_unauthorized", code == 1 and "Allow USB debugging on the phone" in text, text)

    # Linux only: a phone adb can't open ("no permissions"), fixed with one polkit prompt.
    phone({}, devices=[dict(PIXEL, state="no permissions")])
    code, text = sukut("devices")
    check("P_no_permissions_listed", code == 0 and text == "R5CT10A2B3C no permissions Android", text)
    code, text = sukut("activate")
    check("P_no_permissions_explained", code == 1 and "Sukut can't access the phone yet" in text and "sudo" not in text, text)
    code, text = sukut("allow-usb", deny=True)
    check("P_allow_usb_refused", code == 1 and "was not allowed" in text and state()["devices"][0]["state"] == "no permissions", text)
    code, text = sukut("allow-usb", helper=WORK / "missing-helper")
    check("P_allow_usb_needs_package", code == 1 and "can't be fixed from this build" in text, text)
    pathlib.Path(str(STATE) + ".log").write_text("")
    code, text = sukut("allow-usb")
    asked = [c for c in calls() if c.startswith("pkexec ")]
    check("P_allow_usb", code == 0 and "USB access allowed" in text and "sign out" in text and asked == [f"pkexec {HELPER}"]
          and state()["devices"][0]["state"] == "device", f"{text} | {asked}")
    phone({}, devices=[dict(PIXEL, state="no permissions")], in_plugdev=True)
    code, text = sukut("allow-usb")
    check("P_allow_usb_in_group", code == 0 and "USB access allowed" in text and "sign out" not in text, text)
    phone({}, devices=[{"serial": "????????????", "state": "no permissions", "model": "Android"}])
    code, text = sukut("devices")
    check("P_no_permissions_old_adb", code == 0 and text == "???????????? no permissions Android", text)
    # The real helper refuses to run unless pkexec started it as root.
    real = HERE.parent / "data" / "sukut-usb-access"
    syntax = subprocess.run(["sh", "-n", str(real)], capture_output=True, text=True)
    direct = subprocess.run(["sh", str(real)], capture_output=True, text=True, env=dict(os.environ, PKEXEC_UID="1000"))
    check("P_helper_needs_pkexec", syntax.returncode == 0 and direct.returncode == 1 and "pkexec" in direct.stderr, direct.stderr.strip())

    phone({})
    code, text = sukut("devices")
    check("P_devices_listed", code == 0 and text == "R5CT10A2B3C device Pixel 8", text)
    code, text = sukut("inspect")
    check("P_inspect_not_installed", code == 0 and "Pixel 8 · Android 15 · Sukut not installed" in text, text)
    code, text = sukut("install")
    install = [c for c in calls() if " install " in f" {c} "]
    check("P_install", code == 0 and "Sukut installed" in text and state()["installed"]["code"] == bundled
          and install and " -r " not in install[0], f"{text} | {install}")
    code, text = sukut("install")
    check("P_install_again_keeps", code == 0 and "already installed" in text, text)

    phone({}, installed={"code": bundled - 1, "version": "0.4"})
    code, text = sukut("update")
    install = [c for c in calls() if " install " in f" {c} "]
    check("P_update_older", code == 0 and "Sukut updated" in text and install and " -r " in install[0]
          and state()["installed"]["code"] == bundled, f"{text} | {install}")
    code, text = sukut("update")
    check("P_update_current", code == 0 and "already up to date" in text, text)
    phone({})
    code, text = sukut("update")
    check("P_update_needs_install", code == 1 and "Sukut is not installed" in text, text)

    phone({})
    code, text = sukut("setup")
    opened = any(c.endswith("shell am start -W -n com.altarafiyah.sukut/com.osama.cleanwave.MainActivity") for c in calls())
    check("P_setup_installs_and_opens", code == 0 and "ready for activation" in text and opened, text)

    code, text = sukut("activate")
    pushed = [c for c in calls() if c.startswith("-s R5CT10A2B3C push ")]
    ran = any(c.endswith("shell sh /data/local/tmp/sukut-activate-worker.sh 0") for c in calls())
    check("P_activate", code == 0 and "audio worker is ready" in text and pushed and ran
          and pushed[0].endswith("/android/activate-worker.sh /data/local/tmp/sukut-activate-worker.sh"), text)
    code, text = sukut("activate")
    check("P_activate_again", code == 0 and "already ready" in text, text)
    code, text = sukut("inspect")
    check("P_inspect_ready", code == 0 and f"Sukut {bundled} · worker ready" in text, text)
    code, text = sukut("deactivate")
    check("P_deactivate", code == 0 and "deactivated" in text and state()["worker"] == "INACTIVE", text)
    code, text = sukut("open")
    check("P_open", code == 0 and "Sukut opened" in text, text)

    phone({}, installed={"code": bundled - 1, "version": "0.4"})
    code, text = sukut("activate")
    check("P_activate_updates_first", code == 0 and "audio worker is ready" in text and state()["installed"]["code"] == bundled, text)

    phone({})
    code, text = sukut("install", data=tampered("Sukut.apk"))
    check("P_apk_checked", code == 1 and "Sukut APK verification failed" in text and not state()["installed"], text)
    phone({}, installed={"code": bundled, "version": "0.4"})
    code, text = sukut("activate", data=tampered("activate-worker.sh"))
    check("P_script_checked", code == 1 and "activation script verification failed" in text and "pushed" not in state(), text)
    phone({}, install_result="Failure [INSTALL_FAILED_USER_RESTRICTED: Install canceled by user]")
    code, text = sukut("install")
    check("P_install_refused_reported", code == 1 and "INSTALL_FAILED_USER_RESTRICTED" in text, text)
    phone({}, installed={"code": bundled, "version": "0.4"}, worker_fails=True)
    code, text = sukut("activate")
    launches = sum(c.endswith("sukut-activate-worker.sh 0") for c in calls())
    check("P_worker_fails_reported", code == 1 and "did not connect" in text and "fake worker log" in text and launches == 2,
          f"launches={launches} | {text}")

    second = {"serial": "9A281FFAZ0", "state": "device", "model": "Pixel 8a"}
    phone({}, devices=[PIXEL, second])
    code, text = sukut("inspect")
    check("P_two_phones_need_choice", code == 1 and "Select your phone first" in text, text)
    code, text = sukut("inspect", "--serial", "9A281FFAZ0")
    check("P_two_phones_chosen", code == 0 and text.startswith("OK: Pixel 8a"), text)


try:
    main()
finally:
    passed = sum(r["pass"] for r in results)
    log(f"RESULT={'PASS' if results and passed == len(results) else 'FAIL'} {passed}/{len(results)}")
    (EVIDENCE / "stage7-results.json").write_text(json.dumps(results, indent=2, ensure_ascii=False) + "\n")
    (EVIDENCE / "stage7-log.txt").write_text("\n".join(log_lines) + "\n")
    sys.exit(0 if results and passed == len(results) else 1)

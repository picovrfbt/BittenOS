"""Publish the built app as a GitHub release the app's updater will find.

Run after building and pushing: python release.py
Tags app-<code> from version.json and attaches the debug APK as BittenOS.apk.
"""
import json, os, shutil, subprocess, sys

here = os.path.dirname(os.path.abspath(__file__))
v = json.load(open(os.path.join(here, "version.json"), encoding="utf-8"))
apk = os.path.join(here, "app", "build", "outputs", "apk", "debug", "app-debug.apk")
out = os.path.join(here, "BittenOS.apk")
shutil.copyfile(apk, out)
tag = f"app-{v['code']}"
branch = subprocess.check_output(["git", "rev-parse", "--abbrev-ref", "HEAD"], cwd=here, text=True).strip()
sys.exit(subprocess.call(["gh", "release", "create", tag, out, "--repo", "picovrfbt/BittenOS", "--target", branch,
                          "--title", f"BittenOS app v{v['name']}", "--notes", v.get("notes", "")]))

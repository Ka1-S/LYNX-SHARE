#!/usr/bin/env python3
"""Seeded UI monkey test of the real LYNX window (round 190rb, the 0.1.32 quality release).

Starts `LYNX --demo [project]` on an X display (a private Xvfb), then drives it for a while with a seeded random mix of
real X input (xdotool: clicks, right-clicks, drags, wheel, keys) and structured Remote Control / --demo operations
(docs/DEMO_MODE.md: click a random showing control, run a random key-map action, switch layouts, open panels and
editors, undo / redo, save and reload). Every action is written to <out>/actions.jsonl *before* it runs, fully resolved
(coordinates, control IDs), so `--replay <actions.jsonl>` repeats a run without the random generator.

It stops at the first crash (the process died: its exit code and the sanitizer reports in <out>/san.*), a hang (the
API stopped answering for --hang-seconds: SIGABRT, so ASan prints the main thread's stack) or the time limit.
<out>/result.json says which. Run it inside a sandbox where the files that matter are read-only
(scripts/ui-monkey.sh does): random clicks reach Save as..., the file browser and every other menu item.

  ui_monkey.py --app LYNX --display :190 --seed 7 --minutes 5 --layout pattern --project song.flp --out runs/s7
  ui_monkey.py --app LYNX --display :190 --replay runs/s7/actions.jsonl --out runs/s7-replay
"""
import argparse
import json
import os
from pathlib import Path
import random
import signal
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "examples"))
from remote_control import Client  # noqa: E402  (the repo's standard-library client)

LAYOUTS = ["pattern", "clipLaunch", "simple"]
PANELS = ["channelRack", "pianoRoll", "playlist", "mixer", "browser", "loops", "dock", "pluginManager", "versions"]
# keys a user presses (FL's and LYNX's shortcuts among them); no Alt+F4 (it would only end the run early)
KEYS = ["Escape", "Escape", "Return", "space", "Tab", "Delete", "BackSpace", "Left", "Right", "Up", "Down", "Home", "End",
        "Prior", "Next", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
        "ctrl+z", "ctrl+y", "ctrl+shift+z", "alt+z", "ctrl+c", "ctrl+v", "ctrl+x", "ctrl+a", "ctrl+d", "ctrl+b",
        "ctrl+l", "ctrl+n", "ctrl+o", "ctrl+s", "ctrl+shift+s", "ctrl+t", "ctrl+f", "ctrl+e", "ctrl+r", "ctrl+i",
        "shift+Delete", "shift+Left", "shift+Right", "ctrl+Left", "ctrl+Right", "ctrl+Up", "ctrl+Down", "ctrl+Insert",
        "ctrl+alt+Insert", "shift+Insert", "Insert", "plus", "minus", "ctrl+plus", "ctrl+minus", "Menu",
        "a", "b", "c", "d", "e", "g", "l", "m", "n", "p", "q", "r", "s", "t", "u", "w", "x", "y", "z",
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "alt+1", "alt+2", "alt+3", "ctrl+1", "ctrl+2", "shift+1"]
WEIGHTS = {
    "api_click": 22, "x_click": 16, "x_drag": 8, "x_wheel": 7, "x_key": 13, "action": 10, "escape": 6, "show": 5,
    "undo_redo": 5, "editor": 3, "api_drag": 3, "layout": 2, "save_reload": 1, "open": 1, "repaint": 1,
}


class Monkey:
    def __init__(self, a):
        self.a = a
        self.out = Path(a.out).resolve()
        self.out.mkdir(parents=True, exist_ok=True)
        self.profile = self.out / "profile"
        self.profile.mkdir(exist_ok=True)
        (self.profile / "control").mkdir(mode=0o700, exist_ok=True)  # (the discovery folder must be private)
        os.chmod(self.profile / "control", 0o700)
        self.log = open(self.out / "actions.jsonl", "a", buffering=1)
        self.proc = None
        self.client = None
        self.controls = []
        self.controls_at = -99
        self.window = None
        self.actions = self.load_actions()
        self.saves = 0
        self.count = 0

    # --- process ---------------------------------------------------------------------------------------------------
    def env(self):
        e = dict(os.environ)
        san = str(self.out / "san")
        e.update({
            "DISPLAY": self.a.display, "LYNX_PROFILE_DIR": str(self.profile), "XDG_CONFIG_HOME": str(self.profile / "config"),
            "XDG_DATA_HOME": str(self.profile / "data"), "XDG_CACHE_HOME": str(self.profile / "cache"),
            "LYNX_CONTROL_ENABLED": "1", "LYNX_CONTROL_DISCOVERY": str(self.profile / "control/connection.json"),
            "ASAN_OPTIONS": e.get("ASAN_OPTIONS", "") + f":log_path={san}.asan:handle_abort=1:detect_leaks=0",
            "UBSAN_OPTIONS": e.get("UBSAN_OPTIONS", "print_stacktrace=1:halt_on_error=0") + f":log_path={san}.ubsan",
        })
        e.pop("WAYLAND_DISPLAY", None)
        return e

    def start(self, project):
        args = [self.a.app, "--demo"] + ([project] if project else [])
        self.stdout = open(self.out / "lynx.out", "ab")
        self.proc = subprocess.Popen(args, env=self.env(), stdin=subprocess.DEVNULL, stdout=self.stdout, stderr=subprocess.STDOUT,
                                     cwd=str(self.out))
        disc = self.profile / "control/connection.json"
        deadline = time.time() + self.a.start_seconds
        while time.time() < deadline:
            if self.proc.poll() is not None:
                return False
            if disc.exists():
                try:
                    self.client = Client(disc)
                    self.call("ui.window", timeout_ok=True)
                    return True
                except Exception:
                    pass
            time.sleep(0.5)
        return False

    def alive(self):
        return self.proc is not None and self.proc.poll() is None

    def call(self, op, args=None, timeout_ok=False):
        try:
            return self.client.call(op, args or {})
        except RuntimeError as e:  # an error answer: a refused operation is fine for a monkey
            return {"error": str(e)[:200]}
        except OSError as e:
            if timeout_ok:
                raise
            return {"oserror": str(e)[:200]}

    # --- helpers ---------------------------------------------------------------------------------------------------
    def load_actions(self):
        f = Path(__file__).resolve().parent.parent / "src/core/KeyMap.cpp"
        import re
        ids = sorted(set(re.findall(r'\{\s*"([a-z]+\.[A-Za-z0-9.]+)"', f.read_text(errors="replace")))) if f.exists() else []
        return ids or ["edit.undo", "edit.redo"]

    def xdo(self, *args):
        subprocess.run([self.a.xdotool] + [str(x) for x in args], env={**os.environ, "DISPLAY": self.a.display},
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=20)

    def win(self, rng):
        if self.window is None or rng.random() < 0.05:
            r = self.call("ui.window")
            s = r.get("screen") if isinstance(r, dict) else None
            if isinstance(s, dict):
                self.window = (s.get("x", 0), s.get("y", 0), max(200, s.get("width", 1600)), max(200, s.get("height", 900)))
            elif isinstance(s, list) and len(s) == 4:
                self.window = tuple(s)
        return self.window or (0, 0, 1600, 900)

    def point(self, rng):
        x, y, w, h = self.win(rng)
        return int(x + rng.random() * w), int(y + rng.random() * h)

    def control(self, rng):
        if self.count - self.controls_at > 8 or not self.controls:
            r = self.call("ui.list", {"limit": 2000})
            self.controls = [c for c in (r.get("controls") or []) if isinstance(c, dict) and c.get("id")] if isinstance(r, dict) else []
            self.controls_at = self.count
        return rng.choice(self.controls) if self.controls else None

    # --- one random action, resolved to a replayable record --------------------------------------------------------
    def plan(self, rng):
        kind = rng.choices(list(WEIGHTS), weights=list(WEIGHTS.values()))[0]
        if kind == "api_click":
            c = self.control(rng)
            if c is None:
                return {"k": "escape"}
            return {"k": kind, "target": c["id"], "right": rng.random() < 0.2, "double": rng.random() < 0.1,
                    "at": {"x": round(rng.random(), 3), "y": round(rng.random(), 3)}}
        if kind == "api_drag":
            c1, c2 = self.control(rng), self.control(rng)
            if c1 is None or c2 is None:
                return {"k": "escape"}
            return {"k": kind, "from": c1["id"], "to": c2["id"], "dx": rng.randint(-80, 80), "dy": rng.randint(-40, 40),
                    "mods": rng.choice([[], [], ["ctrl"], ["shift"], ["alt"]])}
        if kind == "x_click":
            x, y = self.point(rng)
            return {"k": kind, "x": x, "y": y, "button": rng.choice([1, 1, 1, 3]), "repeat": rng.choice([1, 1, 1, 2]),
                    "mod": rng.choice(["", "", "", "ctrl", "shift", "alt"])}
        if kind == "x_drag":
            x, y = self.point(rng)
            return {"k": kind, "x": x, "y": y, "x2": x + rng.randint(-400, 400), "y2": y + rng.randint(-250, 250),
                    "button": rng.choice([1, 1, 3]), "mod": rng.choice(["", "", "ctrl", "shift", "alt"]), "steps": rng.randint(2, 12)}
        if kind == "x_wheel":
            x, y = self.point(rng)
            return {"k": kind, "x": x, "y": y, "button": rng.choice([4, 5, 6, 7]), "n": rng.randint(1, 8),
                    "mod": rng.choice(["", "", "ctrl", "shift", "alt"])}
        if kind == "x_key":
            return {"k": kind, "key": rng.choice(KEYS)}
        if kind == "action":
            return {"k": kind, "id": rng.choice(self.actions)}
        if kind == "show":
            return {"k": kind, "panel": rng.choice(PANELS)}
        if kind == "undo_redo":
            return {"k": kind, "seq": [rng.choice(["edit.undo", "edit.undo", "edit.redo"]) for _ in range(rng.randint(1, 6))]}
        if kind == "editor":
            return {"k": kind, "channel": rng.randint(0, 20)}
        if kind == "layout":
            return {"k": kind, "style": rng.choice(LAYOUTS)}
        if kind == "save_reload":
            self.saves += 1
            return {"k": kind, "path": str(self.out / f"save{self.saves}.lynx")}
        if kind == "open":
            return {"k": kind, "file": rng.choice(self.a.projects)} if self.a.projects else {"k": "escape"}
        return {"k": kind}

    def run_action(self, act):
        k = act["k"]
        if k == "api_click":
            self.call("ui.click", {"target": act["target"], "right": act["right"], "double": act["double"], "at": act["at"]})
        elif k == "api_drag":
            self.call("ui.drag", {"from": {"target": act["from"]}, "to": {"target": act["to"], "dx": act["dx"], "dy": act["dy"]},
                                  "mods": act["mods"], "seconds": 0.3})
            time.sleep(0.35)
        elif k == "x_click":
            self.xdo("mousemove", act["x"], act["y"])
            if act["mod"]:
                self.xdo("keydown", act["mod"])
            self.xdo("click", "--repeat", act["repeat"], "--delay", 60, act["button"])
            if act["mod"]:
                self.xdo("keyup", act["mod"])
        elif k == "x_drag":
            self.xdo("mousemove", act["x"], act["y"])
            if act["mod"]:
                self.xdo("keydown", act["mod"])
            self.xdo("mousedown", act["button"])
            for i in range(1, act["steps"] + 1):
                t = i / act["steps"]
                self.xdo("mousemove", int(act["x"] + (act["x2"] - act["x"]) * t), int(act["y"] + (act["y2"] - act["y"]) * t))
            self.xdo("mouseup", act["button"])
            if act["mod"]:
                self.xdo("keyup", act["mod"])
        elif k == "x_wheel":
            self.xdo("mousemove", act["x"], act["y"])
            if act["mod"]:
                self.xdo("keydown", act["mod"])
            self.xdo("click", "--repeat", act["n"], "--delay", 30, act["button"])
            if act["mod"]:
                self.xdo("keyup", act["mod"])
        elif k == "x_key":
            self.xdo("key", "--clearmodifiers", act["key"])
        elif k == "escape":
            self.xdo("key", "Escape")
        elif k == "action":
            self.call("ui.action", {"id": act["id"]})
        elif k == "show":
            self.call("ui.show", {"panel": act["panel"]})
        elif k == "undo_redo":
            for a in act["seq"]:
                self.call("ui.action", {"id": a})
        elif k == "editor":
            self.call("ui.editor", {"channel": act["channel"]})
        elif k == "layout":
            self.call("ui.layout", {"style": act["style"]})
            self.controls = []
        elif k == "save_reload":
            r = self.call("file.save", {"path": act["path"]})
            if Path(act["path"]).exists():
                self.call("ui.open", {"file": act["path"]})
        elif k == "open":
            self.call("ui.open", {"file": act["file"]})
            self.controls = []
        elif k == "repaint":
            self.call("ui.repaint")
        elif k == "welcome":
            self.call("ui.welcome", {"choice": act["choice"]})
        elif k == "start_layout":
            self.call("ui.layout", {"style": act["style"]})

    # --- the run ---------------------------------------------------------------------------------------------------
    def responsive(self):
        t0 = time.time()
        while time.time() - t0 < self.a.hang_seconds:
            if not self.alive():
                return True  # (dead, not hung: the caller sees it)
            try:
                self.client.call("ui.window", {})
                return True
            except RuntimeError:
                return True
            except OSError:
                time.sleep(1)
        return False

    def finish(self, status, detail=""):
        rc = self.proc.poll() if self.proc else None
        if status == "hang" and self.alive():
            os.kill(self.proc.pid, signal.SIGABRT)  # ASan (handle_abort=1) prints where the main thread is
            try:
                self.proc.wait(30)
            except subprocess.TimeoutExpired:
                self.proc.kill()
            rc = self.proc.poll()
        elif status == "timeout" and self.alive():
            # the app's own shutdown (engine, plugins, windows torn down in order) is part of the test
            self.log.write(json.dumps({"k": "quit", "n": self.count, "t": round(time.time() - self.t0, 2)}) + "\n")
            answer = self.call("app.quit", {"discard": True})
            terminated = not (isinstance(answer, dict) and answer.get("quitting"))
            if terminated:
                self.proc.terminate()  # (an older build without app.quit)
            try:
                rc = self.proc.wait(self.a.quit_seconds)
                if rc != 0 and not terminated:
                    status, detail = "quit-crash", f"exit code {rc} on quit"
            except subprocess.TimeoutExpired:
                status, detail = "exit-hang", f"did not quit within {self.a.quit_seconds} s of app.quit"
                os.kill(self.proc.pid, signal.SIGABRT)
                try:
                    self.proc.wait(30)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
                rc = self.proc.poll()
        reports = sorted(p.name for p in self.out.glob("san.*"))
        result = {"status": status, "detail": detail, "actions": self.count, "rc": rc, "seed": self.a.seed,
                  "layout": self.a.layout, "project": self.a.project, "sanitizer_reports": reports,
                  "seconds": round(time.time() - self.t0, 1)}
        (self.out / "result.json").write_text(json.dumps(result, indent=1))
        print(json.dumps(result))
        return 0 if status == "timeout" and not reports else 1  # (a quit that crashed or hung is a failure too)

    def main(self):
        self.t0 = time.time()
        if not self.start(self.a.project):
            return self.finish("start-failed" if self.alive() else "crash", "no API answer after start")
        replay = None
        if self.a.replay:
            replay = [json.loads(l) for l in open(self.a.replay) if l.strip()]
        else:
            first = [{"k": "welcome", "choice": "skip"}, {"k": "start_layout", "style": self.a.layout}]
            replay_head = first
        rng = random.Random(self.a.seed)
        end = self.t0 + self.a.minutes * 60
        queue = list(replay) if replay is not None else list(replay_head)
        while True:
            if replay is not None and (not queue or queue[0].get("k") == "quit"):
                return self.finish("timeout", "replay finished")
            if replay is None and time.time() > end:
                return self.finish("timeout")
            act = queue.pop(0) if queue else self.plan(rng)
            act.setdefault("n", self.count)
            act["t"] = round(time.time() - self.t0, 2)
            self.log.write(json.dumps(act) + "\n")
            self.count += 1
            try:
                self.run_action(act)
            except subprocess.TimeoutExpired:
                pass
            time.sleep(self.a.delay)
            if not self.alive():
                return self.finish("crash", f"died after action {act['n']} ({act['k']})")
            if self.count % 10 == 0 and not self.responsive():
                return self.finish("hang", f"no API answer for {self.a.hang_seconds} s after action {act['n']}")


def parse():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--app", required=True)
    p.add_argument("--display", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--minutes", type=float, default=5)
    p.add_argument("--layout", default="pattern", choices=LAYOUTS)
    p.add_argument("--project", default="")
    p.add_argument("--projects", nargs="*", default=[], help="files the 'open' action picks from")
    p.add_argument("--replay", default="")
    p.add_argument("--delay", type=float, default=0.08, help="seconds after each action")
    p.add_argument("--hang-seconds", type=float, default=45)
    p.add_argument("--start-seconds", type=float, default=240)
    p.add_argument("--quit-seconds", type=float, default=120)
    p.add_argument("--xdotool", default="xdotool")
    return p.parse_args()


if __name__ == "__main__":
    sys.exit(Monkey(parse()).main())

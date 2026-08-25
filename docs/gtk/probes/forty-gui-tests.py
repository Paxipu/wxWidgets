#!/usr/bin/env python3
"""GUI tests for the Forty Thieves demo, driven with real XTEST input.

Every check here is a statement about what the user sees, measured from screen
captures of the real binary under a private Xvfb -- the demo has no unit tests
and its drawing is the thing under test, so pixels are the only honest oracle.

  deal    clicking the pack must deal a card *visibly*, and the result must
          agree with what a full repaint from the game state produces
  drag     a card picked up with the mouse must follow it across the board
           (this is the path that saves the pixels under the card and reads
           them back again, which is what GTK4 cannot do on a screen DC)
  resize   the board must survive the window changing size
  undo     a right-click must undo the deal, visibly
  quiet    the demo must not print assertions or GTK criticals

Run against a built demo:

    python3 forty_gui_tests.py path/to/forty
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

import numpy as np
from PIL import Image

CANVAS_GREEN = (0, 128, 0)
CARD_W, CARD_H = 50, 70

# Pile positions, from Game::Game() in demos/forty/game.cpp.
PACK = (2, 2 + 4 * (CARD_H + 2))
# Bases are Pile(x, y, 0, 12) holding four cards, so the top one is 3*12 down.
BASE0_TOP = (8 + 2 * (CARD_W + 2), 2 + 3 * 12)
# A patch of empty baize to drag onto, well clear of every pile and the score.
EMPTY_SPOT = (300, 300)

# A card is 50x70 = 3500 px, so a real change is thousands of pixels; this only
# has to be above capture noise.
CHANGE_THRESHOLD = 200


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def settle(seconds):
    time.sleep(seconds)


def display_is_up(display):
    return run(["xdpyinfo"], env=dict(os.environ, DISPLAY=display)).returncode == 0


def terminate(proc):
    if proc is None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()


class Session:
    """A running demo on a private Xvfb, with the startup dialog dismissed."""

    def __init__(self, binary, outdir, ld_library_path=None):
        self.binary = os.path.abspath(binary)
        self.outdir = outdir
        self.ld_library_path = ld_library_path
        self.display = None
        self.xvfb = None
        self.app = None
        self.home = None
        self.stderr_path = os.path.join(outdir, "stderr.txt")
        self._stderr = None
        self.wid = None
        self.origin = None

    # -- plumbing ---------------------------------------------------------
    def xdo(self, *args):
        return run(["xdotool", *args], env=dict(os.environ, DISPLAY=self.display))

    def start_xvfb(self, first=90, last=160):
        for n in range(first, last):
            display = f":{n}"
            if display_is_up(display):
                continue
            proc = subprocess.Popen(
                ["Xvfb", display, "-screen", "0", "1280x1024x24", "-nolisten", "tcp"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
            deadline = time.monotonic() + 10.0
            while time.monotonic() < deadline:
                if proc.poll() is not None:
                    break
                if display_is_up(display):
                    return display, proc
                settle(0.2)
            terminate(proc)
        return None, None

    def find_window(self, name, timeout=25.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            ids = self.xdo("search", "--name", name).stdout.split()
            if ids:
                return ids[-1]
            settle(0.2)
        return None

    def window_gone(self, name, timeout=10.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not self.xdo("search", "--name", name).stdout.split():
                return True
            settle(0.2)
        return False

    def geometry(self, wid=None):
        out = self.xdo("getwindowgeometry", "--shell", wid or self.wid).stdout
        g = {}
        for line in out.splitlines():
            if "=" in line:
                k, _, v = line.partition("=")
                g[k.strip()] = int(v)
        return g

    def box(self, g):
        x0, y0 = max(0, g["X"]), max(0, g["Y"])
        return (x0, y0, min(x0 + g["WIDTH"], 1280), min(y0 + g["HEIGHT"], 1024))

    def grab(self, name, box=None):
        path = os.path.join(self.outdir, name)
        res = run(["scrot", "-o", path], env=dict(os.environ, DISPLAY=self.display))
        if res.returncode != 0:
            raise RuntimeError(f"scrot failed: {res.stderr.strip()}")
        img = Image.open(path).convert("RGB")
        if box is not None:
            img = img.crop(box)
            img.save(path)
        return np.asarray(img)

    def grab_painted(self, name, box, timeout=20.0):
        """Grab only once the window has actually drawn something.

        A window exists as soon as it is mapped, well before GTK puts pixels in
        it; grabbing then returns solid black and every later comparison is
        measured against nothing.
        """
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            img = self.grab(name, box)
            if int(img.max()) > 0 and np.unique(img.reshape(-1, 3), axis=0).shape[0] > 2:
                return img
            settle(0.3)
        raise RuntimeError("the window never painted anything")

    # -- lifecycle --------------------------------------------------------
    def __enter__(self):
        os.makedirs(self.outdir, exist_ok=True)
        self.display, self.xvfb = self.start_xvfb()
        if self.display is None:
            raise RuntimeError("could not start a private Xvfb")

        self.home = tempfile.mkdtemp(prefix="forty-home-")
        env = dict(os.environ, DISPLAY=self.display, HOME=self.home,
                   GDK_BACKEND="x11")
        if self.ld_library_path:
            env["LD_LIBRARY_PATH"] = self.ld_library_path
        env.pop("WAYLAND_DISPLAY", None)

        self._stderr = open(self.stderr_path, "w")
        self.app = subprocess.Popen(
            [self.binary], env=env, cwd=os.path.dirname(self.binary),
            stdout=subprocess.DEVNULL, stderr=self._stderr,
        )
        settle(0.5)
        if self.app.poll() is not None:
            raise RuntimeError(f"the demo exited at once: {self.stderr_text().strip()}")

        self.dismiss_dialog()

        self.wid = self.find_window("Forty Thieves")
        if self.wid is None:
            raise RuntimeError("the game window never appeared")
        g = self.geometry()
        img = self.grab_painted("board-initial.png", self.box(g))
        self.origin = self.canvas_origin(img)
        if self.origin is None:
            raise RuntimeError("could not find the green canvas")
        settle(0.8)
        return self

    def __exit__(self, *exc):
        terminate(self.app)
        if self._stderr:
            self._stderr.close()
        terminate(self.xvfb)
        # Wait until the server has really let go of the display number: a
        # dying Xvfb still answers for a moment, and the next session would
        # then either skip a free number or collide with this one.
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline and display_is_up(self.display):
            settle(0.2)
        shutil.rmtree(self.home, ignore_errors=True)
        return False

    def dismiss_dialog(self, name="probe"):
        wid = self.find_window("Player Selection")
        if wid is None:
            raise RuntimeError("the Player Selection dialog never appeared")
        settle(0.5)
        g = self.geometry(wid)
        img = self.grab_painted("dialog.png", self.box(g))

        # The list box and the text field are the only pure white areas. The
        # threshold has to stay above the dialog's own background, which the
        # default GTK4 theme paints at (246,245,244) and the GTK+ 2 theme at
        # (220,218,213) -- close enough to white that a laxer test reports the
        # whole dialog as one band.
        wide = (np.all(img >= 250, axis=-1).mean(axis=1) >= 0.6)
        bands, start = [], None
        for y, white in enumerate(wide):
            if white and start is None:
                start = y
            elif not white and start is not None:
                bands.append((start, y - 1))
                start = None
        if start is not None:
            bands.append((start, len(wide) - 1))
        if not bands:
            raise RuntimeError("the dialog has no white areas -- not painted?")

        # The text field is the lowest white thing in the dialog, sitting just
        # above the buttons. Aim a little above the bottom of the lowest white
        # band rather than at a band's midpoint: under the GTK+ 2 theme the
        # list box and the field touch, so they come back as a single band and
        # a midpoint would land in the list box instead.
        field_bottom = bands[-1][1]

        # Xvfb has no window manager, so focus stays at PointerRoot and keys go
        # to whatever the pointer is over: park it on the field before typing.
        self.xdo("mousemove", "--sync", str(g["X"] + g["WIDTH"] // 2),
                 str(g["Y"] + field_bottom - 8))
        settle(0.4)
        self.xdo("click", "1")
        settle(0.4)
        self.xdo("type", "--delay", "50", name)
        settle(0.5)
        self.xdo("key", "Return")  # m_OK->SetDefault()
        if self.window_gone("Player Selection", timeout=6.0):
            return

        # Fall back to clicking OK, which sits below the field behind a 10 px
        # border and is the first of the two buttons.
        self.xdo("mousemove", "--sync", str(g["X"] + 50),
                 str(g["Y"] + field_bottom + 32))
        settle(0.3)
        self.xdo("click", "1")
        if not self.window_gone("Player Selection", timeout=6.0):
            raise RuntimeError("the Player Selection dialog would not close")

    # -- helpers ----------------------------------------------------------
    @staticmethod
    def canvas_origin(img):
        green = np.all(img == np.array(CANVAS_GREEN, dtype=img.dtype), axis=-1)
        rows = np.flatnonzero(green.any(axis=1))
        cols = np.flatnonzero(green.any(axis=0))
        if rows.size == 0 or cols.size == 0:
            return None
        return int(cols[0]), int(rows[0])

    def root(self, canvas_x, canvas_y):
        """Canvas coordinates -> root window coordinates."""
        g = self.geometry()
        ox, oy = self.origin
        return g["X"] + ox + canvas_x, g["Y"] + oy + canvas_y

    def board(self, name):
        return self.grab(name, self.box(self.geometry()))

    def stderr_text(self):
        self._stderr.flush()
        with open(self.stderr_path) as f:
            return f.read()


def changed(a, b, region=None):
    """Pixels differing between two captures, optionally inside a region."""
    if a.shape != b.shape:
        return -1
    if region is not None:
        x0, y0, x1, y1 = region
        a, b = a[y0:y1, x0:x1], b[y0:y1, x0:x1]
    return int(np.count_nonzero(np.any(a != b, axis=-1)))


# --------------------------------------------------------------------------
# the tests
# --------------------------------------------------------------------------

def test_deal(s):
    """Clicking the pack deals a card, and it shows up straight away."""
    before = s.board("deal-1-before.png")
    s.xdo("mousemove", "--sync", *map(str, s.root(PACK[0] + CARD_W // 2,
                                                  PACK[1] + CARD_H // 2)))
    settle(0.4)
    s.xdo("click", "1")
    settle(1.2)
    after = s.board("deal-2-after.png")
    n = changed(before, after)

    # Cross-check: a full repaint from the game state must agree with what the
    # click drew. If they disagree the screen is showing something the game
    # does not think is true.
    g = s.geometry()
    s.xdo("windowsize", s.wid, str(g["WIDTH"] - 40), str(g["HEIGHT"] - 30))
    settle(1.0)
    s.xdo("windowsize", s.wid, str(g["WIDTH"]), str(g["HEIGHT"]))
    settle(1.5)
    repainted = s.board("deal-3-repainted.png")
    drift = changed(after, repainted)

    if n <= CHANGE_THRESHOLD:
        return False, f"the click changed only {n} pixels -- the deal was not drawn"
    if drift > CHANGE_THRESHOLD:
        return False, (f"the click drew {n} pixels but a repaint disagrees by "
                       f"{drift} -- the screen does not match the game state")
    return True, f"deal drew {n} pixels, and a full repaint agrees (drift {drift})"


def test_drag(s):
    """A card picked up follows the pointer, and lands where it is dropped."""
    before = s.board("drag-1-before.png")

    start = s.root(BASE0_TOP[0] + CARD_W // 2, BASE0_TOP[1] + CARD_H // 2)
    target = s.root(EMPTY_SPOT[0], EMPTY_SPOT[1])

    s.xdo("mousemove", "--sync", *map(str, start))
    settle(0.4)
    s.xdo("mousedown", "1")
    settle(0.4)

    # Walk there rather than jumping, so wx sees a sequence of drag motions.
    steps = 8
    for i in range(1, steps + 1):
        x = start[0] + (target[0] - start[0]) * i // steps
        y = start[1] + (target[1] - start[1]) * i // steps
        s.xdo("mousemove", "--sync", str(x), str(y))
        settle(0.12)
    settle(0.8)

    holding = s.board("drag-2-holding.png")

    # The card must now be visible under the pointer. Look only at the patch of
    # baize it was dragged onto, which was empty green before.
    ox, oy = s.origin
    region = (ox + EMPTY_SPOT[0] - CARD_W, oy + EMPTY_SPOT[1] - CARD_H,
              ox + EMPTY_SPOT[0] + CARD_W, oy + EMPTY_SPOT[1] + CARD_H)
    on_target = changed(before, holding, region)

    s.xdo("mouseup", "1")
    settle(1.2)
    after = s.board("drag-3-dropped.png")

    if on_target <= CHANGE_THRESHOLD:
        return False, (f"the dragged card never appeared under the pointer "
                       f"({on_target} pixels changed where it was dragged to)")

    # Dropping on empty baize is not a legal move, so the card goes home and
    # the board must look like it did before the drag.
    residue = changed(before, after)
    if residue > CHANGE_THRESHOLD:
        return False, (f"after dropping on empty baize the board differs by "
                       f"{residue} pixels -- the card did not go back cleanly")
    return True, (f"the card followed the pointer ({on_target} pixels at the "
                  f"target) and went home cleanly on an illegal drop")


def test_resize(s):
    """The board survives the window being resized."""
    g = s.geometry()
    before = s.board("resize-1-before.png")

    for w, h in ((g["WIDTH"] + 120, g["HEIGHT"] + 90),
                 (g["WIDTH"] - 80, g["HEIGHT"] - 60),
                 (g["WIDTH"], g["HEIGHT"])):
        s.xdo("windowsize", s.wid, str(w), str(h))
        settle(1.2)

    after = s.board("resize-2-after.png")
    if after.shape != before.shape:
        return False, f"the window did not come back to its old size: {after.shape}"

    n = changed(before, after)
    if n > CHANGE_THRESHOLD:
        return False, f"the board differs by {n} pixels after resizing back"

    # And it must still be a board, not a blank or black window.
    green = np.all(after == np.array(CANVAS_GREEN, dtype=after.dtype), axis=-1)
    if green.mean() < 0.2:
        return False, f"only {green.mean():.1%} of the window is baize -- the board is gone"
    return True, f"identical after growing and shrinking ({green.mean():.0%} baize)"


def test_undo(s):
    """A right-click undoes the last move, visibly."""
    before = s.board("undo-1-before.png")
    s.xdo("mousemove", "--sync", *map(str, s.root(PACK[0] + CARD_W // 2,
                                                  PACK[1] + CARD_H // 2)))
    settle(0.3)
    s.xdo("click", "1")          # deal one
    settle(1.0)
    dealt = s.board("undo-2-dealt.png")
    if changed(before, dealt) <= CHANGE_THRESHOLD:
        return False, "could not deal a card to undo"

    # Right-click anywhere on the baize undoes it.
    s.xdo("mousemove", "--sync", *map(str, s.root(EMPTY_SPOT[0], EMPTY_SPOT[1])))
    settle(0.3)
    s.xdo("click", "3")
    settle(1.2)
    undone = s.board("undo-3-undone.png")

    n = changed(dealt, undone)
    if n <= CHANGE_THRESHOLD:
        return False, f"the right-click changed only {n} pixels -- the undo was not drawn"
    return True, f"undo redrew {n} pixels"


def test_quiet(s):
    """The demo prints no assertions and no GTK criticals."""
    text = s.stderr_text()
    bad = [ln for ln in text.splitlines()
           if re.search(r"assert|CRITICAL|Gtk-WARNING|wxWidgets", ln, re.I)]
    if bad:
        return False, "stderr complains:\n    " + "\n    ".join(bad[:10])
    return True, "stderr is clean"


TESTS = [
    ("deal", test_deal),
    ("drag", test_drag),
    ("resize", test_resize),
    ("undo", test_undo),
    ("quiet", test_quiet),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("--outdir", default=None)
    ap.add_argument("--only", default=None, help="run just this test")
    ap.add_argument("--ld-library-path", default=os.environ.get("LD_LIBRARY_PATH"),
                    help="passed to the demo, for builds whose libraries are "
                         "not on the default loader path")
    args = ap.parse_args()

    outdir = args.outdir or tempfile.mkdtemp(prefix="forty-tests-")
    os.makedirs(outdir, exist_ok=True)

    tests = [t for t in TESTS if args.only in (None, t[0])]
    if not tests:
        print(f"no such test: {args.only}", file=sys.stderr)
        return 2

    results = []
    # Each test gets its own process, so one cannot leave state for the next.
    for name, fn in tests:
        ok, msg = False, "not run"
        for attempt in range(2):
            sub = os.path.join(outdir, name if attempt == 0 else f"{name}-retry")
            try:
                with Session(args.binary, sub, args.ld_library_path) as s:
                    ok, msg = fn(s)
                break
            except Exception as exc:          # noqa: BLE001 - report, don't crash
                ok, msg = False, f"harness error: {exc}"
                if attempt == 0:
                    print(f"  (retrying {name} after: {exc})")
        results.append((name, ok, msg))
        print(f"[{'PASS' if ok else 'FAIL'}] {name}: {msg}")

    failed = [n for n, ok, _ in results if not ok]
    print()
    print(f"{len(results) - len(failed)}/{len(results)} passed"
          + (f", failed: {', '.join(failed)}" if failed else ""))
    print(f"captures in {outdir}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

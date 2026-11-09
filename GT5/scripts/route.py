import os as _gt5_os  # this clone's folders, wherever it was cloned to
GT5_DIR = _gt5_os.path.dirname(_gt5_os.path.dirname(_gt5_os.path.abspath(__file__)))
BASE_DIR = _gt5_os.path.dirname(GT5_DIR)
"""Scripted menu routes for a live session, each step checked only by timing: route.py <name> [name ...]

    home    from a fresh boot through the dialog and movie to My Home
    sunday  from My Home into the first Sunday Cup race's grid screen
    bspec   from My Home into the first B-Spec Sunday Cup race, started
    rain    from My Home to driving a Practice free run at High Speed Ring in the rain
    photo   from that free run through its replay to Photo Mode, and take a shot
"""
import subprocess
import sys
import time

GP = (GT5_DIR + r"\scripts\gp.py")


def step(button, wait, polls=None):
    a = ["python", GP]
    if button:
        a.append(button)
    if polls:
        a += ["--polls", str(polls)]
    a += ["--wait", str(wait)]
    subprocess.run(a, capture_output=True)


ROUTES = {
    "home": [(None, 40), ("cross", 14), ("cross", 8), ("cross", 20), ("cross", 18)],
    "rain": [("right", 2, 16), ("right", 2, 16), ("down", 2, 16), ("cross", 10), ("right", 3, 16), ("cross", 6),
             ("cross", 8), ("cross", 60), ("cross", 25), ("cross", 12, 1500)],
    "photo": [("start", 4, 30), ("right", 1.6, 16), ("right", 1.6, 16), ("right", 1.6, 16), ("cross", 8), ("right", 2.5, 16),
              ("cross", 14), ("start", 4, 30), ("left", 3, 16), ("cross", 10), ("cross", 5), ("cross", 30)],
    "bspec": [("right", 2, 24), ("right", 2, 24), ("cross", 9, 30), ("cross", 8, 30), ("cross", 8, 30), ("cross", 40, 30), ("cross", 3, 24),
              ("down", 2, 24), ("cross", 40, 30), ("cross", 30, 30)],
    "sunday": [("right", 2, 16), ("cross", 8), ("cross", 4), ("cross", 10), ("cross", 6), ("cross", 60)],
}
for name in sys.argv[1:]:
    t = time.time()
    for s in ROUTES[name]:
        step(*s)
    print(name, "done in %.0f s" % (time.time() - t))

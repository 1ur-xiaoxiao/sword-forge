"""图形版 C 接口验收：使用临时项目和临时存档，不影响玩家存档。"""
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
(ROOT / "work").mkdir(exist_ok=True)
passed = 0


def check(name):
    global passed
    passed += 1
    print(f"PASS WEB {passed}: {name}")


with tempfile.TemporaryDirectory(dir=ROOT / "work", prefix="web_test_") as directory:
    project = Path(directory).resolve()
    assert project.is_relative_to((ROOT / "work").resolve())
    (project / "build").mkdir()
    packaged = os.environ.get("FORGE_TEST_EXE")
    executable = project / ("sword_forge.exe" if packaged else "build/sword_forge.exe")
    shutil.copy2(Path(packaged) if packaged else ROOT / "build/sword_forge.exe", executable)
    if not packaged: shutil.copytree(ROOT / "web", project / "web")
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    base = f"http://127.0.0.1:{port}"
    process = None

    def request(path, payload=None, origin=True):
        data = None if payload is None else urllib.parse.urlencode(payload).encode()
        headers = {"Origin": base} if origin else {}
        req = urllib.request.Request(base + path, data=data, headers=headers)
        with urllib.request.urlopen(req, timeout=4) as response:
            return response.read(), response.headers

    def state():
        return json.loads(request("/api/state")[0])

    def action(name, p=0, q=0):
        return json.loads(request("/api/action", {"action": name, "p": p, "q": q})[0])

    def start():
        child = subprocess.Popen([str(executable), "--headless", "--port", str(port)])
        for _ in range(60):
            if child.poll() is not None:
                raise RuntimeError(f"server exited: {child.returncode}")
            try:
                state()
                return child
            except (OSError, urllib.error.URLError):
                time.sleep(.1)
        child.terminate()
        raise RuntimeError("server startup timed out")

    try:
        process = start()
        first = state()
        assert first["gold"] == 180 and len(first["weapons"]) == 1
        assert len(first["recipes"]) == 6 and len(first["areas"]) == 6
        for r in first["recipes"]:
            assert sum(r["weights"]) == sum(r["luckyWeights"]) == 10000
        check("C backend exposes initial state and exact probability tables")

        for path, expected_type in [("/", "text/html"), ("/styles.css", "text/css"),
                                    ("/app.js", "text/javascript"), ("/scene.js", "text/javascript"), ("/dungeon.js", "text/javascript"),
                                    ("/vendor/rot.js", "text/javascript"), ("/vendor/ROT-LICENSE.txt", "text/plain"), ("/assets/forge.svg", "image/svg+xml")]:
            data, headers = request(path)
            assert len(data) > 100 and expected_type in headers["Content-Type"]
            data.decode("utf-8")
        check("all UI assets served locally as valid UTF-8 with correct content types")

        try:
            request("/api/action", {"action": "forge", "p": 0, "q": 0}, origin=False)
            raise AssertionError("missing Origin accepted")
        except urllib.error.HTTPError as error:
            assert error.code == 403
        try:
            request("/../saves/game.sav")
            raise AssertionError("traversal accepted")
        except urllib.error.HTTPError as error:
            assert error.code == 404
        assert state() == first
        check("foreign-origin mutations and arbitrary file paths rejected")

        bad = action("forge", 3, 0)
        assert bad["result"] != 0 and bad["gold"] == 180 and len(bad["weapons"]) == 1
        forged = action("forge", 0, 1)
        assert forged["result"] == 0 and forged["latest"] == 1
        assert forged["materials"][:2] == [8, 4] and forged["items"][1] >= 0
        assert len(forged["weapons"]) == 2 and forged["saveOk"]
        equipped = action("equip", 1)
        assert equipped["equipped"] == 1
        upgraded = action("upgrade", 1, 1)
        assert upgraded["result"] == 0
        assert upgraded["weapons"][1]["upgrade"] in (0, 1)
        check("forge, luck, equip and upgrade invoke real C functions and auto-save")

        bought = action("buy", 0, 1)
        assert bought["result"] == 0
        rest = action("rest")
        assert rest["hp"] == rest["maxHp"] and rest["energy"] == 100
        battle = action("explore", 0)
        assert battle["battle"]["active"] and battle["energy"] == 85
        prohibited = action("rest")
        assert prohibited["result"] != 0 and prohibited["energy"] == 85
        for _ in range(100):
            battle = action("battle", 0)
            if not battle["battle"]["active"]:
                break
        assert not battle["battle"]["active"] and battle["wins"] == 1
        assert battle["saveOk"]
        check("shop, rest, encounter locking, actual turn combat and victory loot")

        assert action("explore", 1)["result"] == 2
        assert action("boss", 0)["result"] == 2
        gathered = action("gather", 0)
        assert gathered["result"] == 0 and gathered["event"]["kind"] in range(4)
        assert gathered["areas"][0]["surveys"] == 1
        assert action("rest")["result"] == 2
        assert action("event", 2)["result"] == 1
        pending = state()["event"]
        action("quit"); process.wait(timeout=4); process = start()
        assert state()["event"] == pending
        resolved = action("event", 1)
        assert resolved["result"] == 0 and resolved["event"]["kind"] == -1
        assert action("event", 1)["result"] == 1
        check("new route gates, gathering, persisted event selection and single-claim reward")

        primary = state()
        assert primary["activeSlot"] == 0 and len(primary["slots"]) == 4
        fresh = action("slot", 1)
        assert fresh["activeSlot"] == 1 and fresh["gold"] == 180 and len(fresh["weapons"]) == 1
        fresh = action("forge", 0)
        assert fresh["result"] == 0 and len(fresh["weapons"]) == 2
        action("quit"); process.wait(timeout=4); process=start()
        assert state()["activeSlot"] == 1 and state()["gold"] == fresh["gold"]
        restored = action("slot", 0)
        assert restored["gold"] == primary["gold"] and restored["weapons"] == primary["weapons"]
        (project/"saves/game.slot3.sav").write_bytes(b"BAD SLOT")
        refused = action("slot", 2)
        assert refused["result"] == 6 and refused["activeSlot"] == 0
        assert (project/"saves/game.slot3.sav").read_bytes() == b"BAD SLOT"
        check("four independent slots preserve original save, last selection survives cold restart and damaged slot cannot overwrite active progress")
        old = state()
        for ids in ("1-999", "1-1", "1-2", "1-", "-1", "1%2D2"):
            refused = json.loads(request("/api/action", {"action":"salvage_many", "ids":ids})[0])
            assert refused["result"] != 0 and refused["weapons"] == old["weapons"] and refused["gold"] == old["gold"]
        bulk = json.loads(request("/api/action", {"action":"salvage_many", "ids":"1"})[0])
        assert bulk["result"] == 0 and len(bulk["weapons"]) == 1 and bulk["weapons"][0]["id"] == 2
        # Restore the primary fixture used by the existing restart assertions.
        action("quit");process.wait(timeout=4)
        (project/"saves/game.sav").write_bytes((project/"saves/game.slot2.sav").read_bytes())
        process=start();action("equip",1)
        # This fresh slot has no wins; replay one patrol to match the restart fixture.
        action("explore",0)
        while state()["battle"]["active"]: action("battle",4 if state()["battle"]["cooldown"]==0 else 0)
        check("batch API rejects malformed, duplicate and equipped ids without partial rewards")
        previous = (project / "saves/game.sav").read_bytes()
        action("quit")
        process.wait(timeout=4)
        process = start()
        resumed = state()
        assert resumed["wins"] == 1 and len(resumed["weapons"]) == 2 and resumed["equipped"] == 1
        assert (project / "saves/game.sav").read_bytes() == previous
        action("quit")
        process.wait(timeout=4)
        check("graphical backend restart resumes legacy-compatible saves exactly")

        (project / "saves/game.sav").write_bytes(b"INVALID SAVE")
        process = start()
        assert state()["corrupted"]
        assert action("forge", 0, 0)["result"] != 0
        assert (project / "saves/game.sav").read_bytes() == b"INVALID SAVE"
        reset = action("new", 1)
        assert not reset["corrupted"] and reset["gold"] == 180
        assert (project / "saves/game.before-new.sav").read_bytes() == b"INVALID SAVE"
        action("quit")
        process.wait(timeout=4)
        check("damaged save is preserved, explicit new journey creates backup first")
    finally:
        if process and process.poll() is None:
            process.terminate()
            process.wait(timeout=4)

print(f"{passed} graphical API verification groups passed.")

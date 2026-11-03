"""tools/mock_server.py must send the same JSON shape as the firmware.

The firmware's exact text is pinned by test/test_webjson/test_webjson.cpp; this test reads the
expected strings from there and compares the mock's messages with them: same keys in the same
order, same value types, same lap entry layout (L in web/race.js). Values are not compared.

    python3 -m unittest discover -s test/py
"""
import ast
import importlib.util
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("mock_server", ROOT / "tools" / "mock_server.py")
mock = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mock)


def firmware_json(test_name):
    """The first TEST_ASSERT_EQUAL_STRING text in the given test of test_webjson.cpp, parsed."""
    src = (ROOT / "test" / "test_webjson" / "test_webjson.cpp").read_text()
    body = src[src.index(f"void {test_name}()"):]
    call = body[body.index("TEST_ASSERT_EQUAL_STRING("):body.index("out.c_str()")]
    text = "".join(ast.literal_eval(f'"{s}"') for s in re.findall(r'"((?:[^"\\]|\\.)*)"', call))
    return json.loads(text)


def shape(v):
    """Keys (in order) and value types; for arrays the distinct shapes of their elements."""
    if isinstance(v, dict):
        return [(k, shape(x)) for k, x in v.items()]
    if isinstance(v, list):
        shapes = []
        for x in v:
            if shape(x) not in shapes:
                shapes.append(shape(x))
        return ("list", shapes)
    return type(v).__name__


class MockContract(unittest.TestCase):
    def setUp(self):
        t = mock.Timer()
        t.multi = True
        t.driver_add("Martin")
        t.driver_add("Jana")
        start = mock.now_us() - 60_000_000
        for i in range(4):                                    # car 1 (driver 1) and a blink car
            t.passing(0x1B39, start + i * 8_000_000)
            t.passing(465, start + 1_000_000 + i * 9_000_000, precise=False)
        t.car_set_driver(1, 1)
        t.passing(0x1B39, start + 40_000_000)
        t.delete(1)
        t.race = {"state": "running", "go": start // 1000, "laps": 10, "minutes": 0, "order": [], "heat": 0,
                  "heatState": "running", "heatGo": 0, "prep": 0}
        self.timer = t

    def assertSameShape(self, firmware, ours):
        self.assertEqual(shape(firmware), shape(ours))

    def test_state(self):
        st = self.timer.state()
        self.assertTrue(st["drivers"] and st["stats"] and st["laps"] and len(st["cars"]) > 1)
        self.assertSameShape(firmware_json("test_state"), st)

    def test_live(self):
        self.assertSameShape(firmware_json("test_live"), self.timer.live())

    def test_scan(self):
        self.timer.scan_until = 0
        self.assertSameShape(firmware_json("test_scan"), self.timer.scan_results())


if __name__ == "__main__":
    unittest.main()

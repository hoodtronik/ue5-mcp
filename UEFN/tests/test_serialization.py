import json
import unittest

from listener.serialization import Serializer
from tests import _fake_unreal as u


class TestSerializer(unittest.TestCase):
    def setUp(self):
        self.s = Serializer(u)

    def _roundtrip(self, obj):
        # Everything the serializer emits must be JSON-encodable.
        out = self.s.serialize(obj)
        json.dumps(out)
        return out

    def test_primitives(self):
        self.assertEqual(self._roundtrip(5), 5)
        self.assertEqual(self._roundtrip("x"), "x")
        self.assertEqual(self._roundtrip(True), True)
        self.assertIsNone(self._roundtrip(None))

    def test_vector(self):
        self.assertEqual(self._roundtrip(u.Vector(1, 2, 3)), {"x": 1.0, "y": 2.0, "z": 3.0})

    def test_rotator(self):
        self.assertEqual(self._roundtrip(u.Rotator(10, 20, 30)),
                         {"pitch": 10.0, "yaw": 20.0, "roll": 30.0})

    def test_color(self):
        self.assertEqual(self._roundtrip(u.LinearColor(0.1, 0.2, 0.3, 1.0)),
                         {"r": 0.1, "g": 0.2, "b": 0.3, "a": 1.0})

    def test_transform(self):
        out = self._roundtrip(u.Transform(u.Vector(1, 2, 3), u.Quat(), u.Vector(1, 1, 1)))
        self.assertEqual(out["location"], {"x": 1.0, "y": 2.0, "z": 3.0})
        self.assertIn("rotation", out)
        self.assertEqual(out["scale"], {"x": 1.0, "y": 1.0, "z": 1.0})

    def test_asset_data(self):
        out = self._roundtrip(u.AssetData())
        self.assertEqual(out["type"], "AssetData")
        self.assertEqual(out["asset_name"], "SM_Example")

    def test_generic_object_ref(self):
        out = self._roundtrip(u.Object(name="SM_Foo", path="/Game/SM_Foo.SM_Foo"))
        self.assertEqual(out["type"], "Object")
        self.assertEqual(out["name"], "SM_Foo")
        self.assertEqual(out["path"], "/Game/SM_Foo.SM_Foo")
        self.assertEqual(out["class"], "StaticMesh")

    def test_containers(self):
        out = self._roundtrip({"v": u.Vector(1, 0, 0), "list": [1, 2, u.Vector(0, 1, 0)]})
        self.assertEqual(out["v"], {"x": 1.0, "y": 0.0, "z": 0.0})
        self.assertEqual(out["list"][2], {"x": 0.0, "y": 1.0, "z": 0.0})

    def test_cycle_detection(self):
        a = {}
        a["self"] = a
        out = self._roundtrip(a)
        self.assertEqual(out["self"]["type"], "cycle")

    def test_depth_limit(self):
        s = Serializer(u, max_depth=2)
        deep = {"a": {"b": {"c": {"d": 1}}}}
        out = s.serialize(deep)
        json.dumps(out)
        # Somewhere below depth 2 the tree is truncated.
        self.assertIn("truncated", json.dumps(out))

    def test_collection_size_limit(self):
        s = Serializer(u, max_items=3)
        out = s.serialize(list(range(10)))
        self.assertEqual(len(out), 4)  # 3 items + truncation marker
        self.assertIn("__truncated__", out[-1])

    def test_no_unreal_module_falls_back_to_string(self):
        s = Serializer(None)
        vec = u.Vector(1, 2, 3)
        self.assertEqual(s.serialize(vec), str(vec))


if __name__ == "__main__":
    unittest.main()

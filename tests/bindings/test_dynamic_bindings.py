import unittest

from godot.bindings import (
    Object, Node, Viewport, EditorPlugin, LineEdit, Engine, _Engine, KEY_ESCAPE, OK, FAILED)


class TestDynamicBindings(unittest.TestCase):

    def test_singletons(self):
        self.assertEqual(type(Engine), _Engine)
        self.assertTrue(callable(Engine.get_main_loop))
        ml = Engine.get_main_loop()
        self.assertTrue(isinstance(ml, Object))

    def test_constants(self):
        self.assertEqual(OK, 0)
        self.assertEqual(FAILED, 1)
        self.assertEqual(type(KEY_ESCAPE), int)

    def test_objects_unicity(self):
        # Main loop object is a Godot Object, calling `get_main_loop` from
        # python returns a different python wrapper on the same object each time.
        # However those wrappers should feel like they are the same object.
        ml = Engine.get_main_loop()
        ml2 = Engine.get_main_loop()
        self.assertEqual(ml, ml2)
        # Of course different objects should be different and equality
        # should not crash with bad given types
        self.assertNotEqual(ml, Object())
        self.assertNotEqual(ml, None)
        self.assertNotEqual(ml, "")
        self.assertNotEqual(ml, 42)

    def test_class(self):
        self.assertEqual(type(Node), type)

    def test_class_constants(self):
        self.assertTrue(hasattr(EditorPlugin, 'CONTAINER_TOOLBAR'))
        self.assertEqual(type(EditorPlugin.CONTAINER_TOOLBAR), int)

    def test_class_inheritance(self):
        self.assertTrue(issubclass(Node, Object))
        self.assertTrue(issubclass(Viewport, Node))
        self.assertTrue(issubclass(Viewport, Object))

    def test_class_methods(self):
        self.assertTrue(hasattr(LineEdit, 'is_secret'))
        v = LineEdit()
        self.assertTrue(callable(v.is_secret))
        self.assertEqual(type(v.is_secret()), bool)
        self.assertEqual(v.is_secret(), False)
        self.assertTrue(callable(v.set_secret))
        v.set_secret(True)
        self.assertEqual(v.is_secret(), True)

    def test_class_signals(self):
        pass

    def test_class_properties(self):
        self.assertTrue(hasattr(LineEdit, 'max_length'))
        v = LineEdit()
        self.assertTrue(type(v.max_length), int)
        self.assertEqual(v.max_length, 0)
        v.max_length = 42
        self.assertEqual(v.max_length, 42)


if __name__ == '__main__':
    unittest.main()

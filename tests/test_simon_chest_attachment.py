"""Regression guard for d9cb87a (Simon models: police1's chest as the 4th attachment), without the game's files.

client.dll lights another player's phone, flashlight weapon and co-op torch from the player model's attachment 3
(DrawPlayerFlashlights 0x10065260, run from StudioCalcAttachments) and exits for a model with more than 4. The
generated cof_s1..s4 get police1's Bip01 Spine3 attachment as that 4th one (tools/prepare_simon_models.py
add_attachment, called from the model build). The renderer fix for the host's phone light "in a bag" (player
dlight keys moved off the local lights' keys, engine/client/cof_renderer_patch.h) leaves those lights where this
attachment puts them. tests/test_simon_models.py checks the real models; it skips without the user's game files.
"""
import inspect
import struct
import unittest

from tools import prepare_simon_models as models

BONE, ATTACHMENT, HEADER = 112, 88, 244


def model_bytes(bones, attachments):
    """A studio header with only a bone table (offset 140) and an attachment table (offset 212)."""
    data = bytearray(HEADER)
    struct.pack_into("<ii", data, 140, len(bones), len(data))
    for name in bones:
        record = bytearray(BONE)
        record[:len(name)] = name.encode("ascii")
        data.extend(record)
    struct.pack_into("<ii", data, 212, len(attachments), len(data))
    for tag, bone in attachments:
        record = bytearray(ATTACHMENT)
        record[:len(tag)] = tag.encode("ascii")
        struct.pack_into("<i", record, 36, bone)
        struct.pack_into("<fff", record, 40, 1.0, 7.0, 5.0)
        data.extend(record)
    return data


def attachment(data, index):
    count, offset = struct.unpack_from("<ii", data, 212)
    return count, bytes(data[offset + ATTACHMENT * index:offset + ATTACHMENT * (index + 1)])


class SimonChestAttachmentTests(unittest.TestCase):
    def test_the_build_gives_every_simon_model_police1s_4th_attachment(self):
        source = inspect.getsource(models)
        build = source[source.index("    copy_controller(output, police, 3)\n"):]
        self.assertTrue(build.startswith("    copy_controller(output, police, 3)\n    add_attachment(output, police, 3)\n"))
        self.assertLess(build.index("add_attachment(output, police, 3)"), build.index("validate_model(result)"))

    def test_the_chest_is_added_as_attachment_3_with_the_bone_matched_by_name(self):
        simon = model_bytes(["Bip01", "Bip01 Spine3", "Bip01 R Hand"], [("a", 2), ("b", 2), ("c", 2)])
        police = model_bytes(["Bip01", "Bip01 Pelvis", "Bip01 Spine", "Bip01 Spine3"],
                             [("p0", 0), ("p1", 0), ("p2", 0), ("chest", 3)])
        before = [attachment(simon, i)[1] for i in range(3)]
        models.add_attachment(simon, bytes(police), 3)
        count, chest = attachment(simon, 3)
        self.assertEqual(count, 4)
        self.assertEqual([attachment(simon, i)[1] for i in range(3)], before)
        bone = struct.unpack_from("<i", chest, 36)[0]
        self.assertEqual(models.bone_names(simon)[bone], "Bip01 Spine3")
        self.assertEqual(bone, 1)  # Simon's index, not police1's 3
        donor = attachment(police, 3)[1]
        self.assertEqual(chest[:36] + chest[40:], donor[:36] + donor[40:])

    def test_never_a_5th_attachment(self):
        full = model_bytes(["Bip01 Spine3"], [("a", 0)] * 4)
        police = model_bytes(["Bip01 Spine3"], [("p", 0)] * 5)
        with self.assertRaises(ValueError):
            models.add_attachment(full, bytes(police), 4)
        # every built model is validated: a 5th attachment is refused (the synthetic header is no full model, so the
        # check is read from the source)
        validate = inspect.getsource(models.validate_model)
        self.assertIn("if not 0 <= attachments <= 4:", validate)
        self.assertIn("client.dll refuses a model with more than 4 attachments", validate)


if __name__ == "__main__":
    unittest.main()
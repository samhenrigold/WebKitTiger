"""Narrow fail-closed tests for the generated/handwritten wire equivalence."""

import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("tiger_wire_schema", ROOT / "tools/tiger-wire-schema.py")
WIRE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(WIRE)

CG = """
class ColorSpace { public: void SRGB(); void LinearSRGB(); };
using PlatformColorSpace = RetainPtr<CGColorSpaceRef>;
template<> struct ArgumentCoder<RetainPtr<CGColorSpaceRef>> {
    static void encode(Encoder&, const RetainPtr<CGColorSpaceRef>&);
    static void encode(StreamConnectionEncoder&, const RetainPtr<CGColorSpaceRef>&);
    static std::optional<RetainPtr<CGColorSpaceRef>> decode(Decoder&);
};
"""
WEB = """
class ColorSpace { public: void SRGB(); void LinearSRGB(); };
class PlatformColorSpace { public: enum class Name : uint8_t { SRGB, LinearSRGB, }; };
void ArgumentCoder<WebCore::PlatformColorSpace>::encode(Encoder& encoder, const WebCore::PlatformColorSpace& instance) {
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(instance.get())>, WebCore::PlatformColorSpace::Name>);
    encoder << instance.get();
}
void ArgumentCoder<WebCore::PlatformColorSpace>::encode(StreamConnectionEncoder& encoder, const WebCore::PlatformColorSpace& instance) {
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(instance.get())>, WebCore::PlatformColorSpace::Name>);
    encoder << instance.get();
}
std::optional<WebCore::PlatformColorSpace> ArgumentCoder<WebCore::PlatformColorSpace>::decode(Decoder& decoder) {
    auto get = decoder.decode<WebCore::PlatformColorSpace::Name>();
    if (!decoder.isValid()) [[unlikely]] return std::nullopt;
    return { WebCore::PlatformColorSpace { WTF::move(*get) } };
}
encoder << std::to_underlying<T>(value);
auto value = decoder.template decode<std::underlying_type_t<T>>();
if (value && WTF::isValidEnum<T>(*value)) return static_cast<T>(*value);
"""
COMMON = "== WebCore::ColorSpace\n    auto serializableColorSpace = decoder.decode<WebCore::PlatformColorSpace>();\n== Other\n  field : uint32_t\n"
WRAPPER = "== WebCore::PlatformColorSpace\n    auto get = decoder.decode<WebCore::PlatformColorSpace::Name>();\n"


class WireSchemaTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name)
        leaf = Path("Source/WebKit/Shared/tiger/ArgumentCodersTigerCG.cpp")
        (self.source / leaf).parent.mkdir(parents=True)
        shutil.copyfile(ROOT / "WebKit" / leaf, self.source / leaf)
        self.coder = self.source / leaf

    def normalize(self, schema=COMMON, contract=CG):
        return WIRE.normalize_serializers(schema, self.source, contract)

    def test_native_and_web_have_identical_canonical_wire(self):
        self.assertEqual(self.normalize(), self.normalize(WRAPPER + COMMON, WEB))
        self.assertIn(WIRE.CANONICAL_COLOR_SPACE, self.normalize())

    def test_other_differences_are_preserved(self):
        changed = COMMON.replace("uint32_t", "uint64_t")
        self.assertNotEqual(self.normalize(), self.normalize(changed))

    def test_missing_preprocessing_is_rejected(self):
        with self.assertRaises(ValueError):
            self.normalize(contract="")

    def test_changed_byte_mapping_is_rejected(self):
        self.coder.write_text(self.coder.read_text().replace("{ SRGB, LinearSRGB }", "{ LinearSRGB, SRGB }"))
        with self.assertRaises(ValueError):
            self.normalize()

    def test_changed_handwritten_decode_width_is_rejected(self):
        self.coder.write_text(self.coder.read_text().replace("decode<uint8_t>()", "decode<uint16_t>()"))
        with self.assertRaises(ValueError):
            self.normalize()

    def test_additional_color_space_is_rejected_on_either_side(self):
        with self.assertRaises(ValueError):
            self.normalize(contract=CG.replace("void SRGB();", "void DisplayP3();"))
        with self.assertRaises(ValueError):
            self.normalize(WRAPPER + COMMON, WEB.replace("SRGB, LinearSRGB,", "SRGB, LinearSRGB, DisplayP3,"))

    def test_changed_effective_enum_value_is_rejected(self):
        with self.assertRaises(ValueError):
            self.normalize(WRAPPER + COMMON, WEB.replace("SRGB, LinearSRGB,", "SRGB = 1, LinearSRGB,"))

    def test_generated_encoder_change_is_rejected(self):
        with self.assertRaises(ValueError):
            self.normalize(WRAPPER + COMMON, WEB.replace("encoder << instance.get();", "encoder << uint16_t(instance.get());"))

    def test_native_coder_must_be_selected(self):
        with self.assertRaises(ValueError):
            self.normalize(contract=CG.replace("template<> struct ArgumentCoder<RetainPtr<CGColorSpaceRef>>", "struct OtherCoder"))

    def test_unrelated_enum_with_same_name_is_not_a_class(self):
        self.assertEqual(self.normalize(), self.normalize(contract=CG + "enum class ColorSpace { DisplayP3 };"))

    def test_added_wrapper_member_is_not_hidden(self):
        with self.assertRaises(ValueError):
            self.normalize(WRAPPER + "  extra : uint8_t\n" + COMMON, WEB)

    def test_wrong_schema_architecture_is_rejected(self):
        with self.assertRaises(ValueError):
            self.normalize(WRAPPER + COMMON, CG)
        with self.assertRaises(ValueError):
            self.normalize(COMMON, WEB)

    def test_comments_do_not_change_contract(self):
        self.coder.write_text(self.coder.read_text() + "\n// harmless comment\n")
        self.normalize()


if __name__ == "__main__":
    unittest.main()

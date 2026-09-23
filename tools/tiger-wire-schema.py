"""Canonicalize the one generated/handwritten Tiger IPC schema equivalence.

``source_dir`` is a WebKit checkout root. ``preprocessed_contract`` must be the
complete, successfully preprocessed GeneratedSerializersShared.cpp for the build
whose decoded-member listing is supplied as ``text``. An absent or changed
contract raises ValueError; callers must not turn that into a successful check.
This is deliberately a narrow recognizer, not a general C++ parser.
"""

from pathlib import Path
import re


CANONICAL_COLOR_SPACE = "PlatformColorSpace wire: uint8 SRGB=0 LinearSRGB=1"


def _compact(text: str) -> str:
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    return re.sub(r"\s+", "", text)


def _body(code: str, signature: str) -> str:
    signature = _compact(signature)
    marker = signature + "{"
    # Do not mistake an unrelated `enum class ColorSpace` for the WebCore class.
    matches = list(re.finditer(r"(?<!enum)" + re.escape(marker), code))
    if len(matches) != 1:
        raise ValueError(f"Tiger color-space contract missing or ambiguous: {signature}")
    start = matches[0].end()
    depth = 1
    for index in range(start, len(code)):
        depth += (code[index] == "{") - (code[index] == "}")
        if not depth:
            return code[start:index]
    raise ValueError(f"Unclosed Tiger color-space contract: {signature}")


def _expect_body(code: str, signature: str, expected: str) -> None:
    if _body(code, signature) != _compact(expected):
        raise ValueError(f"Tiger color-space wire contract changed: {signature}")


def _verify_source(source_dir: Path) -> None:
    shared = source_dir / "Source/WebKit/Shared/tiger"
    coder = _compact((shared / "ArgumentCodersTigerCG.cpp").read_text())
    _expect_body(coder, "enum class WireName : uint8_t", "SRGB, LinearSRGB")
    _expect_body(coder, "static uint8_t wireName(const RetainPtr<CGColorSpaceRef>& colorSpace)", """
        if (colorSpace && CFEqual(colorSpace.get(), WebCore::ColorSpace::LinearSRGB().platformColorSpace()))
            return static_cast<uint8_t>(WireName::LinearSRGB);
        return static_cast<uint8_t>(WireName::SRGB);
    """)
    for encoder in ("Encoder", "StreamConnectionEncoder"):
        _expect_body(coder, f"void ArgumentCoder<RetainPtr<CGColorSpaceRef>>::encode({encoder}& encoder, const RetainPtr<CGColorSpaceRef>& colorSpace)", """
            encoder << wireName(colorSpace);
        """)
    _expect_body(coder, "std::optional<RetainPtr<CGColorSpaceRef>> ArgumentCoder<RetainPtr<CGColorSpaceRef>>::decode(Decoder& decoder)", """
        auto name = decoder.decode<uint8_t>();
        if (!name)
            return std::nullopt;
        switch (static_cast<WireName>(*name)) {
        case WireName::SRGB:
            return WebCore::ColorSpace::SRGB().serializableColorSpace();
        case WireName::LinearSRGB:
            return WebCore::ColorSpace::LinearSRGB().serializableColorSpace();
        }
        return std::nullopt;
    """)


def _verify_preprocessed(contract: str) -> bool:
    if not contract:
        raise ValueError("Full preprocessed serializer contract is required")
    code = _compact(contract)
    color_space = _body(code, "class ColorSpace")
    if "DisplayP3()" in color_space:
        raise ValueError("Tiger color-space equivalence requires Display P3 disabled")
    is_cg = "usingPlatformColorSpace=RetainPtr<CGColorSpaceRef>;" in code
    if is_cg:
        custom = _body(code, "template<> struct ArgumentCoder<RetainPtr<CGColorSpaceRef>>")
        expected = _compact("""
            static void encode(Encoder&, const RetainPtr<CGColorSpaceRef>&);
            static void encode(StreamConnectionEncoder&, const RetainPtr<CGColorSpaceRef>&);
            static std::optional<RetainPtr<CGColorSpaceRef>> decode(Decoder&);
        """)
        if custom != expected:
            raise ValueError("Tiger handwritten color-space coder is not selected")
    else:
        platform = _body(code, "class PlatformColorSpace")
        enum = _body(platform, "enum class Name : uint8_t").rstrip(",")
        if enum != "SRGB,LinearSRGB":
            raise ValueError("Effective PlatformColorSpace enum is not SRGB=0, LinearSRGB=1")
        for encoder in ("Encoder", "StreamConnectionEncoder"):
            _expect_body(code, f"void ArgumentCoder<WebCore::PlatformColorSpace>::encode({encoder}& encoder, const WebCore::PlatformColorSpace& instance)", """
                static_assert(std::is_same_v<std::remove_cvref_t<decltype(instance.get())>, WebCore::PlatformColorSpace::Name>);
                encoder << instance.get();
            """)
        _expect_body(code, "std::optional<WebCore::PlatformColorSpace> ArgumentCoder<WebCore::PlatformColorSpace>::decode(Decoder& decoder)", """
            auto get = decoder.decode<WebCore::PlatformColorSpace::Name>();
            if (!decoder.isValid()) [[unlikely]]
                return std::nullopt;
            return { WebCore::PlatformColorSpace { WTF::move(*get) } };
        """)
        # Enums must still encode/decode their underlying fixed-width integer.
        for fragment in (
            "encoder << std::to_underlying<T>(value);",
            "auto value = decoder.template decode<std::underlying_type_t<T>>();",
            "if (value && WTF::isValidEnum<T>(*value)) return static_cast<T>(*value);",
        ):
            if _compact(fragment) not in code:
                raise ValueError("Preprocessed enum argument coder has changed")
    return is_cg


def normalize_serializers(text: str, source_dir: Path, preprocessed_contract: str = "") -> str:
    """Return a schema with the verified Tiger color-space byte described once.

    Preserve every other member and type verbatim. Validate the source mapping on
    both builds, the selected implementation in each preprocessed translation unit,
    and the exact generated wrapper before replacing it. Never ignore a diff simply
    because its type name contains PlatformColorSpace.
    """
    _verify_source(Path(source_dir))
    is_cg = _verify_preprocessed(preprocessed_contract)
    lines = text.splitlines(keepends=True)
    wrapper = [i for i, line in enumerate(lines) if line.strip() == "== WebCore::PlatformColorSpace"]
    if len(wrapper) != (0 if is_cg else 1):
        raise ValueError("Decoded schema does not match selected color-space implementation")
    if wrapper:
        start = wrapper[0]
        end = start + 1
        while end < len(lines) and not lines[end].startswith(("== ", "#### ")):
            end += 1
        members = [_compact(line) for line in lines[start + 1:end] if line.strip()]
        if members not in (["autoget=decoder.decode<WebCore::PlatformColorSpace::Name>();"],
                           ["get:WebCore::PlatformColorSpace::Name"]):
            raise ValueError("PlatformColorSpace wrapper has unexpected decoded members")
        del lines[start:end]
    indices = [i for i, line in enumerate(lines) if line.strip() == "== WebCore::ColorSpace"]
    if len(indices) != 1:
        raise ValueError("Expected exactly one ColorSpace serializer")
    lines.insert(indices[0] + 1, "  " + CANONICAL_COLOR_SPACE + "\n")
    return "".join(lines)

"""Guard the V3 bridge's audited public-API source boundary, not ABI certification.

Real compiler/plugin/UI/import tests are also mandatory. This rejects accidentally
introducing private Qt headers into a version-adaptive bridge, or Qt objects into
the rendering engine. An arbitrary external DLL cannot opt in through this test.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / "src/shaders"
PUBLIC = {
    "ShaderPlugin.cpp": {"QQmlEngine", "QQmlExtensionPlugin", "QtQml/qqml.h"},
    "ShaderBridge.h": {"QObject", "QVariantMap", "QVariantList", "QQmlParserStatus"},
    "ShaderBridge.cpp": {"QCoreApplication", "QClipboard", "QGuiApplication", "QCryptographicHash", "QDir", "QFileInfo",
                         "QPlatformSurfaceEvent", "QPointer", "QSet", "QStandardPaths", "QThread", "QTimer", "QWindow"},
    # GUI-side native Display preferences; none of these Qt objects may cross
    # into the renderer or its platform callbacks. Introspection is public API.
    "NativeDisplayBinding.h": {"QObject", "QPointer", "QString"},
    "NativeDisplayBinding.cpp": {"QCoreApplication", "QMetaMethod", "QMetaType", "QThread"},
    "ShaderPreferences.h": {"QByteArray", "QString", "QVariantMap"},
    "ShaderPreferences.cpp": {"QDir", "QCryptographicHash", "QFile", "QFileInfo", "QJsonDocument", "QJsonObject", "QLockFile", "QMetaType", "QSaveFile"},
}
for path in sorted((*SHADERS.glob("*.cpp"), *SHADERS.glob("*.h"))):
    source = path.read_text()
    includes = set(re.findall(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', source, re.M))
    qt_headers = {name for name in includes if re.match(r"(?:Q[A-Z]|Qt[A-Z]|q[a-z0-9_]+\.h$)", name)}
    assert qt_headers == PUBLIC.get(path.name, set()), (path.name, qt_headers, PUBLIC.get(path.name, set()))
    if path.name not in PUBLIC:
        assert not any("ShaderBridge" in name or "ShaderPlugin" in name for name in includes), path
    assert not any("/private/" in name or name.endswith("_p.h") for name in includes), path
    assert not re.search(r"\b(?:QObjectPrivate|QWindowPrivate|QQuickWindowPrivate|QQmlEnginePrivate|qt_qmlDebugEnableDebugging)\b", source), path
cmake = (SHADERS / "CMakeLists.txt").read_text()
assert not re.search(r"::(?:Core|Gui|Qml|Quick)Private\b", cmake)
plugin = (SHADERS / "ShaderPlugin.cpp").read_text()
assert '"BSI_SHADERS_V3;QT=" QT_VERSION_STR' in plugin
assert "API=public-v1;END;" in plugin
assert "sizeof(qreal) == 8" in plugin
assert "QT_VERSION == BSI_SHADERS_EXPECT_QT_VERSION" in plugin
assert "target_compile_definitions(BSIShaders PRIVATE BSI_SHADERS_EXPECT_QT_VERSION=" in cmake
builder = (ROOT / "scripts/Build-ShaderBundle.ps1").read_text()
for argument in ("-DCMAKE_PREFIX_PATH=$prefix", "-DQT_HOST_PATH=$prefix", "-DQt6CoreTools_DIR=$prefix/", "-DBSI_SHADERS_EXPECT_QT_VERSION=$packedVersion"):
    assert argument in builder, argument
assert "new BsiShaders::ShaderBridge(engine)" in plugin
assert "set(bsi_qt_minimum 5.15.2)" in cmake and "set(bsi_qt_minimum 6.2.4)" in cmake
print("PASS: source-only V3 public Qt bridge boundary; renderer has no Qt headers, no private bridge headers, honest compiled version. Not runtime/game certification.")

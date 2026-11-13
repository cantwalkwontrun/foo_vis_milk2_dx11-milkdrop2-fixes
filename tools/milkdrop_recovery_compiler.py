"""Generate and fingerprint an isolated compiler using current MilkDrop preparation.

Only TestResults output is written. The executable compiles one raw shader per
process; the scanner owns process timeouts and supplies the installed include.fx.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import sys
import uuid

SCHEMA_VERSION = 1
INPUT_FILES = (
    "vis_milk2/plugin.cpp",
    "vis_milk2/shadercompat.h",
    "tools/milkdrop_recovery_compiler.py",
    "tools/Build-MilkDropRecoveryCompiler.ps1",
)

HEADER = r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <charconv>
#include <cmath>
#pragma comment(lib, "d3dcompiler.lib")
#define LINEFEED_CONTROL_CHAR '\1'
enum { SHADER_WARP, SHADER_COMP, SHADER_BLUR, SHADER_OTHER };
struct CConstantTable {};
const char* m_szShaderIncludeText;
size_t m_nShaderIncludeTextLen;
wchar_t m_szCurrentPresetFile[MAX_PATH] = L"recovery-audit";
wchar_t m_szLoadingPreset[MAX_PATH] = {};
const wchar_t* ShaderTypeName(int t) { return t == SHADER_WARP ? L"warp" : L"composite"; }
void AppendShaderDebugLog(const wchar_t* text) { fwprintf(stderr, L"%s\n", text); }
std::string Read(const wchar_t* path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open compiler input");
    const auto size = std::filesystem::file_size(path);
    if (size > 1024 * 1024) throw std::runtime_error("Compiler input exceeds 1 MiB guard");
    std::string text(std::istreambuf_iterator<char>(file), {});
    if (text.find('\0') != std::string::npos) throw std::runtime_error("NUL byte in compiler input");
    return text;
}
void Write(const std::filesystem::path& path, const void* data, size_t size) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file || !file.write(static_cast<const char*>(data), size))
        throw std::runtime_error("Cannot write compiler output");
    file.close();
    if (!file) throw std::runtime_error("Cannot finish compiler output");
}
'''

MAIN = r'''
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 6) {
            fputs("Usage: compile.exe INCLUDE RAW KIND PROFILE OUTPUT\nKIND=0 (warp) or 1 (composite); PROFILE=ps_4_0 or ps_5_0\n", stderr);
            return 2;
        }
        int kind;
        if (!wcscmp(argv[3], L"0")) kind = SHADER_WARP;
        else if (!wcscmp(argv[3], L"1")) kind = SHADER_COMP;
        else { fputs("Unsupported shader kind\n", stderr); return 2; }
        const char* profile;
        if (!wcscmp(argv[4], L"ps_4_0")) profile = "ps_4_0";
        else if (!wcscmp(argv[4], L"ps_5_0")) profile = "ps_5_0";
        else { fputs("Unsupported shader profile\n", stderr); return 2; }
        std::string include = Read(argv[1]);
        if (include.empty()) throw std::runtime_error("Empty shader include");
        StripComments(include.data());
        m_szShaderIncludeText = include.c_str();
        m_nShaderIncludeTextLen = strlen(include.c_str());
        std::string raw = Read(argv[2]), output;
        wcsncpy_s(m_szLoadingPreset, argv[2], _TRUNCATE);
        CConstantTable* table = nullptr;
        void* shader = nullptr;
        if (!Prepare(raw.c_str(), "PS", profile, &table, &shader, kind, false, output)) {
            fputs("Production shader preparation rejected input\n", stderr);
            return 3;
        }
        Write(std::filesystem::path(std::wstring(argv[5]) + L".hlsl"), output.data(), output.size());
        Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
        HRESULT hr = D3DCompile(output.data(), output.size(), nullptr, nullptr, nullptr,
            "PS", profile, D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY | D3DCOMPILE_SKIP_OPTIMIZATION,
            0, &code, &errors);
        if (errors) {
            auto size = errors->GetBufferSize();
            const char* message = static_cast<const char*>(errors->GetBufferPointer());
            if (size && !message[size - 1]) --size;
            fwrite(message, 1, size, stderr);
        }
        if (FAILED(hr)) return 1;
        Write(std::filesystem::path(argv[5]), code->GetBufferPointer(), code->GetBufferSize());
        return 0;
    } catch (const std::exception& error) {
        fprintf(stderr, "%s\n", error.what());
        return 4;
    }
}
'''


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def source_hashes(root: Path) -> dict[str, str]:
    return {name: digest((root / name).read_bytes()) for name in INPUT_FILES}


def section(source: str, start: str, end: str) -> str:
    if source.count(start) != 1 or source.count(end) != 1:
        raise RuntimeError(f"Production extraction anchors changed: {start!r} / {end!r}")
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


def generated_source(root: Path) -> bytes:
    source = (root / "vis_milk2/plugin.cpp").read_text(encoding="utf-8-sig")
    helpers = section(source, "static bool IsShaderIdentifierStart", "static int PresetShaderVersionToInternal")
    strip = section(source, "static void StripComments", "// This gets called only once, when your plugin is actually launched.")
    prep = section(source, "bool CPlugin::LoadShaderFromMemory", "    // Now really try to compile the shader.")
    prep = prep.replace("bool CPlugin::LoadShaderFromMemory", "bool Prepare", 1)
    signature = "const bool /*bHardErrors*/)"
    if prep.count(signature) != 1:
        raise RuntimeError("Production preparation signature changed")
    prep = prep.replace(signature, "const bool /*bHardErrors*/, std::string& output)", 1)
    prep += "    output = szShaderText; return true;\n}\n"
    compat = (root / "vis_milk2/shadercompat.h").read_text(encoding="utf-8-sig")
    return (HEADER + compat + helpers + strip + prep + MAIN).encode("utf-8")


def atomic(path: Path, data: bytes) -> None:
    temporary = path.with_name(path.name + ".tmp-" + uuid.uuid4().hex)
    try:
        with temporary.open("xb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def write_json(path: Path, value: dict) -> None:
    atomic(path, (json.dumps(value, indent=2) + "\n").encode("utf-8"))


def freshness(root: Path, out: Path) -> dict:
    result = {"fresh": False, "executable": str(out / "compile.exe"),
              "manifest": str(out / "compiler-manifest.json")}
    try:
        manifest = json.loads((out / "compiler-manifest.json").read_bytes())
        checks = {
            "schema": manifest["schema_version"] == SCHEMA_VERSION,
            "source_hashes": manifest["source_hashes"] == source_hashes(root),
            "generated_source": manifest["generated_source_sha256"] == digest(generated_source(root)),
            "source_on_disk": manifest["generated_source_sha256"] == digest((out / "compile.cpp").read_bytes()),
            "executable": manifest["compiler_exe_sha256"] == digest((out / "compile.exe").read_bytes()),
        }
        result["fresh"] = all(checks.values())
        result["reason"] = "current" if result["fresh"] else ", ".join(name for name, ok in checks.items() if not ok)
    except (OSError, KeyError, ValueError, TypeError, RuntimeError) as error:
        result["reason"] = str(error)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--check", action="store_true")
    action.add_argument("--generate", action="store_true")
    action.add_argument("--record-build", action="store_true")
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--toolchain", default="")
    args = parser.parse_args()
    root = args.repo_root.resolve()
    out = (args.output_dir or root / "TestResults/recovery-compiler").resolve()
    if not out.is_relative_to((root / "TestResults").resolve()):
        raise RuntimeError("Compiler output must remain within this repository's TestResults directory")
    if args.check:
        result = freshness(root, out)
        print(json.dumps(result))
        return 0 if result["fresh"] else 3
    source = generated_source(root)
    hashes = source_hashes(root)
    out.mkdir(parents=True, exist_ok=True)
    inputs = {"schema_version": SCHEMA_VERSION, "source_hashes": hashes,
              "generated_source_sha256": digest(source)}
    if args.generate:
        atomic(out / "compile.cpp", source)
        write_json(out / "build-inputs.json", inputs)
        print(json.dumps({"generated_source": str(out / "compile.cpp"), **inputs}))
        return 0
    recorded = json.loads((out / "build-inputs.json").read_bytes())
    if recorded != inputs or (out / "compile.cpp").read_bytes() != source:
        raise RuntimeError("Build sources changed during compilation; rebuild before recording freshness")
    executable = (out / "compile.exe").read_bytes()
    if len(executable) < 1024 or executable[:2] != b"MZ":
        raise RuntimeError("Compiled output is not a Windows executable")
    manifest = {**inputs, "built_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "repo_root": str(root), "compiler_exe": str(out / "compile.exe"),
                "compiler_exe_sha256": digest(executable), "toolchain": args.toolchain,
                "architecture": "x64", "supported_profiles": ["ps_4_0", "ps_5_0"],
                "compiler_flags": ["D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY", "D3DCOMPILE_SKIP_OPTIMIZATION"],
                "input_include": "Supplied per compiler invocation; scanner must record its hash",
                "timeout": "Caller must bound each isolated compiler process"}
    write_json(out / "compiler-manifest.json", manifest)
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"Recovery compiler setup failed: {error}", file=sys.stderr)
        sys.exit(1)

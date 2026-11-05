"""从单一方法目录生成 C++ 和 TypeScript 常量；--check 用于检查提交内容。"""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def generate() -> dict[Path, str]:
    contract = json.loads((ROOT / "contracts/runtime-api.json").read_text(encoding="utf-8"))
    methods = contract["methods"]
    names = [method["name"] for method in methods]
    if len(set(names)) != len(names):
        raise ValueError("方法名称重复")
    cpp_entries = "\n".join(
        '  {"%s", "%s", %s, %d},' % (
            method["name"], method["capability"], str(method["managerOnly"]).lower(), method["timeoutMs"]
        ) for method in methods
    )
    header = f'''/** @file method_catalog.h 由 scripts/generate-contracts.py 生成，请修改契约源文件。 */
#ifndef ALKAID_METHOD_CATALOG_H
#define ALKAID_METHOD_CATALOG_H
#include <array>
#include <cstddef>
#include <string_view>
namespace alkaid {{
struct MethodContract {{
  std::string_view name;
  std::string_view capability;
  bool managerOnly;
  int timeoutMs;
}};
inline constexpr int PROTOCOL_VERSION = {contract["protocolVersion"]};
inline constexpr std::size_t MAX_PAYLOAD_BYTES = {contract["maxPayloadBytes"]};
inline constexpr std::size_t MAX_PENDING_PER_SESSION = {contract["maxPendingPerSession"]};
inline constexpr std::array<MethodContract, {len(methods)}> METHOD_CATALOG = {{{{
{cpp_entries}
}}}};
}} // namespace alkaid
#endif // ALKAID_METHOD_CATALOG_H
'''
    ts = '// 由 scripts/generate-contracts.py 生成，请修改契约源文件。\n'
    ts += 'export const contract = ' + json.dumps(contract, ensure_ascii=False, indent=2) + ' as const;\n'
    ts += 'export type MethodName = typeof contract.methods[number]["name"];\n'
    return {
        ROOT / "core/rpc/method_catalog.h": header,
        ROOT / "packages/sdk/src/method_catalog.ts": ts,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    for path, content in generate().items():
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != content:
                print(f"契约生成文件未同步：{path.relative_to(ROOT)}")
                return 1
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8", newline="\n")
    print("契约生成文件检查通过" if args.check else "契约生成完成")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

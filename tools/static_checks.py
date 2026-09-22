"""静态一致性检查：无需硬件即可发现模板/路由/面板/ID 之间的不同步问题。

用法: python tools/static_checks.py
全部通过输出 OK 并退出码 0；发现问题逐条列出并退出码 1。
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "code"
FAILS = []


def fail(msg):
    FAILS.append(msg)
    print(f"  [FAIL] {msg}")


def ok(msg):
    print(f"  [ ok ] {msg}")


def read(name):
    return (ROOT / name).read_text(encoding="utf-8")


def check_utf8_and_braces():
    print("[1] 编码与括号配平")
    for f in ROOT.glob("*.[ch]"):
        try:
            t = f.read_text(encoding="utf-8")
        except UnicodeDecodeError as e:
            fail(f"{f.name} 不是合法 UTF-8: {e}")
            continue
        if t.count("{") != t.count("}"):
            fail(f"{f.name} 花括号不配平: {{={t.count('{')} }}={t.count('}')}")
        else:
            ok(f"{f.name} {{}} 配平")
    ino = ROOT / "code.ino"
    t = ino.read_text(encoding="utf-8")
    if t.count("{") != t.count("}"):
        fail(f"code.ino 花括号不配平")
    else:
        ok("code.ino {} 配平")


def check_placeholders():
    print("[2] 模板占位符与 handleRoot 变量表一致")
    html = read("web_html.cpp")
    handlers = read("web_handlers.cpp")
    m = re.search(r'R"rawliteral\((.*)\)rawliteral"', html, re.S)
    if not m:
        fail("web_html.cpp 中找不到 htmlPage rawliteral")
        return
    page = m.group(1)
    tpl_keys = set(re.findall(r"%([A-Z][A-Z_]{0,23})%", page))
    # handleRoot 的 PageVar 数组
    hv = re.search(r"PageVar vars\[\]\s*=\s*\{(.*?)\};", handlers, re.S)
    if not hv:
        fail("web_handlers.cpp 中找不到 PageVar vars[]")
        return
    var_keys = set(re.findall(r'\{"([A-Z_]+)"', hv.group(1)))
    missing = tpl_keys - var_keys
    unused = var_keys - tpl_keys
    if missing:
        fail(f"模板占位符在 handleRoot 变量表中缺失: {sorted(missing)}")
    elif unused:
        fail(f"handleRoot 提供了模板未使用的变量: {sorted(unused)}")
    else:
        ok(f"{len(var_keys)} 个占位符一一对应")


def check_panels():
    print("[3] 侧边栏面板链接与面板 DIV 一一对应")
    html = read("web_html.cpp")
    m = re.search(r'R"rawliteral\((.*)\)rawliteral"', html, re.S)
    page = m.group(1)
    nav_panels = set(re.findall(r'data-panel="([a-z]+)"', page))
    div_panels = set(re.findall(r'id="panel-([a-z]+)"', page))
    if nav_panels != div_panels:
        fail(f"侧边栏 {sorted(nav_panels)} 与面板 {sorted(div_panels)} 不一致")
    else:
        ok(f"{len(div_panels)} 个面板均有导航入口")


def check_routes():
    print("[4] 路由注册与处理器声明一致")
    ino = read("code.ino")
    hh = read("web_handlers.h")
    routes = re.findall(r'server\.on\("([^"]+)"(?:,\s*HTTP_POST)?,\s*(\w+)\)', ino)
    declared = set(re.findall(r"void (handle\w+)\(\)", hh))
    for path, fn in routes:
        if fn not in declared and fn != "handleRoot":
            fail(f"路由 {path} 的处理函数 {fn} 未在 web_handlers.h 声明")
    if not FAILS:
        ok(f"{len(routes)} 条路由，处理函数均有声明")


def check_js_ids():
    print("[5] JS 引用的元素 ID 在页面中存在（静态 ID）")
    html = read("web_html.cpp")
    handlers = read("web_handlers.cpp")
    m = re.search(r'R"rawliteral\((.*)\)rawliteral"', html, re.S)
    page = m.group(1)
    ids = set(re.findall(r'id="([A-Za-z0-9_-]+)"', page))
    refs = set(re.findall(r"getElementById\('([A-Za-z0-9_-]+)'\)", page))
    # 动态拼接的 ID（'testBtn'+i 等）由 handleRoot 的 channelsHtml 服务端生成，一并纳入
    gen_ids = set(re.findall(r'id=\\?"([A-Za-z0-9_-]+)" ?\+ ?idx', handlers))
    # 动态前缀：'channel'+i → 需存在静态实例或服务端生成
    dyn_prefixes = set(re.findall(r"getElementById\('([A-Za-z]+)' *\+ *i\)", page))
    missing = refs - ids
    if missing:
        fail(f"JS 引用但页面不存在的 ID: {sorted(missing)}")
    for p in sorted(dyn_prefixes):
        has_static = any(i.startswith(p) for i in ids | gen_ids)
        if not has_static:
            fail(f"JS 动态前缀 '{p}+' 在页面/服务端生成 HTML 中均无实例")
    if not any(f"[FAIL]" in f for f in FAILS[-3:]):
        ok(f"{len(refs)} 个静态 ID 引用 + {len(dyn_prefixes)} 个动态前缀全部有效")


def check_push_types():
    print("[6] 推送类型表（固件/前端 JS）数量一致")
    push = read("push.cpp")
    html = read("web_html.cpp")
    m = re.search(r'R"rawliteral\((.*)\)rawliteral"', html, re.S)
    js = m.group(1)
    hint_fn = js[js.find("function updateTypeHint"):]
    hint_fn = hint_fn[:hint_fn.find("\n    function")]
    js_types = set(re.findall(r"type == (\d+)", hint_fn))
    fw_types = re.findall(r'\{PUSH_TYPE_\w+,\s*"[^"]+",', push)
    if len(fw_types) != 12:
        fail(f"固件 PUSH_DESCS 描述符表应为 12 项，实际 {len(fw_types)}")
    if len(js_types) != 12:
        fail(f"前端 updateTypeHint 分支应为 12 种类型，实际 {len(js_types)}: {sorted(js_types)}")
    if not FAILS:
        ok("固件与前端各覆盖 12 种推送类型")


def check_json_escape_usage():
    print("[7] 关键 JSON 输出都经过 jsonEscape")
    handlers = read("web_handlers.cpp")
    for bad in re.findall(r'"message\\?":\\?"?" ?\+[^;]+(?<!jsonEscape\([^)]+\));', handlers):
        pass  # 粗查不易精确，跳过复杂场景
    ok("（人工审查项，脚本仅提醒）新增 JSON 字段须用 jsonEscape 包装动态内容")


def main():
    check_utf8_and_braces()
    check_placeholders()
    check_panels()
    check_routes()
    check_js_ids()
    check_push_types()
    print()
    if FAILS:
        print(f"共 {len(FAILS)} 个问题")
        sys.exit(1)
    print("全部通过 ✔")


if __name__ == "__main__":
    main()

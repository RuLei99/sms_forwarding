"""预览服务器冒烟测试：无硬件验证 Web UI 渲染与模拟接口行为。

用法: python tools/smoke_test.py
自起一个独立预览实例（端口 8099），跑完自动退出码 0/1。
"""
import json
import re
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

PORT = 8099
BASE = f"http://127.0.0.1:{PORT}"
ROOT = Path(__file__).resolve().parent.parent
FAILS = []


def fail(msg):
    FAILS.append(msg)
    print(f"  [FAIL] {msg}")


def ok(msg):
    print(f"  [ ok ] {msg}")


def get(path):
    with urllib.request.urlopen(BASE + path, timeout=5) as r:
        return r.status, r.read().decode("utf-8")


def main():
    proc = subprocess.Popen(
        [sys.executable, str(ROOT / "tools" / "preview_server.py"), str(PORT)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        page = None
        for _ in range(20):  # 等服务器就绪
            try:
                st, page = get("/")
                break
            except Exception:
                time.sleep(0.3)
        if page is None:
            print("预览服务器未能启动")
            sys.exit(1)

        print("[1] 主页渲染")
        checks = [
            ("模组信息卡片", "模组信息" in page),
            ("6 项模组信息字段", all(f'ov{i}' in page for i in
                ["Signal", "Operator", "Imei", "Iccid", "Model", "Fw"])),
            ("11 个面板", len(re.findall(r'class="panel[^"]*" id="panel-', page)) == 11),
            ("模组诊断面板已移除", "panel-diagnose" not in page),
            ("5 个推送通道", len(re.findall(r'class="push-channel[ "]', page)) == 5),
            ("通道生成含测试按钮", page.count("testPush(") >= 6),
            ("无未替换占位符", not re.search(r"%[A-Z][A-Z_]+%", page)),
            ("登录账号填充", 'name="webUser" value="admin"' in page),
            ("通道健康卡片", "chHealthTable" in page),
            ("主/备 WiFi 表单", 'name="wifi1Ssid"' in page and 'name="wifi2Ssid"' in page),
            ("手机端更多面板", "navSheet" in page and "nav-more" in page),
            ("SIM PIN 表单", 'name="simPin"' in page),
            ("配置备份卡", "cfgImportFile" in page),
            ("CSV 导出按钮", "/recordsexport" in page),
            ("ntfy/MQTT 前端提示", "type == 11" in page and "type == 12" in page),
        ]
        for name, cond in checks:
            (ok if cond else fail)(name)

        print("[2] /status 接口")
        st, body = get("/status")
        d = json.loads(body)
        need = ["ip", "ssid", "heap", "uptime", "modem", "signal",
                "operator", "imei", "iccid", "model", "fw",
                "email", "push", "channels"]
        miss = [k for k in need if k not in d]
        (ok if not miss else fail)("15 个字段齐全" if not miss else f"缺字段: {miss}")
        chans = d.get("channels", [])
        (ok if isinstance(chans, list) and len(chans) == 5
         else fail)("通道健康数组 5 项")

        print("[3] /smslog 接口")
        st, body = get("/smslog")
        d = json.loads(body)
        (ok if d.get("total", 0) >= 1 and "items" in d else fail)("记录结构与数据正常")

        print("[4] /log 接口")
        st, body = get("/log")
        d = json.loads(body)
        (ok if isinstance(d, list) and len(d) > 0 else fail)("日志数组正常")

        print("[5] 兼容路由与新端点")
        for p in ("/tools", "/sms"):
            st, _ = get(p)
            (ok if st == 200 else fail)(f"{p} → {st}")

        print("[6] 任务队列 / 配置导出 / CSV 导出（模拟）")
        st, body = get("/job?id=1")
        d = json.loads(body)
        (ok if d.get("state") == "done" else fail)("/job 返回 done")
        st, body = get("/config/export")
        d = json.loads(body)
        (ok if d.get("smtpServer") else fail)("/config/export JSON 结构正常")
        st, body = get("/recordsexport")
        (ok if body.startswith("sender,") or "sender" in body[:30] else fail)("/recordsexport CSV 正常")
    finally:
        proc.terminate()

    print()
    if FAILS:
        print(f"共 {len(FAILS)} 个问题")
        sys.exit(1)
    print("冒烟测试全部通过 ✔")


if __name__ == "__main__":
    main()

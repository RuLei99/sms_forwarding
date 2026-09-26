"""本地预览 Web UI：从 web_html.cpp 提取 htmlPage 模板，填充示例值并模拟设备接口。

用法: python tools/preview_server.py [端口]   (默认 8080)
仅用于浏览器查看页面，不连接真实模组。
"""
import json
import re
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs

SRC = Path(__file__).resolve().parent.parent / "code" / "web_html.cpp"

SAMPLE_VARS = {
    "IP": "192.168.1.233",
    "WIFI_SSID": "Home-5G",
    "FREE_HEAP": "187 KB",
    "UPTIME": "3:42:17",
    "MODEM_CHECK": "已就绪",
    "SMTP_CHECK": "已配置",
    "PUSH_COUNT": "2",
    "ADMIN_PHONE": "13800138000",
    "WEB_USER": "admin",
    "WEB_PASS": "admin123",
    "SMTP_SERVER": "smtp.qq.com",
    "SMTP_PORT": "465",
    "SMTP_USER": "me@qq.com",
    "SMTP_PASS": "demo-auth-code",
    "SMTP_SEND_TO": "receiver@example.com",
    "NUMBER_BLACK_LIST": "1069xxxxxxxx\n10086",
    "FILTER_KEYWORDS": "退订\n回T",
    "FLT_WL_SEL": "",
    "FLT_BL_SEL": " selected",
    "TZ_HOURS": "8",
    "SIM_PIN": "0000",
    "WIFI1_SSID": "Home-5G",
    "WIFI1_PASS": "main-pass-1234",
    "WIFI2_SSID": "Home-2.4G-Backup",
    "WIFI2_PASS": "backup-pass",
}

# 模拟已保存的推送通道配置（与 config.pushChannels 对应）
SAMPLE_CHANNELS = [
    {"enabled": True,  "name": "Bark 推送",   "type": 2,  "url": "https://api.day.app/AbCdEf123Key", "key1": "", "key2": "", "custom": ""},
    {"enabled": True,  "name": "钉钉机器人",  "type": 4,  "url": "https://oapi.dingtalk.com/robot/send", "key1": "SEC9f8e7d6c5b4a3f2e1d", "key2": "", "custom": ""},
    {"enabled": False, "name": "",            "type": 10, "url": "https://api.telegram.org", "key1": "123456789", "key2": "123456789:AAH-example-token", "custom": ""},
    {"enabled": False, "name": "",            "type": 5,  "url": "", "key1": "pushplus-demo-token", "key2": "wechat", "custom": ""},
    {"enabled": True,  "name": "ntfy 通知",   "type": 11, "url": "https://ntfy.sh", "key1": "my-sms-topic", "key2": "", "custom": ""},
]


def build_channels_html():
    """复刻 web_handlers.cpp handleRoot 中 %PUSH_CHANNELS% 的生成逻辑。"""
    type_names = {
        1: "POST JSON（通用格式）", 2: "Bark（iOS推送）", 3: "GET请求（参数在URL中）",
        4: "钉钉机器人", 5: "PushPlus", 6: "Server酱", 7: "自定义模板",
        8: "飞书机器人", 9: "Gotify", 10: "Telegram Bot",
    }
    html = ""
    for i, ch in enumerate(SAMPLE_CHANNELS):
        idx = str(i)
        enabled_class = " enabled" if ch["enabled"] else ""
        checked = " checked" if ch["enabled"] else ""
        options = "".join(
            '<option value="%d"%s>%s</option>' % (t, " selected" if ch["type"] == t else "", n)
            for t, n in type_names.items()
        )
        html += (
            '<div class="push-channel%s" id="channel%s">' % (enabled_class, idx)
            + '<div class="push-channel-header">'
            + '<input type="checkbox" name="push%sen" id="push%sen" onchange="toggleChannel(%s)"%s>' % (idx, idx, idx, checked)
            + '<label for="push%sen" class="label-inline">启用推送通道 %d</label>' % (idx, i + 1)
            + '<button type="button" class="btn btn-sm btn-secondary ch-test" onclick="testPush(%s)" id="testBtn%s">发送测试</button>' % (idx, idx)
            + '</div><div class="push-channel-body">'
            + '<div class="form-group"><label>通道名称</label>'
            + '<input type="text" name="push%sname" value="%s" placeholder="自定义名称"></div>' % (idx, ch["name"])
            + '<div class="form-group"><label>推送方式</label>'
            + '<select name="push%stype" id="push%stype" onchange="updateTypeHint(%s)">%s</select>' % (idx, idx, idx, options)
            + '<div class="push-type-hint" id="hint%s"></div></div>' % idx
            + '<div class="form-group"><label>推送URL/Webhook</label>'
            + '<input type="text" name="push%surl" id="url%s" value="%s" placeholder="http://your-server.com/api 或 webhook地址"></div>' % (idx, idx, ch["url"])
            + '<div id="extra%s" style="display:none;">' % idx
            + '<div class="form-group"><label id="key1label%s">参数1</label>' % idx
            + '<input type="text" name="push%skey1" id="key1%s" value="%s"></div>' % (idx, idx, ch["key1"])
            + '<div class="form-group" id="key2group%s"><label id="key2label%s">参数2</label>' % (idx, idx)
            + '<input type="text" name="push%skey2" id="key2%s" value="%s"></div>' % (idx, idx, ch["key2"])
            + '</div>'
            + '<div id="custom%s" style="display:none;">' % idx
            + '<div class="form-group"><label>请求体模板（使用 {sender} {message} {timestamp} 占位符）</label>'
            + '<textarea name="push%sbody" rows="4" style="width:100%%;font-family:monospace;">%s</textarea></div>' % (idx, ch["custom"])
            + '</div></div></div>'
        )
    return html


STATUS = {
    "ip": "192.168.1.233",
    "ssid": "Home-5G",
    "heap": 187,
    "uptime": "3:42:17",
    "modem": True,
    "signal": "-71 dBm",
    "operator": "中国移动",
    "imei": "864123050012345",
    "iccid": "89860123456789012345",
    "model": "ML307R",
    "fw": "ML307RAR01A07",
    "email": True,
    "push": 3,
    "channels": [
        {"i": 0, "ok": 12, "fail": 0, "blocked": 0, "cool": False},
        {"i": 1, "ok": 8, "fail": 2, "blocked": 0, "cool": False},
        {"i": 2, "ok": 0, "fail": 0, "blocked": 0, "cool": False},
        {"i": 3, "ok": 0, "fail": 0, "blocked": 0, "cool": False},
        {"i": 4, "ok": 3, "fail": 5, "blocked": 4, "cool": True},
    ],
}

SMSLOG = {
    "total": 2,
    "items": [
        {"s": "10086", "t": "您本月已使用流量 3.2GB，剩余 6.8GB，详询 10086。", "ts": "2026-09-22 10:21:05", "e": 1, "p": 2, "en": 3},
        {"s": "106912345678", "t": "【某平台】您的验证码是 882914，10 分钟内有效。", "ts": "2026-09-22 09:47:33", "e": 1, "p": 2, "en": 3},
    ],
}

LOGS = [
    "[12:01:03] WiFi 已连接，IP: 192.168.1.233",
    "[12:01:05] 模组初始化完成，信号 -71 dBm",
    "[10:21:05] 收到短信来自 10086，已转发（邮件+推送）",
]


def render_html() -> str:
    text = SRC.read_text(encoding="utf-8")
    m = re.search(r'R"rawliteral\((.*)\)rawliteral"', text, re.S)
    html = m.group(1)
    vars = dict(SAMPLE_VARS)
    vars["PUSH_CHANNELS"] = build_channels_html()
    for key, val in vars.items():
        html = html.replace("%" + key + "%", val)
    html = re.sub(r"%[A-Z_]+%", "", html)  # 兜底清掉未模拟的占位符
    return html


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, ctype, body):
        data = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype + "; charset=utf-8")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        u = urlparse(self.path)
        if u.path in ("/", "/tools", "/sms"):
            self._send(200, "text/html", PAGE)
        elif u.path == "/status":
            self._send(200, "application/json", json.dumps(STATUS, ensure_ascii=False))
        elif u.path == "/smslog":
            self._send(200, "application/json", json.dumps(SMSLOG, ensure_ascii=False))
        elif u.path == "/log":
            self._send(200, "application/json", json.dumps(LOGS, ensure_ascii=False))
        elif u.path == "/job":
            state = urlparse_qs = parse_qs(u.query)
            jid = int(state.get("id", ["0"])[0]) if state.get("id", ["0"])[0].isdigit() else 0
            self._send(200, "application/json", json.dumps(
                {"state": "done", "success": True,
                 "message": "[预览模式] 任务 %s 已完成（模拟）" % jid},
                ensure_ascii=False))
        elif u.path == "/config/export":
            self._send(200, "application/json", json.dumps({
                "smtpServer": "smtp.qq.com", "smtpPort": 465, "smtpUser": "me@qq.com",
                "smtpSendTo": "receiver@example.com", "adminPhone": "13800138000",
                "webUser": "admin",
                "wifi2Ssid": "Home-2.4G-Backup",
                "channels": [{"enabled": True, "type": 2, "name": "Bark 推送", "url": "https://api.day.app/x"}]
            }, ensure_ascii=False))
        elif u.path == "/recordsexport":
            self._send(200, "text/csv",
                       "sender,timestamp,text,email_ok\n10086,2026-09-23 10:00:00,流量提醒,1\n")
        else:
            self._send(200, "application/json", json.dumps(
                {"success": True, "message": "[预览模式] 模拟响应：" + u.path},
                ensure_ascii=False))

    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        self.rfile.read(n)
        self._send(200, "application/json", json.dumps(
            {"queued": True, "id": 1, "success": True, "message": "[预览模式] 已加入任务队列"},
            ensure_ascii=False))

    def log_message(self, *a):
        pass


PAGE = render_html()

if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    print(f"预览: http://127.0.0.1:{port}/")
    HTTPServer(("127.0.0.1", port), Handler).serve_forever()

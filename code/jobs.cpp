#include "jobs.h"
#include "web_handlers.h"
#include "modem.h"
#include <esp_task_wdt.h>

// ---- 单槽任务队列 ----
// 模组是串行设备，同一时刻只可能执行一个串口任务；排队溢出直接拒绝（调用方提示稍后再试）
static ModemJob s_job;
static ModemResult s_result;
static volatile bool s_queued = false;
static volatile bool s_running = false;
static uint32_t s_nextId = 1;

uint32_t jobsSubmit(ModemJobType type, const char* arg1, const char* arg2) {
  if (s_queued || s_running) return 0;
  memset(&s_job, 0, sizeof(s_job));
  s_job.id = s_nextId++;
  s_job.type = type;
  if (arg1) snprintf(s_job.arg1, sizeof(s_job.arg1), "%s", arg1);
  if (arg2) snprintf(s_job.arg2, sizeof(s_job.arg2), "%s", arg2);
  s_queued = true;
  return s_job.id;
}

int jobsQuery(uint32_t id, ModemResult& out) {
  if (id == 0) return 3;
  if (s_queued && s_job.id == id) return 0;
  if (s_running && s_job.id == id) return 1;
  if (!s_running && !s_queued && s_result.id == id) {
    out = s_result;
    return 2;
  }
  return 3;
}

bool jobsIdle() {
  return !s_queued && !s_running;
}

// ---- Ping 执行体（自 web_handlers 迁移；调用方必须已持有串口占用权） ----
static bool runPingJob(String& message) {
  logCaptureLn(String("执行 Ping 任务"));

  while (Serial1.available()) Serial1.read();
  logCaptureLn(String("激活数据连接(CGACT)..."));
  String activateResp = sendATCommand("AT+CGACT=1,1", 10000);
  logCaptureLn(String("CGACT响应: " + activateResp));
  if (activateResp.indexOf("OK") < 0) {
    logCaptureLn(String("数据连接激活失败，尝试继续执行..."));
  }

  while (Serial1.available()) Serial1.read();
  delay(500);  // 等待网络稳定

  Serial1.println("AT+MPING=\"8.8.8.8\",30,1");

  unsigned long start = millis();
  String resp = "";
  bool gotError = false;
  bool gotPingResult = false;
  String pingResultMsg = "";

  // 等待最多35秒（30秒超时 + 5秒余量）
  while (millis() - start < 35000) {
    esp_task_wdt_reset();
    while (Serial1.available()) {
      char c = Serial1.read();
      if (resp.length() < 4096) resp += c;  // 防 String 无限增长

      if (resp.indexOf("+CME ERROR") >= 0 || resp.indexOf("ERROR") >= 0) {
        gotError = true;
        pingResultMsg = "模组返回错误";
        break;
      }

      int mpingIdx = resp.indexOf("+MPING:");
      if (mpingIdx >= 0) {
        int lineEnd = resp.indexOf('\n', mpingIdx);
        if (lineEnd >= 0) {
          String mpingLine = resp.substring(mpingIdx, lineEnd);
          mpingLine.trim();
          logCaptureLn(String("收到MPING结果: " + mpingLine));

          int colonIdx = mpingLine.indexOf(':');
          if (colonIdx >= 0) {
            String params = mpingLine.substring(colonIdx + 1);
            params.trim();
            int commaIdx = params.indexOf(',');
            String resultStr = commaIdx >= 0 ? params.substring(0, commaIdx) : params;
            resultStr.trim();
            int result = resultStr.toInt();
            gotPingResult = true;

            bool pingSuccess = (result == 0 || result == 1) ||
                               (params.indexOf(',') >= 0 && params.length() > 5);
            if (pingSuccess) {
              int idx1 = params.indexOf(',');
              if (idx1 >= 0) {
                String rest = params.substring(idx1 + 1);
                String ip;
                int idx2;
                if (rest.startsWith("\"")) {
                  int quoteEnd = rest.indexOf('\"', 1);
                  if (quoteEnd >= 0) {
                    ip = rest.substring(1, quoteEnd);
                    idx2 = rest.indexOf(',', quoteEnd);
                  } else {
                    idx2 = rest.indexOf(',');
                    ip = rest.substring(0, idx2);
                  }
                } else {
                  idx2 = rest.indexOf(',');
                  ip = rest.substring(0, idx2);
                }
                if (idx2 >= 0) {
                  rest = rest.substring(idx2 + 1);
                  int idx3 = rest.indexOf(',');  // packet_len后
                  if (idx3 >= 0) {
                    rest = rest.substring(idx3 + 1);
                    int idx4 = rest.indexOf(',');  // time后
                    String timeStr, ttlStr;
                    if (idx4 >= 0) {
                      timeStr = rest.substring(0, idx4);
                      ttlStr = rest.substring(idx4 + 1);
                    } else {
                      timeStr = rest;
                      ttlStr = "N/A";
                    }
                    timeStr.trim();
                    ttlStr.trim();
                    pingResultMsg = "目标: " + ip + ", 延迟: " + timeStr + "ms, TTL: " + ttlStr;
                  }
                }
              }
              if (pingResultMsg.length() == 0) pingResultMsg = "Ping成功";
            } else {
              pingResultMsg = "Ping超时或目标不可达 (错误码: " + String(result) + ")";
            }
            break;
          }
        }
      }
    }
    if (gotError || gotPingResult) break;
    server.handleClient();
  }

  logCaptureLn(String("Ping任务完成，关闭PDP上下文..."));
  sendATCommand("AT+CGACT=0,1", 5000);

  if (gotPingResult && pingResultMsg.indexOf("延迟") >= 0) {
    message = pingResultMsg;
    return true;
  }
  if (gotError || gotPingResult) {
    message = pingResultMsg;
    return false;
  }
  message = "操作超时，未收到Ping结果";
  return false;
}

// ---- 任务执行 ----
void jobsRun() {
  if (!s_queued || s_running) return;
  s_running = true;
  s_queued = false;

  memset(&s_result, 0, sizeof(s_result));
  s_result.id = s_job.id;

  // 执行期间持有串口占用权：其他 Web 模组请求（flight/datalock 等）会被拒
  modemPortBusy = true;

  switch (s_job.type) {
    case MODEM_JOB_AT: {
      logCaptureLn(String("执行 AT 任务: " + String(s_job.arg1)));
      String resp = sendATCommand(s_job.arg1, 5000);
      s_result.success = resp.length() > 0;
      snprintf(s_result.message, sizeof(s_result.message), "%s",
               resp.length() > 0 ? resp.c_str() : "超时或无响应");
      break;
    }
    case MODEM_JOB_SEND_SMS: {
      s_result.success = sendSMS(s_job.arg1, s_job.arg2);
      snprintf(s_result.message, sizeof(s_result.message), "%s",
               s_result.success ? "短信发送成功" : "短信发送失败，请检查模组状态");
      break;
    }
    case MODEM_JOB_PING: {
      String msg;
      s_result.success = runPingJob(msg);
      snprintf(s_result.message, sizeof(s_result.message), "%s", msg.c_str());
      break;
    }
    case MODEM_JOB_SOFT_RESET: {
      logCaptureLn(String("执行模组软重启任务"));
      String resp = sendATCommand("AT+CFUN=1,1", 15000);
      bool ok = resp.indexOf("OK") >= 0;
      if (ok) modemInit();  // 内部自行管理 modemPortBusy
      s_result.success = ok;
      snprintf(s_result.message, sizeof(s_result.message), "%s",
               ok ? "模组软重启成功" : ("软重启失败: " + resp).c_str());
      break;
    }
    case MODEM_JOB_HARD_RESET: {
      logCaptureLn(String("执行模组硬重启任务"));
      resetModule();  // 内部自行管理 modemPortBusy
      s_result.success = modemReady;
      snprintf(s_result.message, sizeof(s_result.message), "%s",
               modemReady ? "模组硬重启完成" : "模组硬重启后仍未就绪，后台将继续重试");
      break;
    }
    default:
      s_result.success = false;
      snprintf(s_result.message, sizeof(s_result.message), "未知任务类型");
      break;
  }

  modemPortBusy = false;
  s_running = false;
  logCaptureF("[任务%u] 完成: %s\n", s_job.id, s_result.message);
}

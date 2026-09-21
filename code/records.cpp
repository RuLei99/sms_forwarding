#include "records.h"
#include "push.h"  // jsonEscape

static SmsRecord recs[SMS_RECORD_MAX];
static int recHead = 0;   // 下一个写入位置
static int recCount = 0;

void recordsAdd(const char* sender, const char* text, const char* timestamp,
                bool emailOk, uint8_t pushMask, uint8_t pushEnabled) {
  SmsRecord& r = recs[recHead];
  r.valid = true;
  r.sender = sender;
  r.text = text;
  // PDU 网络时间戳为空时用设备运行时长兜底，至少能看出先后顺序
  if (timestamp == nullptr || timestamp[0] == 0) {
    long up = millis() / 1000;
    char buf[16];
    snprintf(buf, sizeof(buf), "+%ld:%02ld:%02ld", up / 3600, (up % 3600) / 60, up % 60);
    r.timestamp = buf;
  } else {
    r.timestamp = timestamp;
  }
  r.emailOk = emailOk;
  r.pushMask = pushMask;
  r.pushEnabled = pushEnabled;
  recHead = (recHead + 1) % SMS_RECORD_MAX;
  if (recCount < SMS_RECORD_MAX) recCount++;
}

int recordsCount() {
  return recCount;
}

void recordsClear() {
  for (int i = 0; i < SMS_RECORD_MAX; i++) {
    recs[i].valid = false;
    recs[i].sender = "";
    recs[i].text = "";
    recs[i].timestamp = "";
  }
  recHead = 0;
  recCount = 0;
}

String recordsJson() {
  String json = "{\"total\":" + String(recCount) + ",\"items\":[";
  // 最新在前：从最近写入的一条倒序遍历
  for (int k = 0; k < recCount; k++) {
    int idx = (recHead - 1 - k + SMS_RECORD_MAX) % SMS_RECORD_MAX;
    if (!recs[idx].valid) continue;
    if (k > 0) json += ",";
    json += "{\"s\":\"" + jsonEscape(recs[idx].sender) + "\",";
    json += "\"t\":\"" + jsonEscape(recs[idx].text) + "\",";
    json += "\"ts\":\"" + jsonEscape(recs[idx].timestamp) + "\",";
    json += "\"e\":" + String(recs[idx].emailOk ? "1" : "0") + ",";
    json += "\"p\":" + String(recs[idx].pushMask) + ",";
    json += "\"en\":" + String(recs[idx].pushEnabled) + "}";
  }
  json += "]}";
  return json;
}

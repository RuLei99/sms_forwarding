#ifndef JOBS_H
#define JOBS_H

#include "globals.h"
#include "task_types.h"

// 模组任务队列（单槽）：Web 端长串口操作（Ping/发短信/AT/重启）入队后立即返回，
// loop 中由 jobsRun() 执行，前端轮询 /job?id= 取结果 —— 浏览器不再挂着等 35 秒
uint32_t jobsSubmit(ModemJobType type, const char* arg1 = "", const char* arg2 = "");
// 查询任务：0=排队中 1=执行中 2=完成（out 填充结果） 3=未知/已过期
int jobsQuery(uint32_t id, ModemResult& out);
// loop 中调用：有排队任务则执行（执行期间嵌套 handleClient，Web 保持可用）
void jobsRun();
// 队列是否空闲（健康巡检想占用模组时先看这个，避免和任务撞车）
bool jobsIdle();

#endif

#include "config_types.h"

const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
  <meta name="theme-color" content="#f5f5f7">
  <title>SMS Forwarding</title>
  <link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'%3E%3Crect width='64' height='64' rx='15' fill='%231d1d1f'/%3E%3Cpath d='M32 15c-9.4 0-17 5.9-17 13.3 0 4.2 2.4 7.9 6.2 10.3-.2 2-1 3.8-2.6 5.4 3-.2 5.7-1.3 7.8-2.8 1.7.5 3.6.7 5.6.7 9.4 0 17-5.9 17-13.4S41.4 15 32 15z' fill='%23fff'/%3E%3C/svg%3E">
  <style>
    :root{--ink:#1d1d1f;--mute:#6e6e73;--faint:#86868b;--accent:#0071e3;--accent-hover:#0077ed;--accent-soft:rgba(0,113,227,.1);--green:#34c759;--red:#ff3b30;--hairline:rgba(0,0,0,.08);--glass:rgba(255,255,255,.72);--glass-strong:rgba(255,255,255,.88);--radius:16px;--radius-md:12px;--radius-sm:8px;--pill:980px;--sidebar-w:230px;--mono:ui-monospace,'SF Mono','Cascadia Code','JetBrains Mono',Consolas,monospace;--ease:cubic-bezier(.25,.1,.25,1);--shadow-card:0 .5px 1px rgba(0,0,0,.04),0 6px 24px rgba(0,0,0,.05)}
    *{box-sizing:border-box;margin:0;padding:0}
    html{background:#f5f5f7}
    body{font-family:-apple-system,BlinkMacSystemFont,'Helvetica Neue','PingFang SC','Hiragino Sans GB','Microsoft YaHei','Segoe UI',Roboto,Arial,sans-serif;font-size:14px;line-height:1.5;color:var(--ink);display:flex;min-height:100vh;-webkit-font-smoothing:antialiased}
    body::before{content:'';position:fixed;inset:0;z-index:-1;background:radial-gradient(1100px 480px at 50% -12%,rgba(10,132,255,.06),transparent 70%),#f5f5f7}
    ::selection{background:rgba(0,113,227,.2)}
    ::-webkit-scrollbar{width:8px;height:8px}
    ::-webkit-scrollbar-thumb{background:rgba(0,0,0,.16);border-radius:4px}
    ::-webkit-scrollbar-thumb:hover{background:rgba(0,0,0,.26)}
    ::-webkit-scrollbar-track{background:transparent}
    button{font-family:inherit}
    :focus-visible{outline:2px solid var(--accent);outline-offset:2px;border-radius:4px}
    .sidebar{position:fixed;top:0;left:0;bottom:0;width:var(--sidebar-w);background:rgba(255,255,255,.6);-webkit-backdrop-filter:blur(24px) saturate(180%);backdrop-filter:blur(24px) saturate(180%);border-right:1px solid rgba(0,0,0,.06);display:flex;flex-direction:column;z-index:100;overflow-y:auto}
    .sidebar-brand{padding:24px 18px 18px;display:flex;align-items:center;gap:10px}
    .brand-mark{width:30px;height:30px;border-radius:7.5px;flex-shrink:0;background:#1d1d1f;display:flex;align-items:center;justify-content:center}
    .brand-mark svg{width:17px;height:17px;color:#fff}
    .sidebar-brand h2{font-size:15px;font-weight:600;letter-spacing:-.01em;line-height:1.2}
    .sidebar-brand span{font-size:11px;color:var(--mute);display:block;margin-top:1px}
    .sidebar-nav{flex:1;padding:4px 12px 12px}
    .sidebar-nav a{display:flex;align-items:center;gap:10px;padding:7px 10px;border-radius:var(--radius-sm);color:var(--mute);font-size:13px;font-weight:500;text-decoration:none;transition:background .18s var(--ease),color .18s var(--ease);margin-bottom:1px;cursor:pointer;user-select:none}
    .sidebar-nav a:hover{background:rgba(0,0,0,.045);color:var(--ink)}
    .sidebar-nav a:active{background:rgba(0,0,0,.075)}
    .sidebar-nav a.active{background:rgba(0,0,0,.055);color:var(--ink)}
    .ico{width:18px;height:18px;flex-shrink:0;fill:none;stroke:currentColor;stroke-width:1.7;stroke-linecap:round;stroke-linejoin:round}
    .sidebar-divider{height:1px;background:var(--hairline);margin:10px 12px}
    .sidebar-section-label{font-size:11px;color:var(--faint);padding:6px 11px;text-transform:uppercase;letter-spacing:.05em;font-weight:500}
    .sidebar-footer{padding:12px 16px 16px}
    .sidebar-footer .btn{width:100%}
    .main{margin-left:var(--sidebar-w);flex:1;display:flex;justify-content:center;min-width:0}
    .content{width:100%;max-width:820px;padding:40px 32px 64px}
    .page-title{font-size:28px;font-weight:700;letter-spacing:-.02em;line-height:1.15;margin-bottom:5px}
    .page-subtitle{font-size:14px;color:var(--mute);margin-bottom:28px}
    .status-live{color:#248a3d;font-weight:500;animation:pulse 2.2s ease-in-out infinite}
    @keyframes pulse{0%,100%{opacity:1}50%{opacity:.45}}
    .status-banner{display:flex;align-items:center;gap:9px;font-size:15px;font-weight:600;margin-bottom:24px;padding:12px 16px;border-radius:var(--radius-md);background:rgba(52,199,89,.12);color:#1d7a36}
    .status-banner .dot{width:9px;height:9px;border-radius:50%;background:currentColor;animation:pulse 2.2s infinite}
    .status-banner.warn{background:rgba(255,149,0,.12);color:#92400e}
    .status-banner.err{background:rgba(255,59,48,.1);color:#c0271d}
    .card{background:var(--glass);-webkit-backdrop-filter:blur(24px) saturate(180%);backdrop-filter:blur(24px) saturate(180%);border:1px solid rgba(0,0,0,.05);border-radius:var(--radius);box-shadow:var(--shadow-card);margin-bottom:16px}
    .card-header{padding:18px 22px 0;font-size:14px;font-weight:600;letter-spacing:-.01em}
    .card-body{padding:15px 22px 22px}
    .card-header+.card-body{padding-top:13px}
    .panel{display:none}
    .panel.active{display:block;animation:panelIn .4s var(--ease) both}
    @keyframes panelIn{from{opacity:0;transform:translateY(10px)}to{opacity:1;transform:none}}
    .form-group{margin-bottom:16px}
    .form-group:last-child{margin-bottom:0}
    .form-label,.push-channel-body label{display:block;font-size:12px;font-weight:500;color:var(--mute);margin-bottom:5px}
    .form-input,.form-select,.form-textarea,.push-channel-body input[type="text"],.push-channel-body input[type="password"],.push-channel-body select,.push-channel-body textarea{width:100%;padding:8px 11px;font-size:14px;font-family:inherit;border:1px solid #d2d2d7;border-radius:var(--radius-sm);background:#fff;color:var(--ink);transition:border-color .18s var(--ease),box-shadow .18s var(--ease);outline:none}
    .form-input:hover,.form-select:hover,.form-textarea:hover,.push-channel-body input:hover,.push-channel-body select:hover,.push-channel-body textarea:hover{border-color:#a1a1a6}
    .form-input:focus,.form-select:focus,.form-textarea:focus,.push-channel-body input:focus,.push-channel-body select:focus,.push-channel-body textarea:focus{border-color:var(--accent);box-shadow:0 0 0 3.5px rgba(0,113,227,.16)}
    .form-input::placeholder,.form-textarea::placeholder,.push-channel-body input::placeholder,.push-channel-body textarea::placeholder{color:rgba(60,60,67,.35)}
    .form-select,.push-channel-body select{cursor:pointer;background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='10' height='6' viewBox='0 0 10 6'%3E%3Cpath d='M1 1l4 4 4-4' fill='none' stroke='%236e6e73' stroke-width='1.6' stroke-linecap='round' stroke-linejoin='round'/%3E%3C/svg%3E");background-repeat:no-repeat;background-position:right 11px center;padding-right:28px;-webkit-appearance:none;appearance:none}
    .form-textarea,.push-channel-body textarea{resize:vertical;min-height:76px;line-height:1.55}
    .form-hint{font-size:12px;color:var(--faint);margin-top:6px;line-height:1.5}
    .form-warning{font-size:12px;color:#8a5300;background:rgba(255,159,10,.12);border:1px solid rgba(255,159,10,.2);padding:10px 14px;border-radius:var(--radius-sm);margin-bottom:16px;line-height:1.55}
    .form-row{display:flex;gap:14px}
    .form-row .form-group{flex:1}
    input[type="checkbox"]{-webkit-appearance:none;appearance:none;width:42px;height:25px;border-radius:13px;background:rgba(120,120,128,.32);position:relative;cursor:pointer;flex-shrink:0;margin:0;transition:background .25s var(--ease);outline-offset:3px}
    input[type="checkbox"]::after{content:'';position:absolute;top:2px;left:2px;width:21px;height:21px;border-radius:50%;background:#fff;box-shadow:0 2px 6px rgba(0,0,0,.22),0 0 .5px rgba(0,0,0,.12);transition:transform .25s var(--ease)}
    input[type="checkbox"]:checked{background:var(--green)}
    input[type="checkbox"]:checked::after{transform:translateX(17px)}
    input[type="checkbox"]:active::after{width:24px}
    input[type="checkbox"]:checked:active::after{transform:translateX(14px)}
    .toggle-row{display:flex;align-items:flex-start;justify-content:space-between;gap:18px}
    .toggle-text{flex:1;min-width:0}
    .toggle-title{font-size:14px;font-weight:600;margin-bottom:4px}
    .toggle-row input[type="checkbox"]{margin-top:2px}
    .btn{display:inline-flex;align-items:center;justify-content:center;gap:6px;padding:8px 17px;font-size:14px;font-weight:500;border-radius:var(--pill);border:none;cursor:pointer;transition:background .18s var(--ease),transform .12s var(--ease);line-height:1.4;white-space:nowrap;user-select:none}
    .btn:active:not(:disabled){transform:scale(.97)}
    .btn:disabled{opacity:.45;cursor:not-allowed}
    .btn-primary{background:var(--accent);color:#fff}
    .btn-primary:hover:not(:disabled){background:var(--accent-hover)}
    .btn-secondary,.btn-white{background:#fff;color:var(--ink);box-shadow:inset 0 0 0 1px #d2d2d7}
    .btn-secondary:hover:not(:disabled),.btn-white:hover{background:#f5f5f7;box-shadow:inset 0 0 0 1px #b8b8bd}
    .btn-danger{background:var(--red);color:#fff}
    .btn-danger:hover:not(:disabled){background:#ff453a}
    .btn-sm{padding:5px 12px;font-size:12.5px}
    .btn-block{width:100%}
    .btn-save{padding:11px 22px;font-size:15px;margin-top:4px}
    .push-channel{border:1px solid var(--hairline);border-radius:var(--radius-md);padding:15px;margin-bottom:12px;background:rgba(255,255,255,.5);transition:border-color .2s var(--ease),background .2s var(--ease)}
    .push-channel:hover{border-color:rgba(0,0,0,.14)}
    .push-channel.enabled{border-color:rgba(0,113,227,.35);background:rgba(255,255,255,.72)}
    .push-channel-header{display:flex;align-items:center;gap:10px;margin-bottom:12px}
    .push-channel-header label{font-size:14px;font-weight:600;cursor:pointer}
    .push-channel-body{display:none}
    .push-channel.enabled .push-channel-body{display:block;animation:panelIn .3s var(--ease) both}
    .push-channel-body .form-group{margin-bottom:13px}
    .push-channel-body .form-group:last-child{margin-bottom:0}
    .push-type-hint{font-size:11.5px;color:var(--mute);margin-top:7px;padding:9px 13px;background:rgba(0,0,0,.035);border-radius:8px;font-family:var(--mono);line-height:1.6;word-break:break-all}
    .result-box{margin-top:13px;padding:11px 15px;border-radius:var(--radius-sm);display:none;font-size:13px;line-height:1.55;animation:panelIn .3s var(--ease) both}
    .result-success{background:rgba(52,199,89,.13);color:#1d7a36;display:block}
    .result-error{background:rgba(255,59,48,.1);color:#c0271d;display:block}
    .result-loading{background:rgba(255,149,0,.12);color:#92400e;display:block}
    .result-info{background:var(--accent-soft);color:#0058b0;display:block}
    .info-table{width:100%;border-collapse:collapse;margin-top:2px;font-size:13px}
    .info-table td{padding:7px 6px;border-bottom:1px solid rgba(0,0,0,.045);vertical-align:top}
    .info-table tr:last-child td{border-bottom:none}
    .info-table td:first-child{font-weight:500;color:var(--mute);width:42%}
    .overview-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px}
    .overview-item{background:#fff;border:1px solid #e8e8ed;border-radius:var(--radius-md);padding:14px 16px}
    .overview-item .label{font-size:11px;color:var(--faint);text-transform:uppercase;letter-spacing:.05em;font-weight:500;margin-bottom:4px}
    .overview-item .value{font-size:16px;font-weight:600;font-variant-numeric:tabular-nums;word-break:break-all}
    .sms-row{display:flex;gap:12px;padding:13px 0;border-bottom:1px solid rgba(0,0,0,.045)}
    .sms-row:last-child{border-bottom:none}
    .sms-avatar{width:36px;height:36px;border-radius:50%;background:#e8e8ed;color:#6e6e73;font-size:11px;font-weight:600;display:flex;align-items:center;justify-content:center;flex-shrink:0}
    .sms-main{flex:1;min-width:0}
    .sms-head{display:flex;justify-content:space-between;align-items:baseline;gap:8px}
    .sms-head b{font-size:13px;font-weight:600}
    .sms-time{color:var(--faint);font-size:11px;white-space:nowrap}
    .sms-text{font-size:13px;margin-top:2px;white-space:pre-wrap;word-break:break-word}
    .sms-meta{font-size:11px;margin-top:5px;color:var(--faint)}
    .m-ok{color:#1d7a36}
    .m-no{color:#c0271d}
    .sms-empty{padding:36px 0;text-align:center;color:var(--faint);font-size:13px}
    .ch-test{margin-left:auto}
    .btn-row{display:flex;gap:8px;flex-wrap:wrap}
    .btn-row .btn{flex:1;min-width:96px}
    .btn-row+.btn-row{margin-top:9px}
    .console{background:#1d1d1f;color:#e8e8ed;font-family:var(--mono);font-size:12px;line-height:1.65;border-radius:var(--radius-md);padding:14px 16px;white-space:pre-wrap;word-break:break-all;overflow-y:auto}
    #atLog{min-height:136px;max-height:280px;margin-bottom:10px}
    #logView{min-height:300px;max-height:62vh}
    .at-bar{display:flex;gap:8px}
    .at-bar input{flex:1;font-family:var(--mono)}
    .at-bar .btn{min-width:64px}
    .nav-sheet{display:none}
    @media (max-width:740px){
      .sidebar{top:auto;right:0;bottom:0;width:100%;height:auto;flex-direction:row;align-items:stretch;background:rgba(255,255,255,.78);-webkit-backdrop-filter:blur(24px) saturate(180%);backdrop-filter:blur(24px) saturate(180%);border-right:none;border-top:1px solid rgba(0,0,0,.06);padding-bottom:env(safe-area-inset-bottom)}
      .sidebar-brand,.sidebar-divider,.sidebar-footer{display:none}
      .sidebar-nav{display:flex;overflow-x:auto;padding:6px 8px;scrollbar-width:none}
      .sidebar-nav::-webkit-scrollbar{display:none}
      .sidebar-section-label{display:none}
      .sidebar-nav a{flex-direction:column;gap:3px;padding:6px 10px;min-width:56px;font-size:10px;text-align:center}
      /* 底部栏只保留 3 个高频入口 + 更多，其余收进弹出面板（11 项平铺手机上太长） */
      .sidebar-nav a:not(.nav-primary):not(.nav-more){display:none}
      .sidebar-nav a .ico{width:20px;height:20px}
      .sidebar-nav a.active{background:var(--accent-soft);color:var(--accent)}
      .nav-sheet{display:block;position:fixed;inset:0;z-index:200;visibility:hidden;pointer-events:none}
      .nav-sheet.open{visibility:visible;pointer-events:auto}
      .nav-sheet-mask{position:absolute;inset:0;background:rgba(0,0,0,.35);opacity:0;transition:opacity .2s var(--ease)}
      .nav-sheet.open .nav-sheet-mask{opacity:1}
      .nav-sheet-panel{position:absolute;left:0;right:0;bottom:0;background:#fff;border-radius:18px 18px 0 0;padding:18px 16px calc(20px + env(safe-area-inset-bottom));transform:translateY(102%);transition:transform .25s var(--ease);max-height:70vh;overflow-y:auto;box-shadow:0 -8px 40px rgba(0,0,0,.15)}
      .nav-sheet.open .nav-sheet-panel{transform:translateY(0)}
      .nav-sheet-title{font-size:15px;font-weight:600;margin-bottom:14px;text-align:center;color:var(--ink)}
      .nav-sheet-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}
      .nav-sheet-grid a{display:flex;flex-direction:column;align-items:center;gap:7px;padding:12px 4px;border-radius:var(--radius-md);font-size:12px;font-weight:500;color:var(--ink);text-decoration:none;cursor:pointer}
      .nav-sheet-grid a:active{background:rgba(0,0,0,.05)}
      .nav-sheet-grid a .ico{width:22px;height:22px;color:var(--accent)}
      .main{margin-left:0}
      .content{padding:24px 18px calc(92px + env(safe-area-inset-bottom))}
      .page-title{font-size:24px}
      .form-row{flex-direction:column;gap:0}
    }
    @media (prefers-reduced-motion:reduce){*,*::before,*::after{animation:none!important;transition:none!important}}
  </style>
</head>
<body>
  <svg xmlns="http://www.w3.org/2000/svg" style="display:none">
    <symbol id="ig" viewBox="0 0 24 24"><rect x="3.5" y="3.5" width="7" height="7" rx="2"/><rect x="13.5" y="3.5" width="7" height="7" rx="2"/><rect x="3.5" y="13.5" width="7" height="7" rx="2"/><rect x="13.5" y="13.5" width="7" height="7" rx="2"/></symbol>
    <symbol id="iu" viewBox="0 0 24 24"><circle cx="12" cy="8.2" r="3.6"/><path d="M5.5 19.5c1.2-3.2 3.6-4.8 6.5-4.8s5.3 1.6 6.5 4.8"/></symbol>
    <symbol id="im" viewBox="0 0 24 24"><rect x="3.5" y="5.5" width="17" height="13" rx="2.5"/><path d="M4.5 7.5l7.5 6 7.5-6"/></symbol>
    <symbol id="ip" viewBox="0 0 24 24"><circle cx="12" cy="12" r="8.5"/><path d="M12 16V8.5M8.8 11.2L12 8l3.2 3.2"/></symbol>
    <symbol id="is" viewBox="0 0 24 24"><path d="M12 3.5l7 2.5v5.5c0 4.5-2.9 7.6-7 9-4.1-1.4-7-4.5-7-9V6z"/></symbol>
    <symbol id="ie" viewBox="0 0 24 24"><path d="M20.5 3.5L10.8 13.2M20.5 3.5l-6.2 17-3.5-7.3-7.3-3.5z"/></symbol>
    <symbol id="ic" viewBox="0 0 24 24"><path d="M4.5 19.5h15M7 19.5v-6.5M12 19.5v-11M17 19.5v-4"/></symbol>
    <symbol id="io" viewBox="0 0 24 24"><circle cx="12" cy="12" r="8.5"/><path d="M3.5 12h17M12 3.5c2.6 2.3 3.9 5.1 3.9 8.5s-1.3 6.2-3.9 8.5c-2.6-2.3-3.9-5.1-3.9-8.5s1.3-6.2 3.9-8.5z"/></symbol>
    <symbol id="ir" viewBox="0 0 24 24"><path d="M8.6 15.4a4.8 4.8 0 010-6.8M15.4 8.6a4.8 4.8 0 010 6.8M5.8 18.2a8.8 8.8 0 010-12.4M18.2 5.8a8.8 8.8 0 010 12.4"/></symbol>
    <symbol id="it" viewBox="0 0 24 24"><path d="M5 8l4 4-4 4M12.5 16.5H19"/></symbol>
    <symbol id="il" viewBox="0 0 24 24"><path d="M8.5 6.5h11M8.5 12h11M8.5 17.5h11M4.5 6.5h.01M4.5 12h.01M4.5 17.5h.01"/></symbol>
    <symbol id="ii" viewBox="0 0 24 24"><path d="M12 4.5c-4.5 0-8 2.8-8 6.3 0 2 1.1 3.8 2.9 5-.1 1-.5 1.9-1.2 2.6 1.4-.1 2.7-.6 3.7-1.3.8.2 1.7.3 2.6.3 4.5 0 8-2.8 8-6.3s-3.5-6.6-8-6.6z"/></symbol>
  </svg>

  <aside class="sidebar">
    <div class="sidebar-brand">
      <div class="brand-mark"><svg viewBox="0 0 24 24" fill="currentColor"><path d="M12 4.6c-4.6 0-8.3 2.9-8.3 6.5 0 2.1 1.2 3.9 3.1 5.1-.1 1-.5 1.9-1.3 2.7 1.5-.1 2.8-.7 3.8-1.4.9.3 1.8.4 2.7.4 4.6 0 8.3-2.9 8.3-6.5S16.6 4.6 12 4.6z"/></svg></div>
      <div><h2>SMS FWD</h2><span>短信转发器</span></div>
    </div>
    <nav class="sidebar-nav">
      <div class="sidebar-section-label">配置</div>
      <a data-panel="overview" class="active nav-primary"><svg class="ico"><use href="#ig"/></svg><span class="txt">系统概览</span></a>
      <a data-panel="inbox" class="nav-primary"><svg class="ico"><use href="#ii"/></svg><span class="txt">短信记录</span></a>
      <a data-panel="account"><svg class="ico"><use href="#iu"/></svg><span class="txt">账号管理</span></a>
      <a data-panel="email"><svg class="ico"><use href="#im"/></svg><span class="txt">邮件通知</span></a>
      <a data-panel="push" class="nav-primary"><svg class="ico"><use href="#ip"/></svg><span class="txt">推送通道</span></a>
      <a data-panel="admin"><svg class="ico"><use href="#is"/></svg><span class="txt">管理员 &amp; 黑名单</span></a>
      <div class="sidebar-divider"></div>
      <div class="sidebar-section-label">工具</div>
      <a data-panel="sendsms"><svg class="ico"><use href="#ie"/></svg><span class="txt">发送短信</span></a>
      <a data-panel="network"><svg class="ico"><use href="#io"/></svg><span class="txt">网络测试</span></a>
      <a data-panel="modem"><svg class="ico"><use href="#ir"/></svg><span class="txt">模组控制</span></a>
      <a data-panel="atterm"><svg class="ico"><use href="#it"/></svg><span class="txt">AT 终端</span></a>
      <a data-panel="log"><svg class="ico"><use href="#il"/></svg><span class="txt">系统日志</span></a>
      <a class="nav-more" onclick="openNavSheet()"><svg class="ico"><use href="#ic"/></svg><span class="txt">更多</span></a>
    </nav>
    <div class="sidebar-footer">
      <button class="btn btn-white btn-sm btn-block" onclick="switchPanel('account')"><span>修改密码</span></button>
    </div>
  </aside>

  <!-- 手机端「更多」面板：全部功能入口（桌面端隐藏） -->
  <div class="nav-sheet" id="navSheet">
    <div class="nav-sheet-mask" onclick="closeNavSheet()"></div>
    <div class="nav-sheet-panel">
      <div class="nav-sheet-title">全部功能</div>
      <div class="nav-sheet-grid" id="navSheetGrid"></div>
    </div>
  </div>

  <main class="main">
    <div class="content">

    <div class="panel active" id="panel-overview">
      <h1 class="page-title">系统概览</h1>
      <p class="page-subtitle">设备状态与基本信息</p>
      <div class="status-banner" id="stBanner"><span class="dot"></span><span id="stText">状态获取中...</span></div>
      <div class="card">
        <div class="card-header">设备信息</div>
        <div class="card-body">
          <div class="overview-grid">
            <div class="overview-item"><div class="label">Device IP</div><div class="value" id="ovIp">%IP%</div></div>
            <div class="overview-item"><div class="label">WiFi SSID</div><div class="value" id="ovSsid">%WIFI_SSID%</div></div>
            <div class="overview-item"><div class="label">Free Heap</div><div class="value" id="ovHeap">%FREE_HEAP%</div></div>
            <div class="overview-item"><div class="label">Uptime</div><div class="value" id="ovUptime">%UPTIME%</div></div>
          </div>
        </div>
      </div>
      <div class="card">
        <div class="card-header">模组信息</div>
        <div class="card-body">
          <div class="overview-grid">
            <div class="overview-item"><div class="label">信号强度</div><div class="value" id="ovSignal">—</div></div>
            <div class="overview-item"><div class="label">运营商</div><div class="value" id="ovOperator">—</div></div>
            <div class="overview-item"><div class="label">IMEI</div><div class="value" id="ovImei">—</div></div>
            <div class="overview-item"><div class="label">SIM 卡 (ICCID)</div><div class="value" id="ovIccid">—</div></div>
            <div class="overview-item"><div class="label">模组型号</div><div class="value" id="ovModel">—</div></div>
            <div class="overview-item"><div class="label">固件版本</div><div class="value" id="ovFw">—</div></div>
          </div>
          <p class="form-hint">模组初始化成功后自动读取；信号/运营商随健康巡检每 5 分钟自动更新。更多原始信息可在「AT 终端」查询。</p>
        </div>
      </div>
      <div class="card">
        <div class="card-header">通道健康 <span id="chHealthBadge" class="status-live">● 自动刷新中</span></div>
        <div class="card-body">
          <table class="info-table" id="chHealthTable">
            <tr><td colspan="3">加载中...</td></tr>
          </table>
          <p class="form-hint">连续失败 5 次的通道自动熔断（30 分钟起步指数退避，成功即恢复），避免每条短信都陪着重试</p>
        </div>
      </div>
      <div class="card">
        <div class="card-header">配置状态</div>
        <div class="card-body">
          <table class="info-table">
            <tr><td>模组状态</td><td id="cfgModem">%MODEM_CHECK%</td></tr>
            <tr><td>邮件通知</td><td id="cfgEmail">%SMTP_CHECK%</td></tr>
            <tr><td>推送通道</td><td id="cfgPush">%PUSH_COUNT% 个已启用</td></tr>
            <tr><td>数据模式</td><td id="cfgData">%DATA_MODE%</td></tr>
            <tr><td>管理员号码</td><td>%ADMIN_PHONE%</td></tr>
          </table>
        </div>
      </div>
      <div class="card">
        <div class="card-header">系统控制</div>
        <div class="card-body">
          <button class="btn btn-danger" onclick="systemRestart()">重启系统</button>
          <p class="form-hint">重启整个设备（ESP32 + 模组），期间无法接收短信和访问网页，约需 1 分钟恢复</p>
          <div class="result-box" id="sysRestartResult"></div>
        </div>
      </div>
    </div>

    <div class="panel" id="panel-account">
      <h1 class="page-title">账号管理</h1>
      <p class="page-subtitle">修改 Web 管理界面的登录凭据</p>
      <form action="/save" method="POST" id="mainForm">
      <div class="card">
        <div class="card-header">登录凭据</div>
        <div class="card-body">
          <div class="form-warning">首次使用请立即修改默认密码！默认: )rawliteral" DEFAULT_WEB_USER " / " DEFAULT_WEB_PASS R"rawliteral(</div>
          <div class="form-row">
            <div class="form-group"><label class="form-label">管理账号</label><input class="form-input" type="text" name="webUser" value="%WEB_USER%" placeholder="admin"></div>
            <div class="form-group"><label class="form-label">管理密码</label><input class="form-input" type="password" name="webPass" value="%WEB_PASS%" placeholder="设置复杂密码"></div>
          </div>
        </div>
      </div>
      <button type="submit" class="btn btn-primary btn-block btn-save">保存配置</button>
      </form>
      <div class="card" style="margin-top:14px;">
        <div class="card-header">配置备份 / 恢复</div>
        <div class="card-body">
          <div class="btn-row">
            <button class="btn btn-secondary btn-sm" onclick="window.open('/config/export?plain=1')">导出配置（含密钥）</button>
            <button class="btn btn-secondary btn-sm" onclick="window.open('/config/export')">导出配置（密钥打码）</button>
          </div>
          <div class="btn-row" style="margin-top:9px;">
            <input type="file" id="cfgImportFile" accept=".json" style="display:none" onchange="importConfigFile(this)">
            <button class="btn btn-secondary btn-sm" onclick="document.getElementById('cfgImportFile').click()">导入配置（JSON）</button>
          </div>
          <div class="result-box" id="cfgBackupResult" style="margin-top:9px;"></div>
          <p class="form-hint">换设备或重刷固件后可用导出的 JSON 一键恢复全部配置（含推送通道密钥，妥善保管）</p>
        </div>
      </div>
    </div>

    <div class="panel" id="panel-inbox">
      <h1 class="page-title">短信记录</h1>
      <p class="page-subtitle">最近 50 条收到的短信与转发结果 <span id="ibStatus" class="status-live">● 自动刷新中</span></p>
      <div class="card">
        <div class="card-body">
          <div id="ibList"><div class="sms-empty">加载中...</div></div>
          <div class="btn-row" style="margin-top:12px;">
            <button class="btn btn-secondary btn-sm" onclick="refreshInbox()">手动刷新</button>
            <button class="btn btn-secondary btn-sm" onclick="clearInbox()">清空记录</button>
            <button class="btn btn-secondary btn-sm" onclick="window.open('/recordsexport')">导出 CSV</button>
          </div>
          <p class="form-hint">记录持久化保存（重启不丢），页面显示最近 50 条，导出包含全部历史</p>
        </div>
      </div>
    </div>

    <div class="panel" id="panel-email">
      <h1 class="page-title">邮件通知</h1>
      <p class="page-subtitle">配置 SMTP 服务器以接收短信邮件通知</p>
      <form action="/save" method="POST" id="mainForm2">
      <div class="card">
        <div class="card-header">SMTP 设置</div>
        <div class="card-body">
          <div class="form-row">
            <div class="form-group"><label class="form-label">SMTP 服务器</label><input class="form-input" type="text" name="smtpServer" value="%SMTP_SERVER%" placeholder="smtp.qq.com"></div>
            <div class="form-group"><label class="form-label">SMTP 端口</label><input class="form-input" type="number" name="smtpPort" value="%SMTP_PORT%" placeholder="465"></div>
          </div>
          <div class="form-row">
            <div class="form-group"><label class="form-label">邮箱账号</label><input class="form-input" type="text" name="smtpUser" value="%SMTP_USER%" placeholder="your@qq.com"></div>
            <div class="form-group"><label class="form-label">密码 / 授权码</label><input class="form-input" type="password" name="smtpPass" value="%SMTP_PASS%" placeholder="授权码"></div>
          </div>
          <div class="form-group"><label class="form-label">接收邮件地址</label><input class="form-input" type="text" name="smtpSendTo" value="%SMTP_SEND_TO%" placeholder="receiver@example.com"></div>
        </div>
      </div>
      <button type="submit" class="btn btn-primary btn-block btn-save">保存配置</button>
      </form>
    </div>

    <div class="panel" id="panel-push">
      <h1 class="page-title">推送通道</h1>
      <p class="page-subtitle">最多 5 个独立推送通道，支持 POST JSON、Bark、钉钉、飞书、PushPlus、Server酱、Gotify、Telegram、ntfy、MQTT</p>
      <form action="/save" method="POST" id="mainForm3">
      <div class="card">
        <div class="card-header">通道配置</div>
        <div class="card-body">
          %PUSH_CHANNELS%
        </div>
      </div>
      <button type="submit" class="btn btn-primary btn-block btn-save">保存配置</button>
      </form>
    </div>

    <div class="panel" id="panel-admin">
      <h1 class="page-title">管理员 &amp; 黑名单</h1>
      <p class="page-subtitle">远程控制权限与短信过滤</p>
      <form action="/save" method="POST" id="mainForm4">
      <div class="card">
        <div class="card-header">管理员手机号</div>
        <div class="card-body">
          <div class="form-group">
            <input class="form-input" type="text" name="adminPhone" value="%ADMIN_PHONE%" placeholder="13800138000">
            <p class="form-hint">此号码可通过短信发送远程指令（SMS:号码:内容 发短信、RESET 重启、REPORT 立即发健康报告）</p>
          </div>
        </div>
      </div>
      <div class="card">
        <div class="card-header">号码黑名单</div>
        <div class="card-body">
          <div class="form-group">
            <textarea class="form-textarea" name="numberBlackList" rows="5" placeholder="每行一个号码">%NUMBER_BLACK_LIST%</textarea>
            <p class="form-hint">黑名单号码发来的短信将被自动忽略</p>
          </div>
        </div>
      </div>
      <div class="card">
        <div class="card-header">关键词过滤</div>
        <div class="card-body">
          <div class="form-row">
            <div class="form-group"><label class="form-label">过滤模式</label>
              <select class="form-select" name="filterMode">
                <option value="blacklist"%FLT_BL_SEL%>黑名单：命中关键词不转发</option>
                <option value="whitelist"%FLT_WL_SEL%>白名单：仅转发命中关键词</option>
              </select>
            </div>
            <div class="form-group"><label class="form-label">关键词（每行一个）</label>
              <textarea class="form-textarea" name="filterKeywords" rows="4" placeholder="留空则不过滤">%FILTER_KEYWORDS%</textarea>
            </div>
          </div>
          <p class="form-hint">先经过号码黑名单，再经过关键词过滤；管理员命令不受过滤影响</p>
        </div>
      </div>
      <div class="card">
        <div class="card-header">时区与每日报告</div>
        <div class="card-body">
          <div class="form-row">
            <div class="form-group"><label class="form-label">时区（小时，-12 ~ 14）</label>
              <input class="form-input" type="number" name="tzHours" value="%TZ_HOURS%" min="-12" max="14" placeholder="8">
            </div>
            <div class="form-group"><label class="form-label">每日健康报告</label>
              <label style="display:inline-flex;align-items:center;gap:8px;padding:8px 0;cursor:pointer;">
                <input type="checkbox" name="reportEnabled"%REPORT_CHECKED%> 每天 8 点发送
              </label>
            </div>
          </div>
          <p class="form-hint">报告经邮件 + 所有有效推送通道发送，内容为昨日收信/转发统计与设备状态</p>
        </div>
      </div>
      <button type="submit" class="btn btn-primary btn-block btn-save">保存配置</button>
      </form>
    </div>

    <div class="panel" id="panel-sendsms">
      <h1 class="page-title">发送短信</h1>
      <p class="page-subtitle">通过模组直接发送短信</p>
      <div class="card">
        <div class="card-header">新建短信</div>
        <div class="card-body">
          <form id="smsForm" onsubmit="return submitSmsForm(event)">
            <div class="form-group"><label class="form-label">目标号码</label><input class="form-input" type="text" name="phone" id="smsPhone" placeholder="13800138000" required></div>
            <div class="form-group"><label class="form-label">短信内容</label><textarea class="form-textarea" name="content" id="smsContent" placeholder="输入短信内容..." required oninput="updateCount(this)"></textarea><p class="form-hint">已输入 <span id="charCount">0</span> 字符</p></div>
            <button type="submit" class="btn btn-primary" id="smsSendBtn" style="padding:10px 22px;">发送短信</button>
          </form>
          <div class="result-box" id="smsSendResult" style="margin-top:10px;"></div>
        </div>
      </div>
    </div>

    <div class="panel" id="panel-network">
      <h1 class="page-title">网络测试</h1>
      <p class="page-subtitle">通过模组数据连接测试网络连通性</p>
      <div class="card">
        <div class="card-header">Ping</div>
        <div class="card-body">
          <button class="btn btn-secondary" id="pingBtn" onclick="confirmPing()">Ping 8.8.8.8</button>
          <p class="form-hint">通过模组执行 Ping，会临时开启数据连接；"仅收短信模式"开启时此功能不可用</p>
          <div class="result-box" id="pingResult"></div>
        </div>
      </div>
      <div class="card">
        <div class="card-header">WiFi 配置（主 + 备用热备）</div>
        <div class="card-body">
          <form action="/save" method="POST">
            <div class="form-row">
              <div class="form-group"><label class="form-label">主 WiFi SSID</label><input class="form-input" type="text" name="wifi1Ssid" value="%WIFI1_SSID%" placeholder="留空使用固件内置"></div>
              <div class="form-group"><label class="form-label">主 WiFi 密码</label><input class="form-input" type="password" name="wifi1Pass" value="%WIFI1_PASS%" placeholder="主 WiFi 密码"></div>
            </div>
            <div class="form-row">
              <div class="form-group"><label class="form-label">备用 SSID（可选）</label><input class="form-input" type="text" name="wifi2Ssid" value="%WIFI2_SSID%" placeholder="留空不启用热备"></div>
              <div class="form-group"><label class="form-label">备用密码</label><input class="form-input" type="password" name="wifi2Pass" value="%WIFI2_PASS%" placeholder="备用 WiFi 密码"></div>
            </div>
            <p class="form-hint">主 WiFi 保存在设备里，留空则使用固件内置（wifi_config.h）；保存后点击下方「重启 WiFi」或重启设备生效。主 WiFi 掉线超 60 秒自动切备用，恢复后不自动切回。</p>
            <button type="submit" class="btn btn-primary btn-sm">保存 WiFi 配置</button>
          </form>
        </div>
      </div>
      <div class="card">
        <div class="card-header">WiFi 控制</div>
        <div class="card-body">
          <button class="btn btn-danger" onclick="wifiRestart()">重启 WiFi</button>
          <p class="form-hint">断开当前 WiFi 连接并重新连接</p>
          <div class="result-box" id="wifiResult"></div>
        </div>
      </div>
    </div>

    <div class="panel" id="panel-modem">
      <h1 class="page-title">模组控制</h1>
      <p class="page-subtitle">模组重启与飞行模式控制（信号/运营商/IMEI 见「系统概览」）</p>
      <div class="card">
        <div class="card-header">流量安全</div>
        <div class="card-body">
          <div class="toggle-row">
            <div class="toggle-text">
              <div class="toggle-title">仅收短信模式</div>
              <p class="form-hint">开启后锁定模组数据连接（开机及注册网络后自动去激活数据承载），收发短信不受影响。短信转发走的家中 WiFi，不消耗 SIM 流量；开启期间"网络测试"中的 Ping 将被禁用。适合境外漫游卡，避免数据漫游扣费。</p>
            </div>
            <input type="checkbox" id="dataLock" %SMS_ONLY_CHECKED% onchange="toggleDataLock(this)">
          </div>
          <div class="result-box" id="dataLockResult"></div>
        </div>
      </div>
      <div class="card">
        <div class="card-header">模组重启</div>
        <div class="card-body">
          <div class="btn-row"><button class="btn btn-danger" onclick="modemAction('restart')">软重启 (AT+CFUN)</button><button class="btn btn-danger" onclick="modemAction('hardreset')">硬重启 (EN引脚)</button></div>
          <p class="form-hint">软重启发送 AT+CFUN=1,1 指令（15s 超时）；硬重启通过 EN 引脚断电后重新上电</p>
          <div class="result-box" id="modemRstResult"></div>
        </div>
      </div>
      <div class="card">
        <div class="card-header">飞行模式</div>
        <div class="card-body">
          <div class="btn-row"><button class="btn btn-danger" id="flightBtn" onclick="toggleFlightMode()">切换飞行模式</button><button class="btn btn-secondary" onclick="queryFlightMode()">查询状态</button></div>
          <p class="form-hint">飞行模式开启后模组射频关闭，无法收发短信</p>
          <div class="result-box" id="flightResult"></div>
        </div>
      </div>
    </div>

    <div class="panel" id="panel-atterm">
      <h1 class="page-title">AT 指令终端</h1>
      <p class="page-subtitle">直接向模组发送 AT 指令并接收响应</p>
      <div class="card">
        <div class="card-header">终端</div>
        <div class="card-body">
          <div class="console" id="atLog">就绪 — 输入 AT 指令开始调试</div>
          <div class="at-bar"><input class="form-input" type="text" id="atCmd" placeholder="AT+CSQ"><button class="btn btn-primary btn-sm" onclick="sendAT()" id="atBtn">发送</button></div>
          <div class="btn-row" style="margin-top:9px;"><button class="btn btn-secondary btn-sm" onclick="clearATLog()">清空日志</button></div>
          <p class="form-hint">直接向模组串口发送指令并接收响应，请谨慎操作</p>
        </div>
      </div>
    </div>

    <div class="panel" id="panel-log">
      <h1 class="page-title">系统日志</h1>
      <p class="page-subtitle">实时查看设备串口日志输出 <span id="logStatus" class="status-live">● 自动刷新中</span></p>
      <div class="card">
        <div class="card-header">日志输出</div>
        <div class="card-body">
          <div class="console" id="logView">加载中...</div>
          <div class="btn-row" style="margin-top:9px;">
            <button class="btn btn-secondary btn-sm" onclick="clearLogUI()">清空显示</button>
            <button class="btn btn-secondary btn-sm" onclick="refreshLog()">手动刷新</button>
            <label style="margin-left:8px;font-size:13px;cursor:pointer;display:inline-flex;align-items:center;gap:7px;"><input type="checkbox" id="logAuto" checked onchange="toggleLogAuto()"> 自动刷新</label>
          </div>
          <p class="form-hint">显示设备运行时输出的日志信息，每2秒自动刷新。日志最多保留最近120条。</p>
        </div>
      </div>
    </div>

    </div>
  </main>

  <script>
    function switchPanel(name) {
      document.querySelectorAll('.panel').forEach(function(p) { p.classList.remove('active'); });
      document.getElementById('panel-' + name).classList.add('active');
      document.querySelectorAll('.sidebar-nav a').forEach(function(a) { a.classList.remove('active'); });
      document.querySelector('.sidebar-nav a[data-panel="' + name + '"]').classList.add('active');
      window.scrollTo({top: 0, behavior: 'smooth'});
      if (location.hash.slice(1) !== name) location.hash = name;
    }
    document.querySelectorAll('.sidebar-nav a').forEach(function(a) {
      a.addEventListener('click', function() { switchPanel(this.dataset.panel); });
    });
    // hash 路由：#push 直达面板，浏览器后退可用
    function applyHash() {
      var h = location.hash.slice(1);
      if (h && document.getElementById('panel-' + h)) switchPanel(h);
    }
    window.addEventListener('hashchange', applyHash);

    function toggleChannel(idx) {
      var ch = document.getElementById('channel' + idx);
      var cb = document.getElementById('push' + idx + 'en');
      if (cb.checked) ch.classList.add('enabled'); else ch.classList.remove('enabled');
    }
    function updateTypeHint(idx) {
      var sel = document.getElementById('push' + idx + 'type');
      var hint = document.getElementById('hint' + idx);
      var extra = document.getElementById('extra' + idx);
      var custom = document.getElementById('custom' + idx);
      var type = parseInt(sel.value);
      // 按平台预填官方默认接口地址；字段为空或恰为某平台默认地址时才更新，用户手输的自定义地址不动
      var urlInput = document.getElementById('url' + idx);
      if (urlInput) {
        var defUrls = {4:'https://oapi.dingtalk.com/robot/send',5:'http://www.pushplus.plus/send',8:'https://open.feishu.cn/open-apis/bot/v2/hook/',10:'https://api.telegram.org',11:'https://ntfy.sh'};
        var urlPhs = {1:'http://your-server.com/api',2:'https://api.day.app/你的Key（或自建服务器地址）',3:'http://your-server.com/api',6:'留空将自动用 SendKey 拼接官方接口',7:'http://your-server.com/api',9:'https://你的Gotify服务器地址',11:'留空使用官方 ntfy.sh，也可填自建服务器',12:'broker 地址 host:port（默认 1883）'};
        var cur = urlInput.value;
        var isDefaultVal = false;
        for (var k in defUrls) { if (defUrls[k] === cur) { isDefaultVal = true; break; } }
        if ((!cur || isDefaultVal) && defUrls[type]) urlInput.value = defUrls[type];
        urlInput.placeholder = urlPhs[type] || 'http://your-server.com/api 或 webhook地址';
      }
      extra.style.display = 'none'; custom.style.display = 'none';
      document.getElementById('key1label' + idx).innerText = '参数 1';
      document.getElementById('key2label' + idx).innerText = '参数 2';
      document.getElementById('key1' + idx).placeholder = '';
      document.getElementById('key2' + idx).placeholder = '';
      var kg = document.getElementById('key2group' + idx);
      if (kg) kg.style.display = 'none';
      if (type == 1) hint.innerHTML = 'POST JSON<br>{"sender":"+8613800138000","message":"...","timestamp":"2026-01-01 12:00:00"}';
      else if (type == 2) hint.innerHTML = 'Bark (iOS)<br>POST {"title":"发送者","body":"短信内容"}';
      else if (type == 3) hint.innerHTML = 'GET 请求<br>URL?sender=xxx&message=xxx&timestamp=xxx';
      else if (type == 4) { hint.innerHTML = '钉钉机器人<br>填写 Webhook 地址，加签需填 Secret'; extra.style.display='block'; document.getElementById('key1label'+idx).innerText='Secret（加签密钥，可选）'; document.getElementById('key1'+idx).placeholder='SEC...'; }
      else if (type == 5) { hint.innerHTML = 'PushPlus<br>填写 Token，URL 留空使用默认'; extra.style.display='block'; document.getElementById('key1label'+idx).innerText='Token'; document.getElementById('key1'+idx).placeholder='pushplus token'; if(kg)kg.style.display='block'; document.getElementById('key2label'+idx).innerText='发送渠道'; document.getElementById('key2'+idx).placeholder='wechat / extension / app'; }
      else if (type == 6) { hint.innerHTML = 'Server酱<br>填写 SendKey，URL 留空使用默认'; extra.style.display='block'; document.getElementById('key1label'+idx).innerText='SendKey'; document.getElementById('key1'+idx).placeholder='SCT...'; }
      else if (type == 7) { hint.innerHTML = '自定义模板<br>使用 {sender} {message} {timestamp} 占位符'; custom.style.display='block'; }
      else if (type == 8) { hint.innerHTML = '飞书机器人<br>已预填官方地址，需在其末尾拼接你的 Hook Token；签名验证另填 Secret'; extra.style.display='block'; document.getElementById('key1label'+idx).innerText='Secret（签名密钥，可选）'; document.getElementById('key1'+idx).placeholder='飞书签名密钥'; }
      else if (type == 9) { hint.innerHTML = 'Gotify<br>填写服务器地址 + 应用 Token'; extra.style.display='block'; document.getElementById('key1label'+idx).innerText='Token（应用 Token）'; document.getElementById('key1'+idx).placeholder='A...'; }
      else if (type == 10) { hint.innerHTML = 'Telegram Bot<br>Chat ID（参数1）+ Bot Token（参数2）'; extra.style.display='block'; document.getElementById('key1label'+idx).innerText='Chat ID'; document.getElementById('key1'+idx).placeholder='123456789'; if(kg)kg.style.display='block'; document.getElementById('key2label'+idx).innerText='Bot Token'; document.getElementById('key2'+idx).placeholder='12345678:ABC...'; }
      else if (type == 11) { hint.innerHTML = 'ntfy 推送<br>填 Topic（参数1），URL 留空用官方 ntfy.sh，自建服务填根地址'; extra.style.display='block'; document.getElementById('key1label'+idx).innerText='Topic（订阅主题）'; document.getElementById('key1'+idx).placeholder='my-sms-topic'; }
      else if (type == 12) { hint.innerHTML = 'MQTT 发布<br>URL 填 broker 地址 host:port（明文 1883）<br>参数1=发布 Topic，参数2 可选 user:pass'; extra.style.display='block'; document.getElementById('key1label'+idx).innerText='Topic（发布主题）'; document.getElementById('key1'+idx).placeholder='home/sms'; if(kg)kg.style.display='block'; document.getElementById('key2label'+idx).innerText='账号密码（可选）'; document.getElementById('key2'+idx).placeholder='user:pass'; }
    }
    document.addEventListener('DOMContentLoaded', function() {
      for (var i = 0; i < 5; i++) { toggleChannel(i); updateTypeHint(i); }
    });

    function updateCount(el) { document.getElementById('charCount').textContent = el.value.length; }

    // ---- 推送通道测试 ----
    function testPush(i){
      var b=document.getElementById('testBtn'+i);
      var ch=document.getElementById('channel'+i);
      var rb=ch.querySelector('.test-result');
      if(!rb){rb=document.createElement('div');rb.className='result-box test-result';ch.appendChild(rb);}
      b.disabled=true;b.textContent='发送中...';
      rb.classList.remove('result-loading','result-success','result-error');
      rb.classList.add('result-loading');rb.textContent='正在发送测试推送...';
      fetch('/testpush?ch='+i).then(function(r){return r.json()}).then(function(d){
        b.disabled=false;b.textContent='发送测试';
        rb.classList.remove('result-loading','result-success','result-error');
        rb.classList.add(d.success?'result-success':'result-error');
        rb.textContent=d.message;
      }).catch(function(e){
        b.disabled=false;b.textContent='发送测试';
        rb.classList.remove('result-loading','result-success','result-error');
        rb.classList.add('result-error');rb.textContent='请求失败: '+e;
      });
    }

    // ---- 概览状态轮询（5 秒，仅概览面板可见时） ----
    var statusTimer = null;
    function applyStatus(d){
      function set(id,v){var e=document.getElementById(id);if(e)e.textContent=v;}
      set('ovIp',d.ip);set('ovSsid',d.ssid);set('ovHeap',d.heap+' KB');set('ovUptime',d.uptime);
      set('ovSignal',d.signal||'—');set('ovOperator',d.operator||'—');set('ovImei',d.imei||'—');
      set('ovIccid',d.iccid||'—');set('ovModel',d.model||'—');set('ovFw',d.fw||'—');
      var ht=document.getElementById('chHealthTable');
      if(ht&&d.channels){
        var rows='',anyCool=false;
        for(var ci=0;ci<d.channels.length;ci++){
          var c=d.channels[ci];
          if(!c.ok&&!c.fail&&!c.blocked)continue;
          if(c.cool)anyCool=true;
          rows+='<tr><td>通道 '+(ci+1)+'</td><td><span class="'+(c.cool?'m-no':'m-ok')+'">'+(c.cool?'已熔断':'正常')+'</span></td><td>成功 '+c.ok+' / 失败 '+c.fail+(c.blocked?(' / 跳过 '+c.blocked):'')+'</td></tr>';
        }
        ht.innerHTML=rows||'<tr><td colspan="3">尚无推送记录</td></tr>';
        var hb=document.getElementById('chHealthBadge');
        if(hb){hb.className=anyCool?'status-err':'status-live';hb.textContent=anyCool?'● 有通道熔断':'● 正常';}
      }
      set('cfgModem',d.modem?'已就绪':'未就绪');
      set('cfgEmail',d.email?'已配置':'未配置');
      set('cfgPush',d.push+' 个已启用');
      set('cfgData',d.smsOnly?'仅收短信（数据已锁定）':'标准（数据未锁定）');
      var b=document.getElementById('stBanner'),t=document.getElementById('stText');
      if(b&&t){
        if(!d.modem){b.className='status-banner err';t.textContent='模组未就绪 — 短信暂停，后台自动重试中';}
        else if(!d.email&&d.push===0){b.className='status-banner warn';t.textContent='转发未配置 — 请配置邮件或推送通道';}
        else{b.className='status-banner';t.textContent='设备运行正常';}
      }
    }
    function refreshStatus(){
      fetch('/status').then(function(r){return r.json()}).then(applyStatus).catch(function(){});
    }
    function startStatusPoll(){ if(statusTimer)return; statusTimer=setInterval(refreshStatus,5000); }
    function stopStatusPoll(){ if(statusTimer){clearInterval(statusTimer);statusTimer=null;} }

    // ---- 短信记录 ----
    var inboxTimer = null;
    function esc(s){return String(s==null?'':s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}
    function refreshInbox(){
      fetch('/smslog').then(function(r){return r.json()}).then(function(d){
        var el=document.getElementById('ibList');
        if(!d||!d.items||!d.items.length){
          el.innerHTML='<div class="sms-empty">暂无记录 — 收到短信后会自动出现在这里</div>';
          return;
        }
        var h='';
        for(var i=0;i<d.items.length;i++){
          var it=d.items[i];
          var av=String(it.s).replace(/[^0-9A-Za-z\u4e00-\u9fa5]/g,'');av=av.slice(-3)||'?';
          var meta='邮件 <span class="'+(it.e?'m-ok':'m-no')+'">'+(it.e?'成功':'未配置/失败')+'</span>';
          if(it.en>0){
            for(var c=0;c<5;c++){
              if(it.en&(1<<c)){
                var ok=it.p&(1<<c);
                meta+=' · 通道'+(c+1)+' <span class="'+(ok?'m-ok':'m-no')+'">'+(ok?'成功':'失败')+'</span>';
              }
            }
          }else{meta+=' · 未启用推送';}
          h+='<div class="sms-row"><div class="sms-avatar">'+esc(av)+'</div>'
            +'<div class="sms-main"><div class="sms-head"><b>'+esc(it.s)+'</b><span class="sms-time">'+esc(it.ts)+'</span></div>'
            +'<div class="sms-text">'+esc(it.t)+'</div>'
            +'<div class="sms-meta">'+meta+'</div></div></div>';
        }
        el.innerHTML=h;
      }).catch(function(){});
    }
    function startInboxPoll(){ if(inboxTimer)return; inboxTimer=setInterval(refreshInbox,10000); }
    function stopInboxPoll(){ if(inboxTimer){clearInterval(inboxTimer);inboxTimer=null;} }
    function clearInbox(){
      if(!confirm('确定清空所有短信记录？'))return;
      fetch('/smslog?clear=1').then(function(){refreshInbox();});
    }

    // ---- 表单保存（fetch + 行内反馈，无页面跳转） ----
    function setBoxState(rb, state, text){
      rb.classList.remove('result-loading','result-success','result-error');
      rb.classList.add(state);
      rb.textContent=text;
    }
    document.querySelectorAll('form[action="/save"]').forEach(function(f){
      f.addEventListener('submit', function(e){
        e.preventDefault();
        var btn=f.querySelector('.btn-save');
        var rb=f.querySelector('.save-result');
        if(!rb){rb=document.createElement('div');rb.className='result-box save-result';f.appendChild(rb);}
        btn.disabled=true;btn.textContent='保存中，请稍候...';
        setBoxState(rb,'result-loading','正在保存...');
        fetch('/save',{method:'POST',body:new FormData(f)}).then(function(r){return r.json()}).then(function(d){
          btn.disabled=false;btn.textContent='保存配置';
          setBoxState(rb,d.success?'result-success':'result-error',d.message||'已保存');
          refreshStatus();
        }).catch(function(err){
          btn.disabled=false;btn.textContent='保存配置';
          setBoxState(rb,'result-error','请求失败: '+err);
        });
      });
    });

    function importConfigFile(inp){
      var f=inp.files[0];
      if(!f)return;
      var r=document.getElementById('cfgBackupResult');
      r.className='result-box result-loading';r.textContent='正在导入 '+f.name+' ...';
      f.text().then(function(txt){
        return fetch('/config/import',{method:'POST',headers:{'Content-Type':'application/json'},body:txt})
          .then(function(rr){return rr.json()});
      }).then(function(d){
        r.className='result-box '+(d.success?'result-success':'result-error');
        r.textContent=d.message;
        if(d.success)setTimeout(function(){location.reload();},2000);
      }).catch(function(e){
        r.className='result-box result-error';r.textContent='导入失败: '+e;
      });
      inp.value='';
    }

    function openNavSheet(){
      var g=document.getElementById('navSheetGrid');
      if(!g.dataset.built){
        document.querySelectorAll('.sidebar-nav a[data-panel]').forEach(function(a){
          var b=document.createElement('a');
          b.innerHTML=a.innerHTML;
          b.onclick=function(){switchPanel(a.dataset.panel);closeNavSheet();};
          g.appendChild(b);
        });
        g.dataset.built='1';
      }
      document.getElementById('navSheet').classList.add('open');
    }
    function closeNavSheet(){document.getElementById('navSheet').classList.remove('open');}

    function confirmPing(){if(confirm('确定要执行 Ping 吗？将消耗少量流量。'))doPing();}
    function doPing(){
      var b=document.getElementById('pingBtn'),r=document.getElementById('pingResult');
      b.disabled=true;b.textContent='Pinging...';
      runJob({url:'/ping',method:'POST'},r,'正在 Ping 8.8.8.8（最长 35 秒）...',90,function(){
        b.disabled=false;b.textContent='Ping 8.8.8.8';
      });
    }

    // ---- 模组任务队列：提交 → 轮询 /job?id= 直到完成 ----
    function runJob(req, box, busyText, timeoutSec, onDone){
      timeoutSec=timeoutSec||90;
      box.className='result-box result-loading';box.textContent=busyText;
      fetch(req.url,{method:req.method||'GET'}).then(function(rr){return rr.json()}).then(function(d){
        if(!d.queued||!d.id){
          box.className='result-box result-error';box.textContent=d.message||'提交失败';
          if(onDone)onDone();return;
        }
        var left=timeoutSec;
        var timer=setInterval(function(){
          left--;
          if(left<=0){clearInterval(timer);box.className='result-box result-error';box.textContent='等待超时，请到系统日志查看结果';if(onDone)onDone();return;}
          fetch('/job?id='+d.id).then(function(rr){return rr.json()}).then(function(j){
            if(j.state==='queued'||j.state==='running'){box.textContent=busyText+'（'+j.message+'，'+left+'s）';return;}
            clearInterval(timer);
            box.className='result-box '+(j.success?'result-success':'result-error');
            box.textContent=j.message;
            if(onDone)onDone();
          }).catch(function(){});
        },1000);
      }).catch(function(e){box.className='result-box result-error';box.textContent='请求失败: '+e;if(onDone)onDone();});
    }

    function submitSmsForm(ev){
      ev.preventDefault();
      var phone=document.getElementById('smsPhone').value.trim();
      var content=document.getElementById('smsContent').value.trim();
      var b=document.getElementById('smsSendBtn'),r=document.getElementById('smsSendResult');
      if(!phone||!content){r.className='result-box result-error';r.textContent='请填写目标号码和短信内容';return false;}
      b.disabled=true;b.textContent='发送中...';
      var body='phone='+encodeURIComponent(phone)+'&content='+encodeURIComponent(content);
      r.className='result-box result-loading';r.textContent='已提交，正在通过模组发送...';
      fetch('/sendsms',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})
        .then(function(rr){return rr.json()}).then(function(d){
          if(!d.queued){b.disabled=false;b.textContent='发送短信';r.className='result-box result-error';r.textContent=d.message||'提交失败';return;}
          var left=90;
          var timer=setInterval(function(){
            left--;
            if(left<=0){clearInterval(timer);b.disabled=false;b.textContent='发送短信';r.className='result-box result-error';r.textContent='等待超时，请到系统日志查看结果';return;}
            fetch('/job?id='+d.id).then(function(rr){return rr.json()}).then(function(j){
              if(j.state==='queued'||j.state==='running')return;
              clearInterval(timer);b.disabled=false;b.textContent='发送短信';
              r.className='result-box '+(j.success?'result-success':'result-error');r.textContent=j.message;
            }).catch(function(){});
          },1000);
        }).catch(function(e){b.disabled=false;b.textContent='发送短信';r.className='result-box result-error';r.textContent='请求失败: '+e;});
      return false;
    }

    var sysReloadTimer = null;
    function scheduleSysReload(){
      if (sysReloadTimer) return;
      var left = 30, r = document.getElementById('sysRestartResult');
      r.className = 'result-box result-success';
      r.textContent = '系统重启中，' + left + ' 秒后自动刷新页面...';
      sysReloadTimer = setInterval(function(){
        left--;
        if(left <= 0){ clearInterval(sysReloadTimer); location.reload(); }
        else r.textContent = '系统重启中，' + left + ' 秒后自动刷新页面...';
      }, 1000);
    }
    function systemRestart(){
      if(!confirm('确定要重启整个系统吗？重启期间将无法接收短信和访问网页。'))return;
      var r=document.getElementById('sysRestartResult');
      r.className='result-box result-loading';r.textContent='正在发送重启请求...';
      fetch('/system?action=restart').then(function(rr){return rr.json()}).then(function(d){
        scheduleSysReload();
      }).catch(function(e){
        scheduleSysReload();
      });
    }

    function wifiRestart(){
      if(!confirm('确定要重启WiFi吗？网页将暂时不可用。'))return;
      var r=document.getElementById('wifiResult');
      r.className='result-box result-loading';r.textContent='WiFi 重启中（约5秒）...';
      fetch('/wifi?action=restart').then(function(rr){return rr.json()}).then(function(d){
        r.className=d.success?'result-box result-success':'result-box result-error';
        r.textContent=d.message;
      }).catch(function(e){r.className='result-box result-error';r.textContent='请求失败: '+e;});
    }

    function queryFlightMode(){
      var r=document.getElementById('flightResult');
      r.className='result-box result-loading';r.textContent='查询中...';
      fetch('/flight?action=query').then(function(rr){return rr.json()}).then(function(d){
        if(d.success){r.className='result-box result-info';r.innerHTML=d.message;}
        else{r.className='result-box result-error';r.innerHTML='查询失败: '+d.message;}
      }).catch(function(e){r.className='result-box result-error';r.textContent='请求失败: '+e;});
    }
    function toggleFlightMode(){
      if(!confirm('确定要切换飞行模式吗？'))return;
      var b=document.getElementById('flightBtn'),r=document.getElementById('flightResult');
      b.disabled=true;r.className='result-box result-loading';r.textContent='切换中...';
      fetch('/flight?action=toggle').then(function(rr){return rr.json()}).then(function(d){
        b.disabled=false;
        if(d.success){r.className='result-box result-success';r.innerHTML=d.message;}
        else{r.className='result-box result-error';r.innerHTML='切换失败: '+d.message;}
      }).catch(function(e){b.disabled=false;r.className='result-box result-error';r.textContent='请求失败: '+e;});
    }

    function toggleDataLock(cb){
      var r=document.getElementById('dataLockResult');
      var want=cb.checked?'on':'off';
      cb.disabled=true;
      r.className='result-box result-loading';
      r.textContent=want==='on'?'正在锁定数据连接...':'正在解锁数据连接...';
      fetch('/datalock?lock='+want).then(function(rr){return rr.json()}).then(function(d){
        cb.disabled=false;
        if(d.success){
          cb.checked=d.smsOnly;
          r.className='result-box result-success';r.textContent=d.message;
          var cfg=document.getElementById('cfgData');
          if(cfg) cfg.textContent=d.smsOnly?'仅收短信（数据已锁定）':'标准（数据未锁定）';
        }else{
          cb.checked=!cb.checked;
          r.className='result-box result-error';r.textContent=d.message||'操作失败';
        }
      }).catch(function(e){
        cb.disabled=false;cb.checked=!cb.checked;
        r.className='result-box result-error';r.textContent='请求失败: '+e;
      });
    }

    function modemAction(action){
      var names={'restart':'软重启','hardreset':'硬重启'};
      var name=names[action]||action;
      var resultEl=document.getElementById('modemRstResult');
      if(action==='hardreset'){
        if(!confirm('硬重启将断电重启模组，确定继续？'))return;
        resultEl.className='result-box result-loading';resultEl.textContent='硬重启中（约10秒）...';
        fetch('/modem?action=hardreset').then(function(rr){return rr.json()}).then(function(d){
          resultEl.className='result-box result-success';resultEl.textContent=d.message+' — 稍后请手动查询信号确认恢复';
        }).catch(function(e){resultEl.className='result-box result-error';resultEl.textContent='请求失败: '+e;});
        return;
      }
      resultEl.className='result-box result-loading';resultEl.textContent=name+'中...';
      fetch('/modem?action='+action).then(function(rr){return rr.json()}).then(function(d){
        if(d.success){resultEl.className='result-box result-success';resultEl.innerHTML=name+'成功: '+d.message;}
        else{resultEl.className='result-box result-error';resultEl.innerHTML=name+'失败: '+d.message;}
      }).catch(function(e){resultEl.className='result-box result-error';resultEl.textContent='请求失败: '+e;});
    }

    function addLog(msg,type){
      type=type||'resp';var log=document.getElementById('atLog'),div=document.createElement('div'),b=document.createElement('b');
      if(type==='user'){b.style.color='#fff';b.textContent='> ';}
      else if(type==='error'){b.style.color='#ff6961';b.textContent='! ';}
      else{b.style.color='#30d158';b.textContent='';}
      div.appendChild(b);div.appendChild(document.createTextNode(msg));
      log.appendChild(div);log.scrollTop=log.scrollHeight;
    }
    // ---- AT 指令历史（localStorage 最近 20 条，↑/↓ 翻阅） ----
    var atHist=[];try{atHist=JSON.parse(localStorage.getItem('atHist')||'[]');}catch(e){atHist=[];}
    var atIdx=atHist.length;
    function pushATHist(cmd){
      if(atHist[atHist.length-1]===cmd)return;
      atHist.push(cmd);
      if(atHist.length>20)atHist.shift();
      atIdx=atHist.length;
      try{localStorage.setItem('atHist',JSON.stringify(atHist));}catch(e){}
    }
    function sendAT(){
      var i=document.getElementById('atCmd'),b=document.getElementById('atBtn');
      var cmd=i.value.trim();
      if(!cmd){return;}
      b.disabled=true;b.textContent='执行中';
      addLog(cmd,'user');
      fetch('/at?cmd='+encodeURIComponent(cmd)).then(function(rr){return rr.json()}).then(function(d){
        if(!d.queued){addLog(d.message||'提交失败','error');b.disabled=false;b.textContent='发送';return;}
        var left=30;
        var timer=setInterval(function(){
          left--;
          if(left<=0){clearInterval(timer);addLog('等待超时（可在系统日志查看结果）','error');b.disabled=false;b.textContent='发送';return;}
          fetch('/job?id='+d.id).then(function(rr){return rr.json()}).then(function(j){
            if(j.state==='queued'||j.state==='running')return;
            clearInterval(timer);b.disabled=false;b.textContent='发送';
            addLog(j.message||'(空响应)', j.success?'resp':'error');
            pushATHist(cmd);atIdx=atHist.length;i.value='';
          }).catch(function(){});
        },1000);
      }).catch(function(e){addLog('请求失败: '+e,'error');b.disabled=false;b.textContent='发送';});
    }
    function clearATLog(){var l=document.getElementById('atLog');l.innerHTML='';addLog('日志已清空','resp');}
    document.getElementById('atCmd').addEventListener('keydown',function(e){
      if(e.key==='Enter'){sendAT();return;}
      if(e.key==='ArrowUp'||e.key==='ArrowDown'){
        if(!atHist.length)return;
        e.preventDefault();
        atIdx=e.key==='ArrowUp'?Math.max(0,atIdx-1):Math.min(atHist.length,atIdx+1);
        this.value=atHist[atIdx]||'';
      }
    });

    var logTimer = null;
    function startLogPoll() {
      if (logTimer) return;
      logTimer = setInterval(refreshLog, 2000);
    }
    function stopLogPoll() {
      if (logTimer) { clearInterval(logTimer); logTimer = null; }
    }
    function toggleLogAuto() {
      if (document.getElementById('logAuto').checked) startLogPoll();
      else stopLogPoll();
    }
    function clearLogUI() { document.getElementById('logView').textContent = ''; }
    function refreshLog() {
      var el = document.getElementById('logView');
      fetch('/log').then(function(r) { return r.json(); }).then(function(lines) {
        if (!Array.isArray(lines)) return;
        el.textContent = lines.join('\n');
        el.scrollTop = el.scrollHeight;
      }).catch(function() { if (el.textContent === '加载中...') el.textContent = '无法获取日志'; });
    }
    var _origSwitchPanel = switchPanel;
    switchPanel = function(name) {
      _origSwitchPanel(name);
      if (name === 'log') { refreshLog(); startLogPoll(); } else stopLogPoll();
      if (name === 'overview') { refreshStatus(); startStatusPoll(); } else stopStatusPoll();
      if (name === 'inbox') { refreshInbox(); startInboxPoll(); } else stopInboxPoll();
    };
    document.addEventListener('DOMContentLoaded', function() {
      refreshLog(); startLogPoll();
      refreshStatus(); startStatusPoll();
      applyHash();  // 支持 #panel 直达
    });
  </script>
</body>
</html>
)rawliteral";

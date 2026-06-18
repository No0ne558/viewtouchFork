/*
 * Copyright ViewTouch, Inc., 1995, 1996, 1997, 1998, 2025, 2026
 *
 * main/web/web_server.cc
 *
 * Minimal POSIX HTTP/1.1 server — GET/POST, single-threaded per-request.
 * No external HTTP library required.
 *
 * Thread safety for reads: snapshot reads of MasterSystem's linked lists
 * are inherently safe (stale-read acceptable for dashboard reporting).
 * Thread safety for writes: guarded by write_mtx_ so only one web request
 * can mutate POS data at a time; the Xt main loop must not be in the same
 * critical section.  All write operations are simple field updates followed
 * by setting the "changed" flag so the main loop saves on next tick.
 */

#include "web_server.hh"

#include "system.hh"
#include "check.hh"
#include "employee.hh"
#include "labor.hh"
#include "sales.hh"

#include "version/vt_version_info.hh"
#include "src/utils/vt_logger.hh"

#include <nlohmann/json.hpp>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <sstream>
#include <string>
#include <vector>

static std::chrono::steady_clock::time_point s_start_time;

// ─────────────────────────────────────────────────────────────────────────────
// Family name lookup
// ─────────────────────────────────────────────────────────────────────────────
static const char *FamilyName(int f)
{
    static const char *kNames[] = {
        "Appetizers","Beverages","Lunch Entrees","Children's Menu","Desserts",
        "Sandwiches","Side Orders","Breakfast Entrees","A La Carte","",
        "Burgers","Dinner Entrees","Salads","Soup","Pizza","Specialty",
        "Beer","Bottled Beer","Wine","Bottled Wine","Cocktail",
        "Bottled Cocktail","Seafood","Modifier","Light Dinner","Reorder",
        "Merchandise","Specialty Entree","Reserved Wine","Banquet","Bakery","Room"
    };
    if (f >= 0 && f < (int)(sizeof(kNames)/sizeof(kNames[0])) && kNames[f][0])
        return kNames[f];
    return "Other";
}

// ─────────────────────────────────────────────────────────────────────────────
// Embedded HTML: Admin Dashboard (tabbed SPA)
// ─────────────────────────────────────────────────────────────────────────────
static const char kDashboardHtml[] = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ViewTouch — Admin</title>
<style>
*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
:root {
  --bg:#0f1117; --surface:#1a1d27; --border:#2e3247;
  --text:#e2e8f0; --muted:#94a3b8; --accent:#4a9eff;
  --green:#22c55e; --orange:#f97316; --red:#ef4444; --yellow:#eab308;
}
body { background:var(--bg); color:var(--text); font-family:"Noto Sans","Liberation Sans",sans-serif; min-height:100vh; }
header { background:var(--surface); border-bottom:1px solid var(--border); padding:.9rem 2rem; display:flex; align-items:center; gap:.75rem; }
header h1 { font-size:1.2rem; font-weight:600; color:var(--accent); }
.status-dot { width:.5rem; height:.5rem; border-radius:50%; background:var(--green); display:inline-block; }
.tag { font-size:.75rem; color:var(--muted); margin-left:auto; }

/* Tabs */
nav { background:var(--surface); border-bottom:1px solid var(--border); display:flex; gap:0; }
nav button { background:none; border:none; color:var(--muted); padding:.75rem 1.5rem; font-size:.875rem; cursor:pointer; border-bottom:2px solid transparent; transition:all .15s; }
nav button:hover { color:var(--text); }
nav button.active { color:var(--accent); border-bottom-color:var(--accent); }

/* Tab panels */
.panel { display:none; padding:1.5rem 2rem; }
.panel.active { display:block; }

/* Cards */
.cards { display:grid; grid-template-columns:repeat(auto-fit,minmax(170px,1fr)); gap:1rem; margin-bottom:1.5rem; }
.card { background:var(--surface); border:1px solid var(--border); border-radius:.75rem; padding:1.25rem 1.5rem; }
.card .label { font-size:.7rem; color:var(--muted); text-transform:uppercase; letter-spacing:.05em; margin-bottom:.5rem; }
.card .value { font-size:2rem; font-weight:700; line-height:1; }
.card .sub { font-size:.8rem; color:var(--muted); margin-top:.35rem; }
.green { color:var(--green); } .orange { color:var(--orange); } .accent { color:var(--accent); }

/* Tables */
section { background:var(--surface); border:1px solid var(--border); border-radius:.75rem; overflow:hidden; margin-bottom:1.5rem; }
section h2 { font-size:.9rem; font-weight:600; padding:.9rem 1.25rem; border-bottom:1px solid var(--border); display:flex; align-items:center; gap:.5rem; }
table { width:100%; border-collapse:collapse; font-size:.85rem; }
th { text-align:left; padding:.6rem 1.25rem; font-size:.7rem; color:var(--muted); text-transform:uppercase; letter-spacing:.04em; border-bottom:1px solid var(--border); }
td { padding:.65rem 1.25rem; border-bottom:1px solid var(--border); vertical-align:middle; }
tr:last-child td { border-bottom:none; }
tbody tr:hover { background:rgba(74,158,255,.05); }
.badge { display:inline-block; font-size:.7rem; padding:.15rem .5rem; border-radius:999px; font-weight:600; }
.badge-open { background:rgba(34,197,94,.15); color:var(--green); }
.empty { padding:2rem; text-align:center; color:var(--muted); font-size:.875rem; }

/* Search + inputs */
.toolbar { padding:.75rem 1.25rem; border-bottom:1px solid var(--border); display:flex; gap:.75rem; align-items:center; flex-wrap:wrap; }
input[type=search], input[type=number], select {
  background:var(--bg); border:1px solid var(--border); color:var(--text);
  border-radius:.4rem; padding:.4rem .75rem; font-size:.85rem; outline:none;
}
input[type=search]:focus, input[type=number]:focus { border-color:var(--accent); }
input[type=search] { min-width:200px; }
input[type=number] { width:90px; text-align:right; }
.save-btn { background:var(--accent); color:#fff; border:none; border-radius:.4rem; padding:.4rem .9rem; font-size:.8rem; cursor:pointer; font-weight:600; }
.save-btn:hover { opacity:.85; }
.save-btn:disabled { opacity:.4; cursor:not-allowed; }
.msg { font-size:.8rem; color:var(--green); }
.msg.err { color:var(--red); }

/* Reports bars */
.bar-row { display:flex; align-items:center; gap:.75rem; padding:.55rem 1.25rem; border-bottom:1px solid var(--border); font-size:.85rem; }
.bar-row:last-child { border-bottom:none; }
.bar-label { width:160px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.bar-track { flex:1; background:var(--border); border-radius:4px; height:8px; overflow:hidden; }
.bar-fill { height:100%; background:var(--accent); border-radius:4px; transition:width .3s; }
.bar-val { width:80px; text-align:right; color:var(--muted); font-size:.8rem; }
</style>
</head>
<body>
<header>
  <div class="status-dot" id="dot"></div>
  <h1>ViewTouch POS</h1>
  <span class="tag" id="version"></span>
  <span class="tag">Updated: <span id="last-updated">—</span></span>
</header>

<nav>
  <button class="active" onclick="tab('overview',this)">Overview</button>
  <button onclick="tab('menu',this)">Menu</button>
  <button onclick="tab('reports',this)">Reports</button>
</nav>

<!-- ───────────── OVERVIEW ───────────── -->
<div id="panel-overview" class="panel active">
  <div class="cards">
    <div class="card"><div class="label">Today's Sales</div><div class="value green" id="sales">—</div><div class="sub" id="checks-sub">—</div></div>
    <div class="card"><div class="label">Open Checks</div><div class="value orange" id="open-checks">—</div><div class="sub">awaiting payment</div></div>
    <div class="card"><div class="label">Staff Clocked In</div><div class="value accent" id="staff">—</div><div class="sub" id="staff-sub">—</div></div>
    <div class="card"><div class="label">System Uptime</div><div class="value" id="uptime">—</div></div>
  </div>

  <section>
    <h2>Open Checks</h2>
    <div id="checks-body"><div class="empty">Loading…</div></div>
  </section>

  <section>
    <h2>Staff Clocked In</h2>
    <div id="employees-body"><div class="empty">Loading…</div></div>
  </section>
</div>

<!-- ───────────── MENU ───────────── -->
<div id="panel-menu" class="panel">
  <section>
    <h2>Menu Items <span id="menu-count" style="font-weight:400;color:var(--muted);font-size:.8rem;"></span></h2>
    <div class="toolbar">
      <input type="search" id="menu-search" placeholder="Search items…" oninput="filterMenu()">
      <select id="menu-family" onchange="filterMenu()"><option value="">All families</option></select>
      <span class="msg" id="price-msg"></span>
    </div>
    <div id="menu-body"><div class="empty">Loading…</div></div>
  </section>
</div>

<!-- ───────────── REPORTS ───────────── -->
<div id="panel-reports" class="panel">
  <div class="cards" id="report-cards">
    <div class="card"><div class="label">Total Sales</div><div class="value green" id="rpt-total">—</div></div>
    <div class="card"><div class="label">Top Family</div><div class="value accent" id="rpt-top-family">—</div></div>
    <div class="card"><div class="label">Items Sold</div><div class="value" id="rpt-items-sold">—</div></div>
  </div>

  <section>
    <h2>Sales by Family</h2>
    <div id="rpt-family-body"><div class="empty">Loading…</div></div>
  </section>

  <section>
    <h2>Top 20 Items Today</h2>
    <div id="rpt-items-body"><div class="empty">Loading…</div></div>
  </section>
</div>

<script>
// ── Utilities ───────────────────────────────────────────────────────────────
function fmt(cents){return'$'+(cents/100).toLocaleString('en-US',{minimumFractionDigits:2,maximumFractionDigits:2});}
function fmtUptime(s){const h=Math.floor(s/3600),m=Math.floor((s%3600)/60);return h+'h '+String(m).padStart(2,'0')+'m';}
function dot(ok){document.getElementById('dot').style.background=ok?'var(--green)':'var(--red)';}

// ── Tabs ────────────────────────────────────────────────────────────────────
function tab(id,btn){
  document.querySelectorAll('.panel').forEach(p=>p.classList.remove('active'));
  document.querySelectorAll('nav button').forEach(b=>b.classList.remove('active'));
  document.getElementById('panel-'+id).classList.add('active');
  btn.classList.add('active');
  if(id==='menu' && !menuLoaded) loadMenu();
  if(id==='reports') loadReports();
}

// ── Overview ─────────────────────────────────────────────────────────────────
async function refreshOverview(){
  try{
    const [health,sales,checks,emps]=await Promise.all([
      fetch('/api/health').then(r=>r.json()),
      fetch('/api/sales').then(r=>r.json()),
      fetch('/api/checks').then(r=>r.json()),
      fetch('/api/employees').then(r=>r.json()),
    ]);
    dot(health.status==='ok');
    document.getElementById('version').textContent='v'+(health.version||'');
    document.getElementById('uptime').textContent=fmtUptime(health.uptime_seconds||0);
    document.getElementById('sales').textContent=fmt(sales.total_sales_cents||0);
    document.getElementById('checks-sub').textContent=(sales.closed_checks||0)+' closed · '+(sales.total_checks||0)+' total';
    document.getElementById('open-checks').textContent=sales.open_checks||0;
    document.getElementById('staff').textContent=emps.length;
    document.getElementById('staff-sub').textContent=emps.length===1?'employee':'employees';

    const cb=document.getElementById('checks-body');
    if(!checks||checks.length===0){cb.innerHTML='<div class="empty">No open checks</div>';}
    else{cb.innerHTML='<table><thead><tr><th>Table</th><th>Guests</th><th>Server</th><th>Opened</th><th>Balance</th><th>Status</th></tr></thead><tbody>'+
      checks.map(c=>`<tr><td>${c.table||'—'}</td><td>${c.guests||'—'}</td><td>${c.server||'—'}</td><td>${c.time_open||'—'}</td><td>${fmt(c.balance_cents||0)}</td><td><span class="badge badge-${c.status}">${c.status}</span></td></tr>`).join('')+
      '</tbody></table>';}

    const eb=document.getElementById('employees-body');
    if(!emps||emps.length===0){eb.innerHTML='<div class="empty">No employees clocked in</div>';}
    else{eb.innerHTML='<table><thead><tr><th>Name</th><th>Time In</th><th>Hours</th></tr></thead><tbody>'+
      emps.map(e=>`<tr><td>${e.name}</td><td>${e.clocked_in_at||'—'}</td><td>${e.hours_worked||'0:00'}</td></tr>`).join('')+
      '</tbody></table>';}

    document.getElementById('last-updated').textContent=new Date().toLocaleTimeString();
  }catch(err){dot(false);console.error(err);}
}

// ── Menu ─────────────────────────────────────────────────────────────────────
let allItems=[], menuLoaded=false;

async function loadMenu(){
  menuLoaded=true;
  document.getElementById('menu-body').innerHTML='<div class="empty">Loading…</div>';
  try{
    allItems=await fetch('/api/menu').then(r=>r.json());
    // Populate family filter
    const families=[...new Set(allItems.map(i=>i.family_name))].sort();
    const sel=document.getElementById('menu-family');
    families.forEach(f=>{const o=document.createElement('option');o.value=f;o.textContent=f;sel.appendChild(o);});
    document.getElementById('menu-count').textContent='('+allItems.length+' items)';
    filterMenu();
  }catch(e){document.getElementById('menu-body').innerHTML='<div class="empty">Error loading menu</div>';}
}

function filterMenu(){
  const q=document.getElementById('menu-search').value.toLowerCase();
  const fam=document.getElementById('menu-family').value;
  const items=allItems.filter(i=>
    (!q||i.name.toLowerCase().includes(q)||i.zone_name.toLowerCase().includes(q))&&
    (!fam||i.family_name===fam)
  );
  const body=document.getElementById('menu-body');
  if(!items.length){body.innerHTML='<div class="empty">No items match</div>';return;}
  body.innerHTML='<table><thead><tr><th>Name</th><th>Zone Name</th><th>Family</th><th>Price</th><th>Action</th></tr></thead><tbody>'+
    items.map(i=>`<tr id="row-${i.id}">
      <td>${i.name}</td>
      <td style="color:var(--muted)">${i.zone_name}</td>
      <td><span class="badge" style="background:rgba(74,158,255,.12);color:var(--accent)">${i.family_name}</span></td>
      <td><input type="number" id="price-${i.id}" value="${(i.price_cents/100).toFixed(2)}" min="0" step="0.01"></td>
      <td><button class="save-btn" onclick="savePrice(${i.id})">Save</button></td>
    </tr>`).join('')+
  '</tbody></table>';
}

async function savePrice(id){
  const input=document.getElementById('price-'+id);
  const cents=Math.round(parseFloat(input.value)*100);
  if(isNaN(cents)||cents<0){showMsg('Invalid price','err');return;}
  const btn=input.closest('tr').querySelector('.save-btn');
  btn.disabled=true;
  try{
    const res=await fetch('/api/menu/price',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id,price:cents})});
    const j=await res.json();
    if(j.ok){
      showMsg('Saved ✓');
      const item=allItems.find(i=>i.id===id);
      if(item) item.price_cents=cents;
    } else { showMsg(j.error||'Error','err'); }
  }catch(e){showMsg('Network error','err');}
  btn.disabled=false;
}

function showMsg(msg,cls=''){
  const el=document.getElementById('price-msg');
  el.textContent=msg; el.className='msg '+(cls||'');
  setTimeout(()=>{el.textContent='';},3000);
}

// ── Reports ───────────────────────────────────────────────────────────────────
async function loadReports(){
  try{
    const r=await fetch('/api/reports/today').then(r=>r.json());
    document.getElementById('rpt-total').textContent=fmt(r.total_sales_cents||0);
    document.getElementById('rpt-items-sold').textContent=r.total_items_sold||0;

    const families=r.by_family||[];
    const topFamily=families.length?families[0].name:'—';
    document.getElementById('rpt-top-family').textContent=topFamily;

    const maxFam=families.length?families[0].sales_cents:1;
    const fb=document.getElementById('rpt-family-body');
    if(!families.length){fb.innerHTML='<div class="empty">No data</div>';}
    else{fb.innerHTML=families.map(f=>`<div class="bar-row">
      <div class="bar-label">${f.name}</div>
      <div class="bar-track"><div class="bar-fill" style="width:${Math.round(f.sales_cents/maxFam*100)}%"></div></div>
      <div class="bar-val">${fmt(f.sales_cents)}</div>
    </div>`).join('');}

    const items=r.top_items||[];
    const ib=document.getElementById('rpt-items-body');
    if(!items.length){ib.innerHTML='<div class="empty">No data</div>';}
    else{ib.innerHTML='<table><thead><tr><th>Item</th><th>Qty</th><th>Sales</th></tr></thead><tbody>'+
      items.map(i=>`<tr><td>${i.name}</td><td>${i.qty}</td><td>${fmt(i.sales_cents)}</td></tr>`).join('')+
      '</tbody></table>';}
  }catch(e){console.error('Reports error',e);}
}

// ── Boot ─────────────────────────────────────────────────────────────────────
refreshOverview();
setInterval(refreshOverview, 30000);
</script>
</body>
</html>
)HTML";

// ─────────────────────────────────────────────────────────────────────────────
// Embedded HTML: Customer Display Unit (CDU)
// ─────────────────────────────────────────────────────────────────────────────
static const char kCduHtml[] = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ViewTouch — Customer Display</title>
<style>
*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
body {
  background: #0a0c10; color: #e2e8f0;
  font-family: "Noto Sans", "Liberation Sans", sans-serif;
  min-height: 100vh; display: flex; flex-direction: column;
  align-items: center; justify-content: center;
}
.logo { font-size: 2rem; font-weight: 700; color: #4a9eff; margin-bottom: 1rem; letter-spacing: -.02em; }
.sub  { font-size: 1rem; color: #64748b; }
.total-block { margin-top: 3rem; text-align: center; }
.total-label { font-size: .9rem; color: #64748b; text-transform: uppercase; letter-spacing: .1em; }
.total-amount { font-size: 4rem; font-weight: 800; color: #22c55e; line-height: 1.1; margin-top: .5rem; }
.items-list { margin-top: 2rem; max-width: 480px; width: 100%; }
.item-row { display: flex; justify-content: space-between; padding: .6rem 0; border-bottom: 1px solid #1e2433; font-size: 1rem; }
.item-row:last-child { border-bottom: none; }
.item-name { color: #e2e8f0; }
.item-price { color: #94a3b8; }
.thank-you { margin-top: 3rem; font-size: 1.25rem; color: #94a3b8; font-style: italic; }
</style>
</head>
<body>
<div class="logo">ViewTouch POS</div>
<div class="sub">Thank you for dining with us</div>
<div id="order-area">
  <div class="total-block">
    <div class="total-label">Your Total</div>
    <div class="total-amount" id="total">—</div>
  </div>
  <div class="items-list" id="items"></div>
</div>
<div class="thank-you">We appreciate your business</div>

<script>
function fmt(c){return'$'+(c/100).toLocaleString('en-US',{minimumFractionDigits:2,maximumFractionDigits:2});}
// Poll /api/checks every 5 s and display the most recently opened check
async function refresh(){
  try{
    const checks=await fetch('/api/checks').then(r=>r.json());
    if(!checks||!checks.length){
      document.getElementById('total').textContent='Welcome!';
      document.getElementById('items').innerHTML='';
      return;
    }
    // Show the check with the largest balance (i.e. currently being rung)
    const c=checks.reduce((a,b)=>(b.balance_cents>a.balance_cents?b:a));
    document.getElementById('total').textContent=fmt(c.balance_cents);
    if(c.table) document.getElementById('items').innerHTML=
      '<div class="item-row"><div class="item-name">Table '+c.table+'</div><div class="item-price">'+c.guests+' guest'+(c.guests!==1?'s':'')+'</div></div>';
  }catch(e){}
}
refresh();
setInterval(refresh,5000);
</script>
</body>
</html>
)HTML";

// ─────────────────────────────────────────────────────────────────────────────
// HTTP response helpers
// ─────────────────────────────────────────────────────────────────────────────
std::string WebAdminServer::jsonResponse(const std::string &body)
{
    std::ostringstream oss;
    oss << "HTTP/1.1 200 OK\r\n"
           "Content-Type: application/json\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Cache-Control: no-cache\r\n"
           "Connection: close\r\n"
           "Content-Length: " << body.size() << "\r\n\r\n"
        << body;
    return oss.str();
}

std::string WebAdminServer::htmlResponse(const std::string &body)
{
    std::ostringstream oss;
    oss << "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/html; charset=utf-8\r\n"
           "Cache-Control: no-cache\r\n"
           "Connection: close\r\n"
           "Content-Length: " << body.size() << "\r\n\r\n"
        << body;
    return oss.str();
}

std::string WebAdminServer::errorJson(const std::string &msg, int code)
{
    std::string body = "{\"ok\":false,\"error\":\"" + msg + "\"}";
    std::ostringstream oss;
    oss << "HTTP/1.1 " << code << " Error\r\n"
           "Content-Type: application/json\r\n"
           "Connection: close\r\n"
           "Content-Length: " << body.size() << "\r\n\r\n"
        << body;
    return oss.str();
}

std::string WebAdminServer::route404()
{
    static const char body[] = "{\"error\":\"not found\"}";
    std::ostringstream oss;
    oss << "HTTP/1.1 404 Not Found\r\n"
           "Content-Type: application/json\r\n"
           "Connection: close\r\n"
           "Content-Length: " << (sizeof(body) - 1) << "\r\n\r\n"
        << body;
    return oss.str();
}

std::string WebAdminServer::route405()
{
    static const char body[] = "{\"error\":\"method not allowed\"}";
    std::ostringstream oss;
    oss << "HTTP/1.1 405 Method Not Allowed\r\n"
           "Content-Type: application/json\r\n"
           "Connection: close\r\n"
           "Content-Length: " << (sizeof(body) - 1) << "\r\n\r\n"
        << body;
    return oss.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Route: GET /
// ─────────────────────────────────────────────────────────────────────────────
std::string WebAdminServer::routeRoot()
{
    return htmlResponse(kDashboardHtml);
}

// ─────────────────────────────────────────────────────────────────────────────
// Route: GET /cdu  — customer-facing display
// ─────────────────────────────────────────────────────────────────────────────
std::string WebAdminServer::routeCdu()
{
    return htmlResponse(kCduHtml);
}

// ─────────────────────────────────────────────────────────────────────────────
// Route: GET /api/health
// ─────────────────────────────────────────────────────────────────────────────
std::string WebAdminServer::routeHealth()
{
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - s_start_time).count();
    nlohmann::json j;
    j["status"]         = "ok";
    j["uptime_seconds"] = static_cast<long long>(secs);
    j["version"]        = viewtouch::get_version_short();
    return jsonResponse(j.dump());
}

// ─────────────────────────────────────────────────────────────────────────────
// Route: GET /api/sales
// ─────────────────────────────────────────────────────────────────────────────
std::string WebAdminServer::routeSales()
{
    long long total_sales = 0;
    int open_checks = 0, closed_checks = 0;

    if (MasterSystem) {
        for (Check *c = MasterSystem->CheckList(); c; c = c->next) {
            for (SubCheck *sc = c->SubList(); sc; sc = sc->next) {
                total_sales += sc->total_sales;
                if (sc->status == CHECK_OPEN)       ++open_checks;
                else if (sc->status == CHECK_CLOSED) ++closed_checks;
            }
        }
    }

    nlohmann::json j;
    j["total_sales_cents"] = total_sales;
    j["open_checks"]       = open_checks;
    j["closed_checks"]     = closed_checks;
    j["total_checks"]      = open_checks + closed_checks;
    return jsonResponse(j.dump());
}

// ─────────────────────────────────────────────────────────────────────────────
// Route: GET /api/checks
// ─────────────────────────────────────────────────────────────────────────────
std::string WebAdminServer::routeChecks()
{
    nlohmann::json arr = nlohmann::json::array();
    if (!MasterSystem) return jsonResponse(arr.dump());

    for (Check *c = MasterSystem->CheckList(); c; c = c->next) {
        for (SubCheck *sc = c->SubList(); sc; sc = sc->next) {
            if (sc->status != CHECK_OPEN) continue;

            std::string server_name;
            if (c->user_owner > 0) {
                for (Employee *e = MasterSystem->user_db.UserList(); e; e = e->next) {
                    if (e->key == c->user_owner) {
                        server_name = std::string(e->first_name.Value()) + " " +
                                      std::string(e->last_name.Value());
                        break;
                    }
                }
            }

            char time_buf[16] = "—";
            if (c->time_open.IsSet())
                std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d",
                              c->time_open.Hour(), c->time_open.Min());

            nlohmann::json entry;
            entry["table"]         = c->Table() ? std::string(c->Table()) : "";
            entry["guests"]        = c->Guests();
            entry["server"]        = server_name;
            entry["time_open"]     = time_buf;
            entry["balance_cents"] = sc->total_cost - sc->payment;
            entry["status"]        = "open";
            arr.push_back(entry);
        }
    }
    return jsonResponse(arr.dump());
}

// ─────────────────────────────────────────────────────────────────────────────
// Route: GET /api/employees
// ─────────────────────────────────────────────────────────────────────────────
std::string WebAdminServer::routeEmployees()
{
    nlohmann::json arr = nlohmann::json::array();
    if (!MasterSystem) return jsonResponse(arr.dump());

    for (WorkEntry *we = MasterSystem->work_db.WorkList(); we; we = we->next) {
        if (we->IsWorkDone()) continue;

        std::string name;
        for (Employee *e = MasterSystem->user_db.UserList(); e; e = e->next) {
            if (e->key == we->user_id) {
                name = std::string(e->first_name.Value()) + " " +
                       std::string(e->last_name.Value());
                break;
            }
        }

        char in_buf[16] = "—";
        if (we->start.IsSet())
            std::snprintf(in_buf, sizeof(in_buf), "%02d:%02d",
                          we->start.Hour(), we->start.Min());

        int mins = we->MinutesWorked();
        char hours_buf[16];
        std::snprintf(hours_buf, sizeof(hours_buf), "%d:%02d", mins / 60, mins % 60);

        nlohmann::json entry;
        entry["name"]          = name;
        entry["clocked_in_at"] = in_buf;
        entry["hours_worked"]  = hours_buf;
        arr.push_back(entry);
    }
    return jsonResponse(arr.dump());
}

// ─────────────────────────────────────────────────────────────────────────────
// Route: GET /api/menu
// ─────────────────────────────────────────────────────────────────────────────
std::string WebAdminServer::routeMenu()
{
    nlohmann::json arr = nlohmann::json::array();
    if (!MasterSystem) return jsonResponse(arr.dump());

    for (SalesItem *si = MasterSystem->menu.ItemList(); si; si = si->next) {
        nlohmann::json item;
        item["id"]          = si->id;
        item["name"]        = si->item_name.Value() ? si->item_name.Value() : "";
        item["zone_name"]   = si->zone_name.Value()  ? si->zone_name.Value()  : "";
        item["print_name"]  = si->print_name.Value() ? si->print_name.Value() : "";
        item["price_cents"] = si->cost;
        item["family"]      = static_cast<int>(si->family);
        item["family_name"] = FamilyName(si->family);
        item["type"]        = static_cast<int>(si->type);
        item["out_of_stock"]= si->out_of_stock != 0;
        arr.push_back(item);
    }
    return jsonResponse(arr.dump());
}

// ─────────────────────────────────────────────────────────────────────────────
// Route: POST /api/menu/price  body: {"id": N, "price": P}
// ─────────────────────────────────────────────────────────────────────────────
std::string WebAdminServer::routeMenuPriceUpdate(const std::string &body)
{
    int item_id  = 0;
    int new_price = -1;

    try {
        auto j    = nlohmann::json::parse(body);
        item_id   = j.at("id").get<int>();
        new_price = j.at("price").get<int>();
    } catch (...) {
        return errorJson("invalid JSON — expected {\"id\":N,\"price\":P}");
    }

    if (new_price < 0)
        return errorJson("price must be >= 0 cents");

    if (!MasterSystem)
        return errorJson("POS not ready", 503);

    std::lock_guard<std::mutex> lk(write_mtx_);
    SalesItem *found = MasterSystem->menu.FindByID(item_id);
    if (!found)
        return errorJson("item not found");

    found->cost         = new_price;
    MasterSystem->menu.changed = 1;   // trigger save on next main-loop tick

    nlohmann::json j;
    j["ok"]         = true;
    j["id"]         = item_id;
    j["price_cents"]= new_price;
    return jsonResponse(j.dump());
}

// ─────────────────────────────────────────────────────────────────────────────
// Route: GET /api/reports/today
// ─────────────────────────────────────────────────────────────────────────────
std::string WebAdminServer::routeReportsToday()
{
    // Aggregate sales from live checks by family and by item name
    std::map<int, long long>        family_sales;   // family → total cents
    std::map<std::string, std::pair<int,long long>> item_agg; // name → {qty, cents}
    long long total_sales  = 0;
    int       total_items  = 0;
    long long cc_total     = 0;   // TENDER_CREDIT_CARD + TENDER_DEBIT_CARD
    long long cash_total   = 0;   // TENDER_CASH
    long long other_total  = 0;   // everything else

    if (MasterSystem) {
        for (Check *c = MasterSystem->CheckList(); c; c = c->next) {
            for (SubCheck *sc = c->SubList(); sc; sc = sc->next) {
                // Aggregate payments by tender type
                for (Payment *pay = sc->PaymentList(); pay; pay = pay->next) {
                    long long amt = static_cast<long long>(pay->amount);
                    if (pay->tender_type == TENDER_CREDIT_CARD ||
                        pay->tender_type == TENDER_DEBIT_CARD)
                        cc_total   += amt;
                    else if (pay->tender_type == TENDER_CASH)
                        cash_total += amt;
                    else if (pay->tender_type != TENDER_CHANGE &&
                             pay->tender_type != TENDER_OVERAGE)
                        other_total += amt;
                }
                // Walk ordered items
                for (Order *ord = sc->OrderList(); ord; ord = ord->next) {
                    // Order holds SalesItem reference via item_name matching
                    const char *iname = ord->item_name.Value();
                    if (!iname || !iname[0]) continue;

                    int price = ord->cost;
                    int qty   = ord->count;

                    // Try to look up family from menu
                    int fam = 255;
                    SalesItem *si = MasterSystem->menu.FindByName(iname);
                    if (si) fam = si->family;

                    family_sales[fam]  += static_cast<long long>(price) * qty;
                    total_sales        += static_cast<long long>(price) * qty;
                    total_items        += qty;

                    auto &agg = item_agg[iname];
                    agg.first  += qty;
                    agg.second += static_cast<long long>(price) * qty;
                }
            }
        }
    }

    // Build by_family array sorted by sales descending
    std::vector<std::pair<int,long long>> fam_vec(family_sales.begin(), family_sales.end());
    std::sort(fam_vec.begin(), fam_vec.end(),
              [](const auto &a, const auto &b){ return a.second > b.second; });

    nlohmann::json by_family = nlohmann::json::array();
    for (auto &[fam, cents] : fam_vec) {
        nlohmann::json e;
        e["family"]      = fam;
        e["name"]        = FamilyName(fam);
        e["sales_cents"] = cents;
        by_family.push_back(e);
    }

    // Build top_items array sorted by sales descending, limited to 20
    std::vector<std::tuple<std::string,int,long long>> item_vec;
    item_vec.reserve(item_agg.size());
    for (auto &[name, p] : item_agg)
        item_vec.emplace_back(name, p.first, p.second);
    std::sort(item_vec.begin(), item_vec.end(),
              [](const auto &a, const auto &b){ return std::get<2>(a) > std::get<2>(b); });
    if (item_vec.size() > 20) item_vec.resize(20);

    nlohmann::json top_items = nlohmann::json::array();
    for (auto &[name, qty, cents] : item_vec) {
        nlohmann::json e;
        e["name"]        = name;
        e["qty"]         = qty;
        e["sales_cents"] = cents;
        top_items.push_back(e);
    }

    nlohmann::json j;
    j["total_sales_cents"]     = total_sales;
    j["total_items_sold"]      = total_items;
    j["by_family"]             = by_family;
    j["top_items"]             = top_items;
    j["payments"]["cc_cents"]  = cc_total;
    j["payments"]["cash_cents"] = cash_total;
    j["payments"]["other_cents"] = other_total;
    return jsonResponse(j.dump());
}

// Route: GET /api/inventory
// Returns all items with item_count >= 0 (tracked items), sorted by count
// ascending (lowest stock first), plus all 86'd items.
std::string WebAdminServer::routeInventory()
{
    nlohmann::json tracked  = nlohmann::json::array();
    nlohmann::json eightysix = nlohmann::json::array();

    if (MasterSystem) {
        // Collect all items with count tracking or 86 status
        std::vector<SalesItem*> counted_items;
        for (SalesItem *si = MasterSystem->menu.ItemList(); si; si = si->next) {
            if (si->item_count >= 0 || si->out_of_stock)
                counted_items.push_back(si);
        }
        // Sort: items with count first (lowest count first), then unlimited
        std::sort(counted_items.begin(), counted_items.end(),
            [](SalesItem *a, SalesItem *b) {
                if (a->item_count >= 0 && b->item_count >= 0)
                    return a->item_count < b->item_count;
                return a->item_count >= 0;
            });

        for (SalesItem *si : counted_items) {
            nlohmann::json e;
            e["id"]           = si->id;
            e["name"]         = si->item_name.Value();
            e["item_count"]   = si->item_count;
            e["out_of_stock"] = si->out_of_stock != 0;
            if (si->out_of_stock)
                eightysix.push_back(e);
            else
                tracked.push_back(e);
        }
    }

    nlohmann::json j;
    j["tracked"]    = tracked;    // items with count > 0, sorted lowest first
    j["eightysix"]  = eightysix;  // items at 0 or manually 86'd
    return jsonResponse(j.dump());
}

// Route: POST /api/inventory/count
// Body: {"id": N, "count": K}  — set item_count for item N to K
// count = -1 to remove tracking; count = 0 = 86'd
std::string WebAdminServer::routeInventorySetCount(const std::string &body)
{
    int item_id = -1, new_count = -1;
    try {
        auto j = nlohmann::json::parse(body);
        item_id   = j.value("id",    -1);
        new_count = j.value("count", -1);
    } catch (...) {
        return jsonResponse(R"({"error":"invalid json"})");
    }
    if (item_id < 0)
        return jsonResponse(R"({"error":"missing id"})");

    std::lock_guard<std::mutex> lk(write_mtx_);
    if (!MasterSystem)
        return jsonResponse(R"({"error":"no system"})");

    for (SalesItem *si = MasterSystem->menu.ItemList(); si; si = si->next) {
        if (si->id == item_id) {
            si->item_count = new_count;
            // Sync out_of_stock flag
            if (new_count == 0)
                si->out_of_stock = 1;
            else if (new_count > 0 || new_count == -1)
                si->out_of_stock = 0;
            MasterSystem->menu.changed = 1;
            nlohmann::json r;
            r["id"]         = si->id;
            r["name"]       = si->item_name.Value();
            r["item_count"] = si->item_count;
            r["out_of_stock"] = si->out_of_stock != 0;
            return jsonResponse(r.dump());
        }
    }
    return jsonResponse(R"({"error":"item not found"})");
}

// ─────────────────────────────────────────────────────────────────────────────
// HTTP request dispatcher — parses method, path, body; calls route handler
// ─────────────────────────────────────────────────────────────────────────────
void WebAdminServer::handle(int client_fd) noexcept
{
    // Read request (up to 8 KB headers + body)
    std::string request;
    request.reserve(4096);
    char tmp[1024];
    while (true) {
        int n = static_cast<int>(recv(client_fd, tmp, sizeof(tmp), 0));
        if (n <= 0) break;
        request.append(tmp, static_cast<size_t>(n));
        // Stop when we have a blank line (end of headers) or >= 8 KB
        if (request.find("\r\n\r\n") != std::string::npos || request.size() >= 8192)
            break;
    }

    if (request.empty()) { close(client_fd); return; }

    // Parse first line: METHOD PATH HTTP/x.x
    char method[16] = {}, rawpath[512] = {};
    std::sscanf(request.c_str(), "%15s %511s", method, rawpath);

    // Strip query string from path
    std::string path(rawpath);
    {
        auto q = path.find('?');
        if (q != std::string::npos) path = path.substr(0, q);
    }

    // Extract body (after \r\n\r\n) for POST requests
    std::string body;
    {
        auto sep = request.find("\r\n\r\n");
        if (sep != std::string::npos)
            body = request.substr(sep + 4);
    }

    bool is_get  = (std::strcmp(method, "GET")  == 0);
    bool is_post = (std::strcmp(method, "POST") == 0);

    std::string response;

    if (path == "/" || path == "/index.html") {
        if (!is_get) { response = route405(); }
        else         { response = routeRoot(); }
    } else if (path == "/cdu") {
        if (!is_get) { response = route405(); }
        else         { response = routeCdu(); }
    } else if (path == "/api/health") {
        if (!is_get) { response = route405(); }
        else         { response = routeHealth(); }
    } else if (path == "/api/sales") {
        if (!is_get) { response = route405(); }
        else         { response = routeSales(); }
    } else if (path == "/api/checks") {
        if (!is_get) { response = route405(); }
        else         { response = routeChecks(); }
    } else if (path == "/api/employees") {
        if (!is_get) { response = route405(); }
        else         { response = routeEmployees(); }
    } else if (path == "/api/menu") {
        if (!is_get) { response = route405(); }
        else         { response = routeMenu(); }
    } else if (path == "/api/menu/price") {
        if (!is_post) { response = route405(); }
        else          { response = routeMenuPriceUpdate(body); }
    } else if (path == "/api/reports/today") {
        if (!is_get) { response = route405(); }
        else         { response = routeReportsToday(); }
    } else if (path == "/api/inventory") {
        if (!is_get) { response = route405(); }
        else         { response = routeInventory(); }
    } else if (path == "/api/inventory/count") {
        if (!is_post) { response = route405(); }
        else          { response = routeInventorySetCount(body); }
    } else {
        response = route404();
    }

    const char *ptr = response.c_str();
    size_t left     = response.size();
    while (left > 0) {
        ssize_t sent = ::send(client_fd, ptr, left, MSG_NOSIGNAL);
        if (sent <= 0) break;
        ptr  += sent;
        left -= static_cast<size_t>(sent);
    }
    close(client_fd);
}

// ─────────────────────────────────────────────────────────────────────────────
// Server event loop (background thread)
// ─────────────────────────────────────────────────────────────────────────────
void WebAdminServer::serve()
{
    while (running_.load()) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(server_fd_, &rfds);
        struct timeval tv{1, 0};
        if (select(server_fd_ + 1, &rfds, nullptr, nullptr, &tv) <= 0) continue;

        struct sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        int client = ::accept(server_fd_,
                              reinterpret_cast<struct sockaddr *>(&addr), &len);
        if (client < 0) continue;
        handle(client);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Start / Stop
// ─────────────────────────────────────────────────────────────────────────────
WebAdminServer::WebAdminServer(int port) : port_(port)
{
    s_start_time = std::chrono::steady_clock::now();
}

WebAdminServer::~WebAdminServer()
{
    Stop();
}

void WebAdminServer::Start()
{
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        vt::Logger::error("WebAdmin: socket() failed — dashboard unavailable");
        return;
    }

    int yes = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(port_));

    if (::bind(server_fd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        vt::Logger::error("WebAdmin: bind() on port {} failed", port_);
        close(server_fd_);
        server_fd_ = -1;
        return;
    }

    ::listen(server_fd_, 16);
    running_.store(true);
    thread_ = std::thread(&WebAdminServer::serve, this);
    vt::Logger::info("WebAdmin: http://localhost:{}/ | CDU: http://localhost:{}/cdu",
                     port_, port_);
}

void WebAdminServer::Stop()
{
    if (!running_.load()) return;
    running_.store(false);
    if (server_fd_ >= 0) { close(server_fd_); server_fd_ = -1; }
    if (thread_.joinable()) thread_.join();
}

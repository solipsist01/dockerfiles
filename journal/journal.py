from flask import Flask, request, render_template_string, redirect, url_for, send_from_directory
import sqlite3
import os
from datetime import datetime
from werkzeug.utils import secure_filename

app = Flask(__name__)

DB_PATH = "data/journal.db"
UPLOAD_FOLDER = "data/screenshots"
ALLOWED_EXTENSIONS = {"png", "jpg", "jpeg", "gif"}

app.config["UPLOAD_FOLDER"] = UPLOAD_FOLDER

os.makedirs("data", exist_ok=True)
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

def allowed_file(filename):
    return "." in filename and filename.rsplit(".", 1)[1].lower() in ALLOWED_EXTENSIONS

# ---------------- DB INIT ----------------
conn = sqlite3.connect(DB_PATH)
conn.execute("""
CREATE TABLE IF NOT EXISTS trades (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    date TEXT,
    symbol TEXT,
    direction TEXT,
    entry REAL,
    stop REAL,
    exit REAL,
    position_size REAL,
    pnl REAL,
    r_multiple REAL,
    notes TEXT,
    screenshot TEXT
)
""")
conn.close()

# ---------------- HTML ----------------
HTML = """
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>Trading Journal</title>
<style>
body { font-family: system-ui; background:#0f172a; color:#e5e7eb; padding:20px; }
h1 { text-align:center; margin-bottom:5px; }
.summary {
  max-width:1200px; margin:0 auto 10px auto;
  display:flex; justify-content:space-between; align-items:center;
}
.summary a { color:#facc15; text-decoration:none; font-weight:bold; }
.summary a:hover { text-decoration:underline; }

form { max-width:650px; margin:20px auto; background:#1e293b; padding:15px; border-radius:8px; }
input, select, textarea, button {
  width:100%; margin-bottom:10px; padding:8px;
  background:#0f172a; border:1px solid #334155; color:#e5e7eb; border-radius:5px;
}
button { background:#22c55e; color:#000; font-weight:bold; cursor:pointer; }
table { width:100%; max-width:1200px; margin:auto; border-collapse:collapse; }
th, td { padding:8px; text-align:center; border-bottom:1px solid #334155; font-size:0.85rem; }
th { background:#1e293b; }
tr:nth-child(even) { background:#111827; }
td.pnl { font-weight:bold; }
button.delete { background:none; border:none; color:#ef4444; font-size:1.8rem; cursor:pointer; }
button.edit { background:none; border:none; color:#facc15; font-size:1.3rem; cursor:pointer; }

img.thumb { max-width:100px; cursor:pointer; border-radius:4px; }

.modal { display:none; position:fixed; inset:0; background:rgba(0,0,0,.9); }
.modal img { display:block; max-width:90%; max-height:90%; margin:auto; margin-top:5%; }
.modal span { position:absolute; top:20px; right:30px; font-size:40px; cursor:pointer; }
</style>
</head>

<body>

<h1>Trading Journal</h1>

<div class="summary">
  <div>Total Net Profit: <b>{{ "%.2f"|format(total_profit) }} USDT</b></div>
  <div>
    <a href="/position_size_calculator.html" target="_blank">
      Open Position Size Calculator
    </a>
  </div>
</div>

<form method="post" action="{{ form_action }}" enctype="multipart/form-data">
{% if edit_trade %}
<input type="hidden" name="id" value="{{ edit_trade['id'] }}">
{% endif %}
<input type="date" name="date" value="{{ edit_trade['date'] if edit_trade else current_date }}" required>
<input name="symbol" placeholder="Symbol" value="{{ edit_trade['symbol'] if edit_trade else '' }}" required>
<select name="direction">
  <option value="long" {% if edit_trade and edit_trade['direction']=='long' %}selected{% endif %}>Long</option>
  <option value="short" {% if edit_trade and edit_trade['direction']=='short' %}selected{% endif %}>Short</option>
</select>
<input name="entry" type="number" step="0.0001" placeholder="Entry" value="{{ edit_trade['entry'] if edit_trade else '' }}" required>
<input name="stop" type="number" step="0.0001" placeholder="Stop" value="{{ edit_trade['stop'] if edit_trade else '' }}" required>
<input name="exit" type="number" step="0.0001" placeholder="Exit (optional)" value="{{ edit_trade['exit'] if edit_trade and edit_trade['exit'] is not none else '' }}">
<input name="position_size" type="number" step="0.01" placeholder="Position Size (USDT)" value="{{ edit_trade['position_size'] if edit_trade else '' }}" required>
<textarea name="notes" placeholder="Notes">{{ edit_trade['notes'] if edit_trade else '' }}</textarea>
<input type="file" name="screenshot" accept="image/*">
<button>{{ "Edit Trade" if edit_trade else "Add Trade" }}</button>
</form>

<table>
<tr>
<th>Date</th><th>Symbol</th><th>Dir</th><th>Entry</th><th>Stop</th><th>Exit</th>
<th>Size</th><th>PnL</th><th>R</th><th>Notes</th><th>Img</th><th></th>
</tr>

{% for t in trades %}
<tr>
<td>{{ t.date }}</td>
<td>{{ t.symbol }}</td>
<td>{{ t.direction }}</td>
<td>{{ "{:g}".format(t.entry) }}</td>
<td>{{ "{:g}".format(t.stop) }}</td>
<td>{{ "{:g}".format(t.exit) if t.exit is not none else "" }}</td>
<td>{{ "%.2f"|format(t.position_size) }}</td>
<td class="pnl" style="color:{{ '#22c55e' if t.pnl>=0 else '#ef4444' }}">{{ "%.2f"|format(t.pnl) }}</td>
<td>{{ "%.2f"|format(t.r_multiple) }}R</td>
<td>{{ t.notes }}</td>
<td>
{% if t.screenshot %}
<img src="/screenshots/{{ t.screenshot }}" class="thumb" onclick="showImg(this.src)">
{% endif %}
</td>
<td>
<form method="post" action="/edit" style="display:inline;">
<input type="hidden" name="id" value="{{ t.id }}">
<button class="edit">&#9998;</button>
</form>
<form method="post" action="/delete" style="display:inline;">
<input type="hidden" name="id" value="{{ t.id }}">
<button class="delete">&times;</button>
</form>
</td>
</tr>
{% endfor %}
</table>

<div id="modal" class="modal" onclick="this.style.display='none'">
<span>&times;</span>
<img id="modalImg">
</div>

<script>
function showImg(src){
  document.getElementById("modalImg").src = src;
  document.getElementById("modal").style.display = "block";
}
</script>

</body>
</html>
"""

# ---------------- HELPERS ----------------
def get_trades():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    rows = conn.execute("SELECT * FROM trades ORDER BY date DESC").fetchall()
    conn.close()
    return rows

# ---------------- ROUTES ----------------
@app.route("/")
def index():
    trades = get_trades()
    total_profit = sum(t["pnl"] for t in trades)
    return render_template_string(
        HTML,
        trades=trades,
        total_profit=total_profit,
        current_date=datetime.now().strftime("%Y-%m-%d"),
        edit_trade=None,
        form_action="/add"
    )

@app.route("/position_size_calculator.html")
def calculator():
    with open("position_size_calculator.html") as f:
        return f.read()

@app.route("/add", methods=["POST"])
def add():
    date = request.form["date"]
    symbol = request.form["symbol"]
    direction = request.form["direction"]
    entry = float(request.form["entry"])
    stop = float(request.form["stop"])
    pos_size = float(request.form["position_size"])
    notes = request.form.get("notes","")

    exit_raw = request.form.get("exit")
    exit_price = float(exit_raw) if exit_raw else None

    pnl = 0
    r_multiple = 0
    if exit_price is not None:
        pnl = pos_size * ((exit_price-entry)/entry if direction=="long" else (entry-exit_price)/entry)
        risk = pos_size * abs(entry-stop)/entry
        r_multiple = pnl/risk if risk else 0

    filename = None
    f = request.files.get("screenshot")
    if f and allowed_file(f.filename):
        filename = secure_filename(f.filename)
        f.save(os.path.join(UPLOAD_FOLDER, filename))

    conn = sqlite3.connect(DB_PATH)
    conn.execute("""
    INSERT INTO trades VALUES (NULL,?,?,?,?,?,?,?,?,?,?,?)
    """,(date,symbol,direction,entry,stop,exit_price,pos_size,pnl,r_multiple,notes,filename))
    conn.commit()
    conn.close()
    return redirect("/")

@app.route("/edit", methods=["POST"])
def edit():
    tid = request.form["id"]
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    trade = conn.execute("SELECT * FROM trades WHERE id=?", (tid,)).fetchone()
    conn.close()

    trades = get_trades()
    total_profit = sum(t["pnl"] for t in trades)

    return render_template_string(
        HTML,
        trades=trades,
        total_profit=total_profit,
        current_date=datetime.now().strftime("%Y-%m-%d"),
        edit_trade=trade,
        form_action="/update"
    )

@app.route("/update", methods=["POST"])
def update():
    tid = request.form["id"]
    date = request.form["date"]
    symbol = request.form["symbol"]
    direction = request.form["direction"]
    entry = float(request.form["entry"])
    stop = float(request.form["stop"])
    pos_size = float(request.form["position_size"])
    notes = request.form.get("notes","")

    exit_raw = request.form.get("exit")
    exit_price = float(exit_raw) if exit_raw else None

    pnl = 0
    r_multiple = 0
    if exit_price is not None:
        pnl = pos_size * ((exit_price-entry)/entry if direction=="long" else (entry-exit_price)/entry)
        risk = pos_size * abs(entry-stop)/entry
        r_multiple = pnl/risk if risk else 0

    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    trade = conn.execute("SELECT screenshot FROM trades WHERE id=?", (tid,)).fetchone()
    filename = trade["screenshot"] if trade else None

    f = request.files.get("screenshot")
    if f and allowed_file(f.filename):
        filename = secure_filename(f.filename)
        f.save(os.path.join(UPLOAD_FOLDER, filename))

    conn.execute("""
    UPDATE trades SET
    date=?,symbol=?,direction=?,entry=?,stop=?,exit=?,position_size=?,pnl=?,r_multiple=?,notes=?,screenshot=?
    WHERE id=?
    """,(date,symbol,direction,entry,stop,exit_price,pos_size,pnl,r_multiple,notes,filename,tid))
    conn.commit()
    conn.close()
    return redirect("/")

@app.route("/delete", methods=["POST"])
def delete():
    tid = request.form["id"]
    conn = sqlite3.connect(DB_PATH)
    conn.execute("DELETE FROM trades WHERE id=?", (tid,))
    conn.commit()
    conn.close()
    return redirect("/")

@app.route("/screenshots/<name>")
def screenshots(name):
    return send_from_directory(UPLOAD_FOLDER, name)

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=80)

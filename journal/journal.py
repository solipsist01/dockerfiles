from flask import Flask, request, render_template_string, redirect, url_for
import sqlite3
import os
from datetime import datetime

app = Flask(__name__)
DB_PATH = "data/journal.db"

os.makedirs("data", exist_ok=True)

# Initialize DB
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
    notes TEXT
)
""")
conn.close()

HTML = """
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Trading Journal</title>
<style>
body { font-family: 'Segoe UI', Roboto, system-ui; background:#0f172a; color:#e5e7eb; padding:20px; }
h1 { text-align:center; margin-bottom:5px; }
.summary { max-width:1000px; margin:0 auto 10px auto; display:flex; justify-content: space-between; font-size:1rem; }
.summary span { color:#a5b4fc; font-weight:bold; }
.summary a { color:#facc15; text-decoration:none; font-weight:bold; }
.summary a:hover { text-decoration:underline; }
form { max-width: 650px; margin:20px auto; background:#1e293b; padding:15px; border-radius:8px; box-shadow:0 0 15px rgba(0,0,0,0.3);}
input, select, textarea, button { width:100%; margin-bottom:10px; padding:8px; background:#0f172a; border:1px solid #334155; color:#e5e7eb; border-radius:5px; font-size:0.9rem;}
button { background:#22c55e; color:#000; font-weight:bold; cursor:pointer; transition:0.2s;}
button:hover { background:#16a34a; }
table { width:100%; border-collapse:collapse; max-width:1000px; margin:20px auto 50px auto; table-layout:fixed; word-wrap:break-word; }
th, td { padding:8px 6px; text-align:center; border-bottom:1px solid #334155; font-size:0.85rem; }
th { background:#1e293b; }
tr:nth-child(even) { background:#111827; }
td.notes { text-align:left; }
td.pnl { font-weight:bold; }
button.delete { background:none; color:#ef4444; border:none; font-size:1.8rem; font-weight:bold; cursor:pointer; transition:0.2s; line-height:1; padding:0;}
button.delete:hover { color:#b91c1c; }
button.edit { background:none; color:#facc15; border:none; font-size:1.2rem; cursor:pointer; transition:0.2s; line-height:1; padding:0;}
button.edit:hover { color:#eab308; }
</style>
</head>
<body>

<h1>Trading Journal</h1>
<div class="summary">
    <span>Total Net Profit: {{ "%.2f"|format(total_profit) }} USDT</span>
    <a href="/position_size_calculator.html" target="_blank">Open Position Size Calculator</a>
</div>

<form method="post" action="{{ form_action }}">
    {% if edit_trade %}
    <input type="hidden" name="id" value="{{ edit_trade['id'] }}">
    {% endif %}
    <input type="date" name="date" required value="{{ edit_trade['date'] if edit_trade else current_date }}">
    <input type="text" name="symbol" placeholder="Symbol (e.g. BTCUSDT)" value="{{ edit_trade['symbol'] if edit_trade else '' }}" required>
    <select name="direction">
        <option value="long" {% if edit_trade and edit_trade['direction']=='long' %}selected{% endif %}>Long</option>
        <option value="short" {% if edit_trade and edit_trade['direction']=='short' %}selected{% endif %}>Short</option>
    </select>
    <input type="number" step="0.0001" name="entry" placeholder="Entry Price" value="{{ edit_trade['entry'] if edit_trade else '' }}">
    <input type="number" step="0.0001" name="stop" placeholder="Stop Loss" value="{{ edit_trade['stop'] if edit_trade else '' }}">
    <input type="number" step="0.0001" name="exit" placeholder="Exit Price" value="{{ edit_trade['exit'] if edit_trade else '' }}">
    <input type="number" step="0.01" name="position_size" placeholder="Position Size (USDT)" value="{{ edit_trade['position_size'] if edit_trade else '' }}">
    <textarea name="notes" placeholder="Notes">{{ edit_trade['notes'] if edit_trade else '' }}</textarea>
    <button>{{ 'Edit Trade' if edit_trade else 'Add Trade' }}</button>
</form>

<table>
<tr>
<th>Date</th><th>Symbol</th><th>Dir</th><th>Entry</th><th>Stop</th><th>Exit</th><th>Pos Size (USDT)</th><th>PnL (USDT)</th><th>R</th><th>Notes</th><th></th>
</tr>
{% for t in trades %}
<tr>
<td>{{ t['date'] }}</td>
<td>{{ t['symbol'] }}</td>
<td>{{ t['direction'] }}</td>
<td>{{ "{:g}".format(t['entry']) }}</td>
<td>{{ "{:g}".format(t['stop']) }}</td>
<td>{{ "{:g}".format(t['exit']) }}</td>
<td>{{ "%.2f"|format(t['position_size']) }}</td>
<td class="pnl" style="color:{{ '#22c55e' if t['pnl']>=0 else '#ef4444' }}">{{ "%.2f"|format(t['pnl']) }}</td>
<td>{{ "%.2f"|format(t['r_multiple']) }}R</td>
<td class="notes">{{ t['notes'] }}</td>
<td>
<form method="post" action="/edit" style="display:inline;">
<input type="hidden" name="id" value="{{ t['id'] }}">
<button type="submit" class="edit">&#9998;</button>
</form>
<form method="post" action="/delete" style="display:inline;">
<input type="hidden" name="id" value="{{ t['id'] }}">
<button type="submit" class="delete">&times;</button>
</form>
</td>
</tr>
{% endfor %}
</table>

</body>
</html>
"""

def get_trades():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    trades = conn.execute("SELECT * FROM trades ORDER BY id DESC").fetchall()
    conn.close()
    return trades

@app.route("/", methods=["GET"])
def index():
    trades = get_trades()
    total_profit = sum(t['pnl'] for t in trades)
    current_date = datetime.now().strftime("%Y-%m-%d")
    return render_template_string(HTML, trades=trades, current_date=current_date,
                                  total_profit=total_profit, edit_trade=None, form_action="/add")

@app.route("/add", methods=["POST"])
def add():
    date = request.form.get("date") or datetime.now().strftime("%Y-%m-%d")
    symbol = request.form.get("symbol")
    direction = request.form.get("direction")
    entry = float(request.form.get("entry"))
    stop = float(request.form.get("stop"))
    exit_price = float(request.form.get("exit"))
    pos_size = float(request.form.get("position_size"))
    notes = request.form.get("notes") or ""

    pnl = pos_size * (exit_price - entry) / entry if direction.lower()=="long" else pos_size * (entry - exit_price)/entry
    risk_amount = pos_size * abs(entry - stop) / entry if entry != stop else 0
    r_multiple = pnl / risk_amount if risk_amount else 0

    conn = sqlite3.connect(DB_PATH)
    conn.execute("""
    INSERT INTO trades
    (date,symbol,direction,entry,stop,exit,position_size,pnl,r_multiple,notes)
    VALUES (?,?,?,?,?,?,?,?,?,?)
    """,(date,symbol,direction,entry,stop,exit_price,pos_size,pnl,r_multiple,notes))
    conn.commit()
    conn.close()
    return redirect(url_for("index"))

@app.route("/delete", methods=["POST"])
def delete():
    trade_id = request.form.get("id")
    conn = sqlite3.connect(DB_PATH)
    conn.execute("DELETE FROM trades WHERE id=?", (trade_id,))
    conn.commit()
    conn.close()
    return redirect(url_for("index"))

@app.route("/position_size_calculator.html")
def calculator():
    with open("position_size_calculator.html") as f:
        return f.read()

@app.route("/edit", methods=["POST"])
def edit():
    trade_id = request.form.get("id")
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    trade = conn.execute("SELECT * FROM trades WHERE id=?", (trade_id,)).fetchone()
    conn.close()
    if trade:
        trades = get_trades()
        total_profit = sum(t['pnl'] for t in trades)
        current_date = datetime.now().strftime("%Y-%m-%d")
        return render_template_string(HTML, trades=trades, current_date=current_date,
                                      total_profit=total_profit, edit_trade=trade, form_action="/update")
    return redirect(url_for("index"))

@app.route("/update", methods=["POST"])
def update():
    trade_id = request.form.get("id")
    date = request.form.get("date") or datetime.now().strftime("%Y-%m-%d")
    symbol = request.form.get("symbol")
    direction = request.form.get("direction")
    entry = float(request.form.get("entry"))
    stop = float(request.form.get("stop"))
    exit_price = float(request.form.get("exit"))
    pos_size = float(request.form.get("position_size"))
    notes = request.form.get("notes") or ""

    pnl = pos_size * (exit_price - entry) / entry if direction.lower()=="long" else pos_size * (entry - exit_price)/entry
    risk_amount = pos_size * abs(entry - stop) / entry if entry != stop else 0
    r_multiple = pnl / risk_amount if risk_amount else 0

    conn = sqlite3.connect(DB_PATH)
    conn.execute("""
    UPDATE trades SET
    date=?, symbol=?, direction=?, entry=?, stop=?, exit=?, position_size=?, pnl=?, r_multiple=?, notes=?
    WHERE id=?
    """, (date, symbol, direction, entry, stop, exit_price, pos_size, pnl, r_multiple, notes, trade_id))
    conn.commit()
    conn.close()
    return redirect(url_for("index"))

if __name__=="__main__":
    app.run(host="0.0.0.0", port=80)

"""
ESP32-P4 人脸识别数据后端
接收 ESP32 上报的人脸识别和语音命令事件，保存到 SQLite，提供 Web 看板。
"""

import json
import sqlite3
from datetime import datetime, timezone, timedelta

from flask import Flask, g, jsonify, render_template, request

app = Flask(__name__)
DATABASE = "data.db"

# ── 数据库 ──────────────────────────────────────────────


def get_db():
    db = getattr(g, "_database", None)
    if db is None:
        db = g._database = sqlite3.connect(DATABASE)
        db.row_factory = sqlite3.Row
        db.execute("PRAGMA journal_mode=WAL")
    return db


@app.teardown_appcontext
def close_db(exception):
    db = getattr(g, "_database", None)
    if db is not None:
        db.close()


def init_db():
    with app.app_context():
        db = get_db()
        db.execute(
            """
            CREATE TABLE IF NOT EXISTS events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                device VARCHAR(32) DEFAULT 'esp32-p4',
                event_type VARCHAR(32) NOT NULL,
                detail TEXT,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            )
            """
        )
        db.execute(
            "CREATE INDEX IF NOT EXISTS idx_events_type ON events(event_type)"
        )
        db.execute(
            "CREATE INDEX IF NOT EXISTS idx_events_created ON events(created_at)"
        )
        db.commit()


# ── CORS ────────────────────────────────────────────────


@app.after_request
def add_cors(response):
    response.headers["Access-Control-Allow-Origin"] = "*"
    response.headers["Access-Control-Allow-Headers"] = "Content-Type"
    response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS"
    return response


# ── API ─────────────────────────────────────────────────

TZ = timezone(timedelta(hours=8))  # 北京时间


@app.route("/api/event", methods=["POST", "OPTIONS"])
def api_event():
    if request.method == "OPTIONS":
        return jsonify({"ok": 1})

    data = request.get_json(force=True)

    device = data.get("device", "esp32-p4")
    event = data.get("event", "unknown")
    # 统一事件名：兼容 face_recognize / face_recognized 两种写法
    event_aliases = {"face_recognize": "face_recognized"}
    event = event_aliases.get(event, event)
    # 把 device 和 event 之外的所有字段存为 detail JSON
    detail = json.dumps({k: v for k, v in data.items() if k not in ("device", "event")}, ensure_ascii=False)

    db = get_db()
    db.execute(
        "INSERT INTO events (device, event_type, detail) VALUES (?, ?, ?)",
        (device, event, detail),
    )
    db.commit()

    return jsonify({"ok": 1})


@app.route("/api/events")
def api_events():
    limit = request.args.get("limit", 50, type=int)
    limit = min(limit, 500)  # 上限

    db = get_db()
    rows = db.execute(
        "SELECT id, device, event_type, detail, created_at "
        "FROM events ORDER BY id DESC LIMIT ?",
        (limit,),
    ).fetchall()

    events = [
        {
            "id": r["id"],
            "device": r["device"],
            "event_type": r["event_type"],
            "detail": r["detail"],
            "created_at": r["created_at"],
        }
        for r in rows
    ]
    # 最新的排在前面
    return jsonify({"events": events})


@app.route("/api/status")
def api_status():
    """返回设备在线状态"""
    db = get_db()
    row = db.execute(
        "SELECT created_at FROM events WHERE event_type = 'heartbeat' "
        "ORDER BY id DESC LIMIT 1"
    ).fetchone()

    online = False
    last_heartbeat = None
    if row:
        last_heartbeat = row["created_at"]
        # SQLite 存的是 UTC，这里做比较
        try:
            hb_dt = datetime.strptime(last_heartbeat, "%Y-%m-%d %H:%M:%S")
            ago = datetime.utcnow() - hb_dt
            online = ago.total_seconds() < 120
        except (ValueError, TypeError):
            pass

    return jsonify({
        "online": online,
        "last_heartbeat": last_heartbeat,
    })


# ── 前端 ────────────────────────────────────────────────


@app.route("/")
def index():
    return render_template("index.html")


# ── 启动 ────────────────────────────────────────────────

if __name__ == "__main__":
    init_db()
    print("ESP32-P4 数据后端已启动 → http://0.0.0.0:8765")
    app.run(host="0.0.0.0", port=8765, debug=True)

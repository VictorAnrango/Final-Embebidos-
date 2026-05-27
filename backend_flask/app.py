from flask import Flask, jsonify, request, render_template

from database.db import get_db
from processing.alerts import get_thresholds, process_alerts

app = Flask(__name__)


# ---------------------------------------------------------------------------
# Dashboard
# ---------------------------------------------------------------------------

@app.route("/")
def home():
    return render_template("dashboard.html")


# ---------------------------------------------------------------------------
# Test de conexión
# ---------------------------------------------------------------------------

@app.route("/api/test-db")
def test_db():
    db = get_db()
    try:
        with db.cursor() as cursor:
            cursor.execute("SELECT NOW() AS hora_servidor")
            result = cursor.fetchone()
    finally:
        db.close()

    return jsonify(result)


# ---------------------------------------------------------------------------
# Ingesta de datos + alertas
# ---------------------------------------------------------------------------

@app.route("/api/data", methods=["POST"])
def receive_data():
    data = request.get_json()

    REQUIRED_FIELDS = [
        "n1_acc_x", "n1_acc_y", "n1_acc_z",
        "n1_gyro_x", "n1_gyro_y", "n1_gyro_z",
        "n1_spo2", "n1_bpm", "n1_bpm_valid",
        "n2_acc_x", "n2_acc_y", "n2_acc_z",
        "n2_gyro_x", "n2_gyro_y", "n2_gyro_z",
        "n2_latitud", "n2_longitud",
        "n2_altitud", "n2_velocidad",
    ]

    missing = [f for f in REQUIRED_FIELDS if f not in data]
    if missing:
        return jsonify({"error": f"Faltan campos: {', '.join(missing)}"}), 400

    db = get_db()
    try:
        thresholds = get_thresholds(db)
        alerts = process_alerts(data, thresholds)

        SQL_INSERT = """
            INSERT INTO registros (
                n1_acc_x, n1_acc_y, n1_acc_z,
                n1_gyro_x, n1_gyro_y, n1_gyro_z,
                n1_spo2, n1_bpm, n1_bpm_valid,
                n2_acc_x, n2_acc_y, n2_acc_z,
                n2_gyro_x, n2_gyro_y, n2_gyro_z,
                n2_latitud, n2_longitud,
                n2_altitud, n2_velocidad,
                alerta_pulso, alerta_spo2, alerta_movimiento
            ) VALUES (
                %s, %s, %s,
                %s, %s, %s,
                %s, %s, %s,
                %s, %s, %s,
                %s, %s, %s,
                %s, %s,
                %s, %s,
                %s, %s, %s
            )
        """

        values = (
            data["n1_acc_x"], data["n1_acc_y"], data["n1_acc_z"],
            data["n1_gyro_x"], data["n1_gyro_y"], data["n1_gyro_z"],
            data["n1_spo2"], data["n1_bpm"], data["n1_bpm_valid"],
            data["n2_acc_x"], data["n2_acc_y"], data["n2_acc_z"],
            data["n2_gyro_x"], data["n2_gyro_y"], data["n2_gyro_z"],
            data["n2_latitud"], data["n2_longitud"],
            data["n2_altitud"], data["n2_velocidad"],
            alerts["alerta_pulso"],
            alerts["alerta_spo2"],
            alerts["alerta_movimiento"],
        )

        with db.cursor() as cursor:
            cursor.execute(SQL_INSERT, values)
        db.commit()

        with db.cursor() as cursor:
            cursor.execute("SELECT LAST_INSERT_ID() AS id")
            inserted_id = cursor.fetchone()["id"]

    except Exception as e:
        db.rollback()
        return jsonify({"error": f"Error al guardar: {str(e)}"}), 500
    finally:
        db.close()

    return jsonify({
        "message":           "Datos guardados correctamente",
        "id":                inserted_id,
        "alerta_pulso":      alerts["alerta_pulso"],
        "alerta_spo2":       alerts["alerta_spo2"],
        "alerta_movimiento": alerts["alerta_movimiento"],
        "umbrales_usados":   thresholds,
    }), 201


# ---------------------------------------------------------------------------
# Último registro — lee directamente desde la BD
# ---------------------------------------------------------------------------

@app.route("/api/latest", methods=["GET"])
def get_latest():
    db = get_db()
    try:
        with db.cursor() as cursor:
            cursor.execute("""
                SELECT *
                FROM registros
                ORDER BY id DESC
                LIMIT 1
            """)
            row = cursor.fetchone()
    finally:
        db.close()

    if row is None:
        return jsonify({})

    # Convertir fecha a string si es datetime
    if row.get("fecha_hora") and hasattr(row["fecha_hora"], "isoformat"):
        row["fecha_hora"] = row["fecha_hora"].strftime("%Y-%m-%d %H:%M:%S")

    return jsonify(row)


# ---------------------------------------------------------------------------
# Umbrales
# ---------------------------------------------------------------------------

@app.route("/api/thresholds", methods=["GET"])
def get_thresholds_route():
    db = get_db()
    try:
        thresholds = get_thresholds(db)
    finally:
        db.close()

    return jsonify(thresholds)


@app.route("/api/thresholds", methods=["PUT"])
def update_thresholds():
    data = request.get_json()

    required = ["bpm_min", "bpm_max", "spo2_min", "movimiento_max"]
    missing = [f for f in required if f not in data]
    if missing:
        return jsonify({"error": f"Faltan campos: {', '.join(missing)}"}), 400

    db = get_db()
    try:
        with db.cursor() as cursor:
            cursor.execute("""
                UPDATE umbrales
                SET bpm_min=%s,
                    bpm_max=%s,
                    spo2_min=%s,
                    movimiento_max=%s
                WHERE id = 1
            """, (
                data["bpm_min"],
                data["bpm_max"],
                data["spo2_min"],
                data["movimiento_max"],
            ))
        db.commit()
    except Exception as e:
        db.rollback()
        return jsonify({"error": str(e)}), 500
    finally:
        db.close()

    return jsonify({"message": "Umbrales actualizados"})


# ---------------------------------------------------------------------------
# Historial
# ---------------------------------------------------------------------------

@app.route("/api/history", methods=["GET"])
def get_history():
    start = request.args.get("start")
    end   = request.args.get("end")

    if not start or not end:
        return jsonify({"error": "Parámetros 'start' y 'end' requeridos"}), 400

    db = get_db()
    try:
        with db.cursor() as cursor:
            cursor.execute("""
                SELECT *
                FROM registros
                WHERE fecha_hora BETWEEN %s AND %s
                ORDER BY fecha_hora ASC
            """, (start, end))
            result = cursor.fetchall()

        # Serializar fechas
        for row in result:
            if row.get("fecha_hora") and hasattr(row["fecha_hora"], "isoformat"):
                row["fecha_hora"] = row["fecha_hora"].strftime("%Y-%m-%d %H:%M:%S")

    finally:
        db.close()

    return jsonify(result)


# ---------------------------------------------------------------------------

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8080, debug=True)
from flask import Blueprint, request, jsonify
from database.db import get_db
from processing.alerts import get_thresholds, process_alerts

data_bp = Blueprint('data_bp', __name__)

REQUIRED_FIELDS = [
    "n1_acc_x", "n1_acc_y", "n1_acc_z",
    "n1_gyro_x", "n1_gyro_y", "n1_gyro_z",
    "n1_spo2", "n1_bpm", "n1_bpm_valid",
    "n2_acc_x", "n2_acc_y", "n2_acc_z",
    "n2_gyro_x", "n2_gyro_y", "n2_gyro_z",
    "n2_latitud", "n2_longitud",
    "n2_altitud", "n2_velocidad",
]

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
)
VALUES (
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


@data_bp.route('/api/data', methods=['POST'])
def insert_data():
    data = request.get_json()

    # --- Validación de campos ---
    missing = [f for f in REQUIRED_FIELDS if f not in data]
    if missing:
        return jsonify({"error": f"Faltan campos: {', '.join(missing)}"}), 400

    db = get_db()
    try:
        # --- Leer umbrales dinámicos desde la BD ---
        thresholds = get_thresholds(db)

        # --- Calcular alertas ---
        alerts = process_alerts(data, thresholds)

        # --- Construir valores para el INSERT ---
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
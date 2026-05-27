import math


DEFAULT_THRESHOLDS = {
    'bpm_min':        40,
    'bpm_max':        120,
    'spo2_min':       90,
    'movimiento_max': 2.5,
}


def get_thresholds(db):
    """
    Lee los umbrales activos desde la tabla `umbrales`.
    Si no encuentra ningún registro, devuelve los valores por defecto.
    """
    try:
        with db.cursor() as cursor:
            cursor.execute("SELECT * FROM umbrales WHERE id = 1")
            row = cursor.fetchone()

        if row:
            return {
                'bpm_min':        row.get('bpm_min',        DEFAULT_THRESHOLDS['bpm_min']),
                'bpm_max':        row.get('bpm_max',        DEFAULT_THRESHOLDS['bpm_max']),
                'spo2_min':       row.get('spo2_min',       DEFAULT_THRESHOLDS['spo2_min']),
                'movimiento_max': row.get('movimiento_max', DEFAULT_THRESHOLDS['movimiento_max']),
            }

    except Exception as e:
        print(f"[alerts] Error leyendo umbrales, usando valores por defecto: {e}")

    return DEFAULT_THRESHOLDS.copy()


def process_alerts(data, thresholds=None):
    """
    Evalúa las alertas para un registro de sensores.

    Parámetros
    ----------
    data       : dict con los campos del sensor
    thresholds : dict con los umbrales (obtenido con get_thresholds).
                 Si es None usa DEFAULT_THRESHOLDS.

    Retorna
    -------
    dict con alerta_pulso, alerta_spo2, alerta_movimiento (bool)
    """
    if thresholds is None:
        thresholds = DEFAULT_THRESHOLDS

    bpm  = data['n1_bpm']
    spo2 = data['n1_spo2']

    movimiento_n1 = math.sqrt(
        data['n1_acc_x'] ** 2 +
        data['n1_acc_y'] ** 2 +
        data['n1_acc_z'] ** 2
    )

    movimiento_n2 = math.sqrt(
        data['n2_acc_x'] ** 2 +
        data['n2_acc_y'] ** 2 +
        data['n2_acc_z'] ** 2
    )

    return {
        'alerta_pulso':      bpm < thresholds['bpm_min'] or bpm > thresholds['bpm_max'],
        'alerta_spo2':       spo2 < thresholds['spo2_min'],
        'alerta_movimiento': movimiento_n1 > thresholds['movimiento_max']
                             or movimiento_n2 > thresholds['movimiento_max'],
    }
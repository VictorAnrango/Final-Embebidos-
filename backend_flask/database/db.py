import os
import time
import pymysql

from dotenv import load_dotenv

load_dotenv()

DB_CONFIG = {
    'host':        os.environ.get('DB_HOST', 'localhost'),
    'port':        int(os.environ.get('DB_PORT', 3306)),
    'user':        os.environ.get('DB_USER', 'root'),
    'password':    os.environ.get('DB_PASS', 'victor123'),
    'database':    os.environ.get('DB_NAME', 'embebidos_db'),
    'charset':     'utf8mb4',
    'cursorclass': pymysql.cursors.DictCursor,
    'autocommit':  False,
    'init_command': "SET time_zone = '-05:00'",
    'connect_timeout': 10,
}

def get_db(retries=3, delay=1):

    for attempt in range(retries):

        try:

            connection = pymysql.connect(**DB_CONFIG)

            print("MySQL conectado")

            return connection

        except Exception as e:

            print(f"Error MySQL: {e}")

            time.sleep(delay)

    raise Exception("No se pudo conectar a MySQL")
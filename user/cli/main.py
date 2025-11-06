#!/usr/bin/env python3

import os
import fcntl
import struct
import select
import sys
import argparse
import time
from datetime import datetime

# Definimos la estructura binaria (¡debe coincidir con nxp_simtemp_ioctl.h!)
# __u64 timestamp_ns -> 'Q' (unsigned long long, 8 bytes)
# __s32 temp_mC      -> 'i' (signed int, 4 bytes)
# __u32 flags        -> 'I' (unsigned int, 4 bytes)
STRUCT_FORMAT = 'Q i I'
STRUCT_SIZE = struct.calcsize(STRUCT_FORMAT)

# Banderas (deben coincidir con nxp_simtemp_ioctl.h)
SIMTEMP_FLAG_NEW_SAMPLE = (1 << 0)
SIMTEMP_FLAG_THRESHOLD_CROSSED = (1 << 1)

# Rutas Sysfs (asumiendo que se monta en /sys/class/simtemp/simtemp)
SYSFS_PATH = "/sys/class/simtemp/simtemp"
DEVICE_PATH = "/dev/simtemp"

def sysfs_write(attr, value):
    """Escribe un valor a un atributo sysfs."""
    path = os.path.join(SYSFS_PATH, attr)
    try:
        with open(path, 'w') as f:
            f.write(str(value))
        # print(f"SYSFS: Set {attr} = {value}")
    except IOError as e:
        print(f"Error escribiendo a sysfs {path}: {e}", file=sys.stderr)
        print("¿Está el módulo cargado? (sudo insmod)", file=sys.stderr)
        sys.exit(1)

def sysfs_read(attr):
    """Lee un valor de un atributo sysfs."""
    path = os.path.join(SYSFS_PATH, attr)
    try:
        with open(path, 'r') as f:
            return f.read().strip()
    except IOError as e:
        print(f"Error leyendo sysfs {path}: {e}", file=sys.stderr)
        sys.exit(1)

def run_monitor(dev_fd):
    """Bucle principal de monitoreo usando poll."""
    print(f"Monitoreando {DEVICE_PATH} (tamaño struct={STRUCT_SIZE} bytes)...")
    print("Timestamp (ISO)         | Temp (C) | Alerta")
    print("-" * 50)

    # Crear un objeto poll
    poller = select.poll()
    # Registrar el file descriptor para:
    # POLLIN | POLLRDNORM -> Datos listos para leer
    # POLLPRI              -> Evento prioritario (nuestro umbral)
    poller.register(dev_fd, select.POLLIN | select.POLLRDNORM | select.POLLPRI)

    while True:
        try:
            # Esperar indefinidamente por un evento
            events = poller.poll()
            
            for fd, event in events:
                # --- Evento de Alerta de Umbral (POLLPRI) ---
                if event & select.POLLPRI:
                    print(f"!!! EVENTO DE UMBRAL (POLLPRI) RECIBIDO !!!")

                # --- Evento de Datos Listos (POLLIN) ---
                if event & (select.POLLIN | select.POLLRDNORM):
                    # Leer el número exacto de bytes para nuestro struct
                    binary_data = os.read(dev_fd, STRUCT_SIZE)
                    
                    if len(binary_data) == 0:
                        print("Fin de archivo (¿módulo descargado?). Saliendo.")
                        return
                    
                    if len(binary_data) != STRUCT_SIZE:
                        print(f"Lectura corta: {len(binary_data)}/{STRUCT_SIZE} bytes", file=sys.stderr)
                        continue

                    # Desempaquetar los datos binarios
                    timestamp, temp, flags = struct.unpack(STRUCT_FORMAT, binary_data)
                    
                    # Formatear la salida
                    temp_c = temp / 1000.0
                    ts_iso = datetime.fromtimestamp(timestamp / 1e9).isoformat(timespec='milliseconds')
                    
                    # Comprobar la bandera de alerta en los datos
                    alert_flag = bool(flags & SIMTEMP_FLAG_THRESHOLD_CROSSED)
                    
                    print(f"{ts_iso} | {temp_c:8.3f} | {alert_flag}")

        except KeyboardInterrupt:
            print("\nMonitoreo detenido por el usuario.")
            break
        except Exception as e:
            print(f"Error en el bucle de poll: {e}", file=sys.stderr)
            break

def run_test_mode():
    """
    Modo de prueba (T3/T5 del challenge):
    1. Establece un umbral bajo (p.ej. 30C)
    2. Establece un muestreo rápido (100ms)
    3. Espera (con poll) por un evento POLLPRI
    4. Falla (exit 1) si no ocurre en 3 periodos.
    5. Tiene éxito (exit 0) si ocurre.
    """
    print("--- Iniciando Modo de Prueba (Acceptance Test) ---")
    
    # 1. Configurar el dispositivo para la prueba
    #    (Asumimos que el modo 'normal' genera 25-35C)
    test_threshold = 30000 # 30.0 C (debería dispararse)
    test_period_ms = 100   # 100 ms
    
    print(f"Configurando: sampling_ms={test_period_ms}, threshold_mC={test_threshold}")
    sysfs_write("mode", "normal")
    sysfs_write("sampling_ms", test_period_ms)
    sysfs_write("threshold_mC", test_threshold)
    
    timeout_sec = (test_period_ms * 3) / 1000.0 + 0.1 # 3 periodos + margen
    
    try:
        # Abrir el dispositivo
        fd = os.open(DEVICE_PATH, os.O_RDONLY)
        
        poller = select.poll()
        poller.register(fd, select.POLLPRI) # Solo nos importa la alerta
        
        print(f"Esperando por un evento POLLPRI (alerta) (timeout={timeout_sec}s)...")
        
        # 2. Esperar por el evento
        # poll() toma el timeout en milisegundos
        events = poller.poll(timeout_sec * 1000)
        
        if not events:
            print("\n--- PRUEBA FALLIDA (FAIL) ---")
            print("Timeout: El evento POLLPRI (alerta de umbral) no se recibió.")
            print(f"Stats actuales: {sysfs_read('stats')}")
            return False

        # 3. Verificar el evento
        for _, event in events:
            if event & select.POLLPRI:
                print("\n--- PRUEBA SUPERADA (PASS) ---")
                print("Evento POLLPRI (alerta de umbral) recibido correctamente.")
                return True
            else:
                print(f"Evento inesperado: {event}")
        
        print("\n--- PRUEBA FALLIDA (FAIL) ---")
        print("poll() despertó pero el evento no fue POLLPRI.")
        return False

    except Exception as e:
        print(f"\n--- PRUEBA FALLIDA (FAIL) ---")
        print(f"Error durante la prueba: {e}", file=sys.stderr)
        return False
    finally:
        if 'fd' in locals():
            os.close(fd)
        # Restaurar configuración
        sysfs_write("sampling_ms", 1000)
        sysfs_write("threshold_mC", 27000)

def main():
    parser = argparse.ArgumentParser(description="CLI App for NXP simtemp driver")
    parser.add_argument(
        '--test', 
        action='store_true', 
        help="Correr el modo de prueba de aceptación (T3/T5)."
    )
    parser.add_argument(
        '-s', '--set-sampling-ms', 
        type=int, 
        metavar="MS",
        help="Configurar el intervalo de muestreo (ms) vía sysfs"
    )
    parser.add_argument(
        '-t', '--set-threshold-mc', 
        type=int, 
        metavar="MC",
        help="Configurar el umbral de alerta (mili-C) vía sysfs"
    )
    parser.add_argument(
        '-m', '--set-mode', 
        type=str, 
        choices=['normal', 'noisy', 'ramp'],
        help="Configurar el modo de simulación vía sysfs"
    )
    
    args = parser.parse_args()

    # --- Modo de Prueba ---
    if args.test:
        if not run_test_mode():
            sys.exit(1) # Salir con error para el script de demo
        sys.exit(0)

    # --- Modo de Configuración ---
    if args.set_sampling_ms is not None:
        sysfs_write("sampling_ms", args.set_sampling_ms)
    if args.set_threshold_mc is not None:
        sysfs_write("threshold_mC", args.set_threshold_mc)
    if args.set_mode:
        sysfs_write("mode", args.set_mode)

    # Si solo se configuró, no monitorear
    if any([args.set_sampling_ms, args.set_threshold_mc, args.set_mode]):
        print("Configuración actualizada. Stats actuales:")
        print(sysfs_read("stats"))
        sys.exit(0)

    # --- Modo de Monitoreo (default) ---
    try:
        # Abrir el dispositivo de caracteres
        # Usamos os.open para obtener un file descriptor (int)
        # que es lo que 'select.poll()' necesita.
        fd = os.open(DEVICE_PATH, os.O_RDONLY | os.O_NONBLOCK)
    except Exception as e:
        print(f"Error abriendo {DEVICE_PATH}: {e}", file=sys.stderr)
        print("¿Está el módulo cargado? (sudo insmod)", file=sys.stderr)
        sys.exit(1)

    run_monitor(fd)
    
    # Cerrar el file descriptor
    os.close(fd)

if __name__ == "__main__":
    main()
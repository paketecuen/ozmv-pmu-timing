# Registro de sesión 02: dos ozmV con GPS

Plan: [SESION-02](../../../bench/kit-fran/SESION-02.md) (manda), [BANCO-para-Fran](../../../BANCO-para-Fran.md) revisión 2026-10-20, [LEEME](../../../bench/kit-fran/LEEME.md) y [GUIA-OPUS](../../../bench/kit-fran/GUIA-OPUS.md).
Kit usado: rama `fran/kit-sesion-02`, commit `1124433` (`--trig-out APE` y `copy-board-direct.sh`).
Este registro y los logs de estado van en `data/bench-s02/fran/`; las grabaciones de A y B, en `C:\Users\moren\ozmv-bench\datos\sesion-02\`.

Fecha: 2026-10-29. Lugar: laboratorio de Fran. Operador: Fran. ID de sesión: sesion-02.
Fuente: red del laboratorio, misma fase y neutro para A y B. Agilent 6811B: panel frontal entero muerto, pantalla incluida (Fran, 2026-10-29); solo quedaría el acceso remoto por RS-232 o GPIB, sin probar. E2 pendiente: fuente sin verificar y sin secuencia definida por Paco.
Horas del registro: **UTC** (hora local = UTC+1).
Datos privados, solo para Paco: los estados pueden contener coordenadas GPS.

## Cronología UTC

Todo cambio de cables, tierras, antenas o sondas va aquí con su hora.

| UTC | Qué | Notas |
| --- | --- | --- |
| ~07:17:14 | A arranca | boot `b520e8d8-b58e-4bdd-8f9e-024c14a0c11f` (de su uptime) |
| ~07:18:11 | B arranca | boot `9c8d4e94-9bb3-4d0b-9153-bddf0a22331d` (de su uptime) |
| antes de 08:02 | Regla 1 en el PC (powercfg como administrador) | Fran. Antes ya: suspensión e hibernación en corriente = nunca; PC enchufado |
| 07:57:52 | Lanzador `ozmv-bench.cmd`; SSH a las dos a las 07:58:10 | usbipd 1-2 y 2-1 Attached |
| 07:58:22 | Estado inicial | A y B: crédito sí, `source_recoveries` 0, 0 fallos de adquisición, GPS confirmado (A 8 sat., B 7), `min_free_kbytes` 3882 |
| 08:02:23 | A: regla 3 (`min_free_kbytes` 65536), `STOP` → `STOP-sesion01-20261027`, grabador arrancado 08:02:24 | `wmu/pmu-20261029T080224Z`; 6,1 GB en `/data/bench-193`, 41,7 GB libres. Estado: Holdovers 2, Rejoins 1, LastRejoinNs 1 999 999 681 |
| 08:02:41 | B: lo mismo, grabador arrancado 08:02:41 | `wmu/pmu-20261029T080241Z`; 5,9 GB, 42,2 GB libres. Estado: Holdovers 1, Rejoins 0 |
| 08:03 | Inicio de los 5 min con GPS confirmado | `gps-5min.log` |
| 08:05 | Registro y logs de preparación movidos al repo, `data/bench-s02/fran/` | |
| 08:02–08:09 | GPS confirmado en las dos, sin holdovers nuevos; WMU creciendo | `gps-5min.log` (del `recorder.log`). El PMU no publicaba: `ReferenceFailures`, sin tensión en VA |
| ~08:12 | Red conectada a VA_A, VA_B (y 8588A); el PMU empieza a publicar hacia 08:12:30 | Hora deducida del contador de tramas; confirmar con Fran |
| ¿? | Fluke conectados con el cableado de M2 (PPS_A al 8558A) | Hora por confirmar con Fran |
| 08:17:04 | **A y B: `PRU stopped acquisition: error 13` en el mismo segundo** | Sin avisos de memoria en dmesg. `source_recoveries` 1 en cada una, crédito sí; adquisición reanudada 08:17:06–07. `A-fallo-1.log`, `B-fallo-1.log`. Fran: «puede ser que estuviera enchufando algo, los Fluke o algo» |
| ~08:3x | Osciloscopio conectado (sondas y masas) | Hora por confirmar con Fran |
| 08:36:17 | **A y B: `PRU stopped acquisition: error 9` en el mismo segundo** | B: `source_recoveries` 2, le queda 1 ventana |
| 08:36:22, 08:36:27 | A: `error 13` dos veces más; cuarto fallo del arranque → `m4_fault` → **A se reinicia a las 08:36:30** | Con crédito: sin bloqueo. `A-fallo-2.log` |
| 08:37:29 | A: arranque nuevo `87f282ab-6df1-44fe-b999-6fc2eac74643`; grabador arrancado 08:37:35 | Trozo `*-20261029T083237Z` cortado por el reinicio; sigue en `*-20261029T083741Z`. Sin crédito hasta ~09:07:30 si no falla |
| ~09:00 | Osciloscopio, medida b en A (cablecito del PPS frente a la entrada del 8558A): «imposible detectar diferencia entre los flancos» | Ver la tabla del osciloscopio. B no se mide (decisión de Fran) |
| ~09:02–09:03 | B: holdover espontáneo, retorno a 27 ns | PMU de B: `EstimatorResets` 293 275 (1 a las 08:02), estable desde las 09:00 |
| 09:04:44 | A: regla 3 otra vez (`min_free_kbytes` 3882 → 65536) | Sin reserva de 08:37 a 09:04 |
| 09:08:05 | A recupera el crédito de reinicio | B lo mantiene |
| ~09:12:30–09:12:47 | Osciloscopio quitado: puntas y pinzas de masa | Vigilancia de fallos cada 10 s: `vigilancia.log` |
| 09:12:30 | **A: `error 13`** al quitar el osciloscopio | `source_recoveries` 1, crédito sí. `A-fallo-3.log` |
| ~09:13 | 8558A quitado: PPS (T y adaptador), disparo del 8588A, tierras | |
| 09:13:12 | **B: `error 9`** al quitar el 8558A | `source_recoveries` 3: sin ventanas; el próximo fallo la reinicia (con crédito). `B-fallo-3.log` |
| 09:14:47 | **B: `error 9`**, cuarto fallo → `m4_fault` → **B se reinicia a las 09:14:48** | Con crédito: sin bloqueo. Fran estaba quitando la tensión. `B-fallo-4-reinicio.log` |
| ~09:14:47–09:16 | Tensión quitada (Fran) | |
| ~09:46 | B recupera el crédito de reinicio | A y B con crédito |
| ~09:48–09:49 | Ramales cruzados (VA_A ↔ VA_B, misma polaridad) y tensión dada (Fran) | Tensión hacia 09:49:28, deducida del PMU de B. Sin fallos en ninguna placa |
| 09:50 | Comprobación antes de M4-r02 | A: 1 fallo en el arranque, crédito sí; B: 0 fallos, crédito sí. GPS confirmado (6–7 sat.), PMU publicando en las dos |
| **09:51:00** | **Inicio de M4-r02**: ramales cruzados, sin Fluke ni osciloscopio, cada PPS solo a su PRU y su IN | Fin previsto 10:21:00. Estado cada 30 s: `M4-r02-estado.log` |
| ~09:55–09:56 | B: holdover espontáneo durante M4-r02, retorno a 275 ns | A sin holdovers en toda la pasada |
| **10:21:00** | **Fin de M4-r02**: 30 min sin fallos, GPS confirmado en cada muestra (6–9 sat.), PMU a 50 tramas/s en las dos, 0 muestras perdidas | `M4-r02-estado.log` |
| ~10:25–10:37 | Montaje de E4-A (Fran): divisor de IN_A pasado del PPS_A al PPS_B; el PRU de A conserva PPS_A | Sin fallos en ninguna placa (A sigue con 1, B con 0; crédito en las dos) |
| 10:38:30, 10:39:05 | Prueba del PPS en IN con lectura directa (6 MB por placa, de una en una) | IN_A: 28 pulsos/28,5 s, 1,000 s, 100 ms, meseta +0,296 V; IN_B: meseta +0,298 V. Sin saturación. (Corregido a las 11:37: el nivel no identifica la fuente; los 0,337 V de la sesión 01 son el escalón del ajuste de M1, no esta meseta. Ver la verificación de E4-A más abajo) |
| 10:44:11 | **A: reinicio planificado** (regla 6, `ozmv-pb2-m4-control reboot`) | Antes: crédito sí, `source_recoveries` 1, sin ventana abierta. `A-reinicio-planificado.log` |
| 10:44:19 | A: arranque nuevo `2021b800-d3a4-482c-8624-6cd7686cb00c`; grabador arrancado 10:44:19 | Tras el reinicio planificado: `reset_credit` false, `reset_reserved` true (no conserva el crédito; lo recupera ~11:14 si no falla) |
| 10:45:06 | A: regla 3 (`min_free_kbytes` 65536) | GPS confirmado, 9 sat. B sin reiniciar: 0 ventanas gastadas |
| 10:53:09, 10:53:16 | **E4-A: registro de estado cada 2 s** en A y en B (`/data/bench-193/e4-A-s02.log` en cada placa; bucle de GUIA-OPUS en `e4-poll.sh`, con `nohup`) | Privado: puede llevar coordenadas, no va al repo. Se para con `STOP_E4` |
| 10:53:16 | **E4-A: inicio de la base** (5 min, antenas destapadas) | |
| 09:2x | Decisión: se hace M4-r02 (ramales cruzados) cuando B tenga crédito | Fran: la diferencia A–B es de las tolerancias de la placa; en la cadena de VA unas resistencias son del 0,1 % y otras del 1 %. Los cables de los ramales tienen la misma longitud |
| 09:15:27 | B: arranque nuevo `4d762a7e-a7d7-4bab-a734-eaff8d61502c`; grabador arrancado 09:16:09 | Sin crédito hasta ~09:46 si no falla |
| 09:16:11 | B: regla 3 (`min_free_kbytes` 65536) | |

Salidas de las reglas 3, 5 y 6 al empezar: `A-preparacion.log`, `B-preparacion.log`; la regla 6 está en el estado inicial de arriba.

## Identidad y configuración

| Dato | Equipo A | Equipo B |
| --- | --- | --- |
| Acceso | USB-C, puente, `ssh -p 2223 root@127.0.0.1` | USB-C, puente, `ssh -p 2222 root@127.0.0.1` |
| Release / aplicación | | |
| Boot ID | `b520e8d8-b58e-4bdd-8f9e-024c14a0c11f` | `9c8d4e94-9bb3-4d0b-9153-bddf0a22331d` |
| Sesión del supervisor | `d3aa2339c915325845c51315f7e26a52` | `499642a87a51ec2d6bd925781d9c64f0` |
| Antena / ubicación / cielo | | |
| Grabador: inicio / límites | 08:02:24, 10 h, 30 GB, trozos de 1800 s | 08:02:41, 10 h, 30 GB, trozos de 1800 s |

## Cableado

| Tramo | Longitud | Notas |
| --- | --- | --- |
| PPS_A → PRU_A | | |
| PPS_B → PRU_B | | |
| PPS_A → divisor → IN_A | | |
| PPS_B → divisor → IN_B | | |
| PPS_B → divisor → IN_A (E4-A) | | Rama PPS_A retirada de IN_A: |
| PPS_A → divisor → IN_B (E4-B) | | Rama PPS_B retirada de IN_B: |
| Red → VA_A / VA_B | | Ramales originales / intercambiados: |

## Pasadas (orden de SESION-02)

| Paso / ID | Inicio UTC | Fin UTC | PPS en IN_A / IN_B / Fluke | Ficheros A / B | Incidencias |
| --- | --- | --- | --- | --- | --- |
| 0 Preparación y GPS 5 min | 08:02 | | A / B / — | `*-20261029T080224Z` / `*-20261029T080241Z` | |
| 1 Osciloscopio A (a con/sin 8558A, b) | | | | | |
| 1 Osciloscopio B (a con/sin 8558A, b) | | | | | |
| 2 Fluke desconectados del todo; regla 6 | | | | | |
| 3 M4-r02, ramales intercambiados, sin Fluke | 09:51:00 | 10:21:00 | A / B / — | A: `wmu-20261029T093817Z`, `wmu-20261029T100831Z` y sus PMU; B: `wmu-20261029T094603Z`, `wmu-20261029T101617Z` y sus PMU | Sin fallos; B: un holdover ~09:55 (retorno 275 ns) |
| 3b M4-r01b, ramales originales (opcional) | | | A / B / — | | |
| 4 E4-A (A sin GPS, B referencia) | | | B / B / — | | |
| 5 E4-B (si A ≥ 6 sat. y sin holdover espontáneo) | | | A / A / — | | |
| 6 E2 (solo si la Agilent funciona) | | | | | |
| 7 STOP, cierre, copia de una en una | | | | | |

## Osciloscopio: desfase del PPS

| Placa | Medida | Con 8558A | Retardo (cruces 1,65 V) | Niveles alto/bajo | Subida | Cables | Captura |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A | a) receptor → entrada PocketBeagle | — | No medible: P2.17 queda oculto (SMD); el PPS sale por un cablecito del que cuelga todo (PRU, divisor de IN y 8558A) | | | | |
| A | b) cablecito del PPS → entrada 8558A | sí | Sin diferencia detectable entre los flancos (Fran, ~09:00) | | | | |
| B | a) y b) | — | No se mide (decisión de Fran) | | | | |

Del cablecito al PRU quedan la pista y R19 (P2.17, captura eCAP del PRU): sacar su valor del esquema para acotar ese tramo.

## Cada pérdida y recuperación de GPS

| ID | Sin GPS / referencia | Objetivo | Antena cubierta | Holdover efectivo | Antena destapada | GPS confirmado | Fin estabilización | Referencia conservó GPS |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| E4-A-1 | A / B | 1 min | ~10:58:30 | 10:58:49 (Fix false); Fix otra vez 11:00:21 | ~10:59:50 (aviso 10:59:49) | 11:00:30 (candidate 11:00:23) | 11:05:30 | Sí (sin transiciones en B) |
| E4-A-5 | A / B | 5 min | ~11:05:30 | 11:05:35 (Fix false); Fix otra vez 11:11:16 | ~11:10:40 (aviso 11:10:35) | 11:11:25 (candidate 11:11:18) | 11:16:25 | Sí durante la pérdida. Antes, B: Reference unavailable 11:04:10–11:04:21 con Fix, confirmado otra vez 11:04:21 |
| E4-A-10 | A / B | 10 min | ~11:17:30 | 11:17:45 (Fix false); Fix otra vez 11:28:22. **Fix breve a través de la tapa 11:18:23–11:18:27** (candidate, sin confirmar) | ~11:27:50 (aviso 11:27:45) | 11:28:33 (candidate 11:28:26) | 11:33:33 | Sí (B sin transiciones de 11:13:39 a 11:28) |
| E4-B-1 | B / A | 1 min | ~11:48:00 | 11:49:24 (Fix false; 11:49:26 en el registro de 2 s); Fix otra vez 11:50:53 | ~11:50:30 (aviso 11:50:24) | 11:51:02 (candidate 11:50:56) | 11:56:02 | Sí (A sin transiciones) |
| E4-B-5 | B / A | 5 min | ~11:56:30 (aviso «tapada» 11:56:34; B mantiene el Fix ~2 min) | 11:58:28 (Fix false); Fix otra vez 12:03:50; sin destellos | ~12:03:35 (aviso 12:03:30) | 12:04:01 (candidate 12:03:52) | 12:09:01 (la de 10 min empieza después del cambio de fichero de B, ~12:17:16) | Sí (A sin transiciones) |
| E4-B-10 | B / A | 10 min | ~12:18 (aviso «tapada» 12:18:14); la tapa no bloquea: B confirmado con 7–8 sat. hasta que se recoloca hacia las 12:25 | 12:26:06 (Fix false); Fix otra vez 12:36:57; sin destellos | ~12:36:30 (aviso 12:36:06) | 12:37:06 (candidate 12:37:00) | 12:42:06 | Sí (A sin transiciones de 12:17 a 12:37) |

Cómo se tapa (Fran): la antena se mete en la habitación, donde ya pierde la señal, y se tarda unos 20–30 s en meterla en la caja. El Fix breve de E4-A-10 (11:18:23–11:18:27) cae en ese tránsito. El último pulso del receptor de A fue a las ~11:18:26 (`PPSAgeMs`).
Entre la pérdida de 5 y la de 10 min, **A y B a la vez**: Reference unavailable con Fix 11:13:15–11:13:26 (A) y 11:13:16–11:13:39 (B). Evento común a los dos receptores, sin tapar nada; cae en la recuperación de E4-A-5.
Condición de E4-B: en la base (10:53:16–10:58:16) A tuvo 8–9 satélites y ningún holdover espontáneo → se cumple. Ningún fallo de adquisición durante E4-A en ninguna placa.
Fin de E4-A: 11:33:33. `STOP_E4` a las 11:33:36 en las dos; bucles terminados y `STOP_E4` retirado. `e4-A-s02.log`: 11,5 MB (A) y 11,7 MB (B), en `/data/bench-193` de cada placa (privados).
**Verificación del montaje de E4-A:** en `wmu-20261029T111454Z` de A, 11:23:15–11:23:39 (mitad de la pérdida de 10 min), IN_A tiene 24 pulsos de 1 s; a las 11:23:20 `PPSAgeMs` de A era 294 511 ms (su receptor sin pulso desde ~11:18:26). Los pulsos de IN_A venían de PPS_B.

| UTC | Qué | Notas |
| --- | --- | --- |
| ~11:34–11:35:40 | Montaje de E4-B (Fran): divisor de IN_B pasado de PPS_B a PPS_A; IN_A sigue en PPS_B; PRU de B con PPS_B | Sin fallos en ninguna placa (0 ventanas gastadas, crédito en las dos): no hace falta reinicio planificado. IN_B: pico de 0,42 V durante el cambio; después meseta +0,299 V, 1 pulso/s |
| 11:36:46 | **A: `error 9`** | Tras el recableado de E4-B, antes de la base. Causa por confirmar con Fran |
| 11:38:11 | **B: `error 13`** | Antes de la base. Causa por confirmar con Fran |
| 11:38:23, 11:38:29 | **E4-B: registro de estado cada 2 s** en A y en B (`e4-B-s02.log`) e inicio de la base | GPS confirmado: A 10 sat., B 9. B: Reference unavailable 11:39:02–11:39:13 con Fix |
| ~11:48:00 | E4-B-1: Fran avisa «tapada» (11:48:06) | B mantiene el Fix ~78 s más, mientras la antena llega a la caja |

Fin de E4-B: 12:42:06. `STOP_E4` a las 12:42:09 en las dos; bucles terminados y `STOP_E4` retirado. `e4-B-s02.log`: 18,5 MB (A) y 18,2 MB (B), privados. Ningún fallo de adquisición durante la base ni la serie de E4-B.
**Cierre: `STOP` a las 12:42:10 (A) y 12:42:12 (B).** Grabadores cerrados: **A 12:45:37** (`wmu-20261029T121523Z`, 482 996 256 bytes) y **B 12:47:31** (`wmu-20261029T121716Z`, 492 177 639 bytes), los dos con «stop: PMU and WMU back to the defaults». Último estado: A Holdovers 9, Rejoins 7, LastRejoinNs 95; B Holdovers 13, Rejoins 11, LastRejoinNs 21 965 (retorno tras E4-B-10).
Copia de A con `bench/kit-fran/copy-board-direct.sh A 20261029 …/datos/sesion-02`: a las 12:48:03 se para antes del primer fichero por «memoria libre baja (188048 kB)». La memoria libre estaba en ~185–200 MB en las dos placas, pero la disponible en ~590 MB: ~577 MB de caché limpia de la grabación (Dirty < 1 MB). El 28/10 las placas acababan de arrancar (~730 MB libres). Con el permiso de Fran, `sync; echo 1 > /proc/sys/vm/drop_caches` en A a las 12:49:04 (libre 192 → 734 MB, sin fallos). Copia de A relanzada a las 12:49:27. Entre ~12:49 y 12:53, Fran: «un pequeño meneíto de las placas»; a las 12:53:34, sin fallos nuevos ni reinicios en ninguna y la copia siguiendo.
**Copia de A terminada a las 13:43:00:** 22 ficheros, 4 568 988 990 bytes, iguales en placa y PC; SHA-256 22/22 comprobadas otra vez en el PC. Sin fallos durante la copia (a ~1,35–1,65 MB/s, libre ~700 MB). Registros privados de A (`e4-A-s02.log`, `e4-B-s02.log`, `recorder.log`, `e4-poll.sh`) en `datos\sesion-02\A\privado`, con SHA-256.
**B: copia pendiente** (decisión de Fran: apagar ya y copiar B otro día, como el 27→28/10). Registros privados de B copiados a `datos\sesion-02\B\privado` con SHA-256 a las 13:59. Comprobación final a las 13:59:33: A y B con crédito, grabador parado y `STOP` presente, sin sondeo E4; B con 20 ficheros de hoy en `/data/bench-193`.
Para la copia de B: al encender, regla 3; `copy-board-direct.sh B 20261029 /c/Users/moren/ozmv-bench/datos/sesion-02` (espera al crédito, ~30 min tras el arranque; ~4,6 GB, ~50 min). Con la placa recién arrancada no hace falta vaciar la caché.
**Apagado** hacia las 14:00 UTC (Fran quita los USB-C; hora exacta por confirmar).
**B otra vez encendida para la copia** (Fran; la antena bajo cubierta, sin GPS posible). Puente relanzado a las 16:06:11 UTC; SSH a B a las 16:06:29. B arrancó con un **oops del kernel a los 4,8 s**: `Internal error: synchronous external abort` en `ramoops` (`persistent_ram_post_init`, proceso `systemd-modules`), kernel `Tainted: G M` (`tainted=144`). Por eso **la adquisición no arranca** («Kernel is tainted», 5 reintentos, `failed») **y el supervisor M4 tampoco** («Kernel must be untainted before supervision»); `m4-control status` no responde. Arranque `bae9de3b-8e61-4b51-9320-3d2074d547bf`. El reloj de B arranca del último guardado (~14:00) por no tener GPS. Detalle en `B-arranque-oops.log`. Esta mañana B arrancó dos veces sin este error.
Con la adquisición parada no hay riesgo de pérdida de paquetes del PRU: con el permiso de Fran, copia de B a las ~16:10 UTC con una variante de `copy-board-direct.sh` sin la espera al crédito (que no se puede leer); el resto de comprobaciones igual. Después, desenchufar B y dejarla para Paco. Copia arrancada a las 16:09:58. A las 16:29 la memoria libre de B había bajado de 225 a 204 MB (caché solo 106 MB): `sync; echo 1 > /proc/sys/vm/drop_caches` (libre 204 → 274 MB) para no tocar el umbral de 200 MB del script; la copia sigue. Después, vigilancia cada 5 min (`B-cache.log`): libre 243–266 MB, no hizo falta vaciar más.
**Copia de B terminada a las 17:00:20:** 20 ficheros, 4 619 660 721 bytes, iguales en placa y PC; SHA-256 20/20 comprobadas otra vez en el PC. B se queda desenchufada a la espera de Paco (oops de `ramoops`, supervisor sin arrancar en ese arranque).
Mejora para otra sesión: el puente (`ecm_bridge.py`) tiene una sola lectura USB en vuelo, ~1,5 kB por ida y vuelta de usbipd → ~1,5 MB/s. Probar 2–4 lecturas en vuelo con las placas sin grabar, vigilando fallos (el 28/10 hubo error 13 con ~2,5 MB/s por el puente).

**Verificación del montaje de E4-B:** en `wmu-20261029T114701Z` de B, 11:49:46–11:50:11 (pérdida de 1 min), IN_B tiene 25 pulsos de 1 s; a las 11:50:11 `PPSAgeMs` de B era 46 919 ms (su receptor sin pulso desde ~11:49:24). Los pulsos de IN_B venían de PPS_A.

## Fallos de adquisición (regla 7)

| UTC | Placa | Mensaje | Qué se estaba haciendo | journalctl / dmesg guardados |
| --- | --- | --- | --- | --- |
| 08:17:04 | A y B | `PRU stopped acquisition: error 13` | Probablemente enchufando los Fluke (Fran) | `A-fallo-1.log`, `B-fallo-1.log` |
| 08:36:17 | A y B | `PRU stopped acquisition: error 9` | Por confirmar: ¿conexión del osciloscopio? | `A-fallo-2.log`, `B-fallo-2.log` |
| 08:36:22, 08:36:27 | A | `error 13` ×2 → `m4_fault`, reinicio 08:36:30 | | `A-fallo-2.log` |
| 09:12:30 | A | `error 13` | Quitando el osciloscopio | `A-fallo-3.log` |
| 09:13:12 | B | `error 9` | Quitando el 8558A | `B-fallo-3.log` |
| 09:14:47 | B | `error 9` → `m4_fault`, reinicio 09:14:48 | Quitando la tensión (Fran) | `B-fallo-4-reinicio.log` |

## Cierre y datos

| Comprobación | A | B |
| --- | --- | --- |
| Sondeo E4 finalizado | | |
| STOP / grabador terminado | | |
| Copia verificada: ficheros / bytes / SHA-256 | | |
| Hora de apagado | | |

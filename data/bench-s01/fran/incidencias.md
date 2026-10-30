# Sesión 01 (2026-10-27): incidencias para Paco

Horas en UTC.

## Reinicios provocados por la copia de datos

- 12:02–12:18: se copian los ficheros cerrados de las dos placas por scp, uno a uno, a ~1,2 MB/s cada placa (`copy-board.sh`).
- B, 12:11:19: el kernel registra `SLUB: Unable to allocate … GFP_ATOMIC, cache: skbuff_head_cache` y, en el mismo segundo, la adquisición falla con `Lost or replayed PRU packet` → `m4_fault` → reinicio por watchdog (12:11:28). Boot anterior `949f1417…`.
- A: el mismo fallo a las 12:11:11 y a las 12:13:14 (ventanas de recuperación), y una tercera vez a las 12:18:06 → `m4_fault` → reinicio. Boot anterior `80b42f2d…`.
- El primer fichero grande (~460 MB) pasó sin fallos en las dos placas; los fallos llegan con el segundo, cuando la caché ya ocupa la RAM libre (`min_free_kbytes` = 3882).
- Copias paradas a las ~12:18. Copiado y verificado con SHA-256: A 11/20 ficheros, B 9/17 (`A/SHA256SUMS`, `B/SHA256SUMS`). El resto sigue en `/data/bench-193` de cada placa.
- Hipótesis: con la RAM libre en el mínimo, la reserva atómica de skb en la recepción rpmsg falla y el paquete del PRU se pierde. Pendiente de tu revisión: ¿subir `vm.min_free_kbytes`, limitar la velocidad de copia o leer con O_DIRECT?

## Fallos sueltos sin copia en marcha (después de los reinicios)

| Placa | UTC | Error | Nota |
| --- | --- | --- | --- |
| B | 12:24:50 | Lost or replayed PRU packet | |
| A | 12:31:23 | Lost or replayed PRU packet | |
| B | 12:38:46 | PRU stopped acquisition: error 13 | cerca del apagado de un Fluke (usbipd cayó a las 12:33:48) |
| B | 12:42:37 | Lost or replayed PRU packet | 10 s antes de que el grabador cerrara su último fichero |
| A | 12:49:35 | Lost or replayed PRU packet | 9 s antes de que el grabador cerrara su último fichero |
| A | 12:57:27 | Lost or replayed PRU packet | con `SLUB: Unable to allocate` en el mismo segundo; grabador ya parado, 29 MB libres |

- Con el grabador en marcha, la RAM libre estaba en ~10 MB y la caché en ~790 MB en las dos placas.
- Ninguno de estos fallos reinició las placas, pero cada uno reinicia el plazo de 30 min para el crédito. Las dos placas gastaron las 3 ventanas de recuperación del arranque. B tuvo crédito a las 13:13 (comprobado a las 13:23).

## Otras

- 12:33:48: el servicio usbipd de Windows se cerró solo (evento 7034); las placas quedaron fuera de WSL. Se arrancó otra vez a las ~12:39 y se sustituyeron los ayudantes `--auto-attach`, que se habían quedado colgados. La reconexión no produjo fallos en las placas.
- `STOP` creado a las 12:32:20 en las dos placas. Grabadores cerrados limpiamente: B a las 12:42:47 y A a las 12:49:44 (`stop: PMU and WMU back to the defaults`).

## 2026-10-28: copia del resto del 27/10

- Las dos placas arrancaron a las ~07:07 sin bloqueo, con el crédito de ayer. `STOP` presente, grabadores parados y sin GPS; el reloj de las placas seguía en la hora del 27/10.
- Nuevo script `copy-board-direct.sh`:
  - lee con `dd iflag=direct`, de modo que la caché no crece (comprobado) y la RAM libre se queda en ~730 MB;
  - empieza solo con crédito y se para ante cualquier fallo nuevo de adquisición.
- ~07:46, copiando las dos placas a la vez (~1,25 MB/s cada una): `PRU stopped acquisition: error 13` (cadencia/DRDY) en B y, unos 10 s después, en A, sin aviso de memoria. Una ventana de recuperación en cada placa; sin reinicios.
- 07:51–08:14, copiando de una placa en una (~1,4–1,6 MB/s): ningún fallo. Indicio de que influye copiar las dos a la vez por el mismo puente.
- Resultado: están todos los ficheros del 27/10, con tamaño y SHA-256 comprobados en la placa y en el PC (B: 20 ficheros, 4,7 GB; A: 25 ficheros, 4,8 GB). Los ficheros del 16/10 y del 22/10 siguen en las placas.

## Boots de la sesión, por si hace falta readmitir

- B: `3526c33569e24832a5ed89b1c872af95`, desde las 12:11:35.
- A: `4f8f4b377345486c98c2e82db239a13b`, desde las 12:19:25.

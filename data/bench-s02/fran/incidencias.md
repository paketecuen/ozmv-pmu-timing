# Sesión 02 (2026-10-29): incidencias para Paco

Horas en UTC. El detalle está en [registro-sesion.md](registro-sesion.md) y en los logs de esta carpeta; las grabaciones, en la carpeta de datos `sesion-02` (A, B, fluke) que se entrega aparte.

## Fallos de adquisición: todos al tocar el montaje

| Placa | UTC | Error | Qué se hacía |
| --- | --- | --- | --- |
| A y B | 08:17:04 | `PRU stopped acquisition: error 13` | Enchufando los Fluke (cableado de M2) |
| A y B | 08:36:17 | `error 9` | Conectando el osciloscopio |
| A | 08:36:22, 08:36:27 | `error 13` ×2 → `m4_fault` → **reinicio 08:36:30** | Ídem; con crédito, sin bloqueo |
| A | 09:12:30 | `error 13` | Quitando el osciloscopio |
| B | 09:13:12 | `error 9` | Quitando el 8558A |
| B | 09:14:47 | `error 9` → `m4_fault` → **reinicio 09:14:48** | Quitando la tensión; con crédito, sin bloqueo |
| A | 11:36:46 | `error 9` | Tras recablear el IN de B para E4-B |
| B | 11:38:11 | `error 13` | Ídem |

- En ninguno hay avisos de memoria en dmesg. `min_free_kbytes` estaba en 65536 desde las 08:02 y se repuso tras cada reinicio (en A estuvo sin reserva de 08:37 a 09:04).
- Sin tocar nada no hubo ningún fallo: M4-r02 (30 min), la serie de E4-A y la de E4-B.
- Tampoco fallaron al cruzar los ramales y dar tensión (09:48–09:49) ni al montar el PPS cruzado de E4-A.
- Reinicio planificado de A (`m4-control reboot`, regla 6) a las 10:44:11. Tras él, A queda **sin crédito** hasta las ~11:14.

## Copias

- Tras el día grabando, las placas tenían ~580 MB de caché limpia y 185–200 MB libres. `copy-board-direct.sh` se para por debajo de 200 MB **libres** (no mira la memoria disponible, que era ~590 MB). Se vació la caché (`sync; echo 1 > /proc/sys/vm/drop_caches`) antes de copiar: A a las 12:49, B a las 16:29.
- A se copió de 12:49 a 13:43 y B de 16:10 a 17:00, sin fallos, a ~1,5 MB/s. El puente lee una sola trama USB por cada ida y vuelta de usbipd, y eso limita la velocidad.
- Todo con SHA-256 comprobado en la placa y en el PC. Los originales siguen en `/data/bench-193` de cada placa.

## B: oops del kernel al volver a encenderla

- Las dos se apagaron hacia las 14:00, con crédito. B se encendió otra vez hacia las 16:00 para copiarla, con la antena bajo cubierta y sin GPS.
- A los 4,8 s del arranque: `Internal error: synchronous external abort` en `ramoops` (`persistent_ram_post_init`), con el kernel `Tainted: G M` (`tainted=144`).
- Con el kernel tainted, **no arrancan ni la adquisición** («Kernel is tainted») **ni el supervisor M4** («Kernel must be untainted before supervision»).
- Se copió sin la espera al crédito, con la adquisición parada. La memoria libre bajaba ~1 MB/min aunque la caché era pequeña.
- **B queda apagada y no se enciende hasta que la mires.** Detalle en [B-arranque-oops.log](B-arranque-oops.log). Arranque `bae9de3b-8e61-4b51-9320-3d2074d547bf`. Esa mañana B había arrancado dos veces sin este error.

## GPS

- E4-A-10: entró señal ~4 s (11:18:23–11:18:27, sin llegar a confirmar). Fue al meter la antena en la caja: se tarda 20–30 s desde que entra en la habitación.
- 11:13:15–11:13:39: A y B pierden la referencia a la vez (con Fix), sin tocar nada.
- E4-B: la antena de B tarda 1–2 min en perder la señal después de taparla. En la de 10 min, la tapa no bloqueó hasta recolocarla (12:18 → 12:26).
- Al arrancar los grabadores por la mañana: `LastRejoinNs` ~2 s (A) y ~1 s (B).

## Otras

- Osciloscopio: solo la medida b en A (cablecito del PPS frente a la entrada del 8558A), sin diferencia visible. La a no se puede hacer porque P2.17 queda oculto. B no se midió.
- Fluke: sin datos en esta sesión (`fluke/LEEME.md` en la carpeta de datos).
- Agilent 6811B: el panel frontal está muerto entero; sin probar por RS-232 o GPIB. E2 pendiente.
- Repositorio: no se puede hacer push a `main` (está protegida). Todo va en la rama `fran/kit-sesion-02`.

## Arranques de la sesión

- A: `2021b800-d3a4-482c-8624-6cd7686cb00c`, desde las 10:44:19 (reinicio planificado).
- B: `4d762a7e`, desde las 09:15:27; después del apagado, `bae9de3b-8e61-4b51-9320-3d2074d547bf` (el del oops).

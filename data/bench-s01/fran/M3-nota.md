# M3, sesión 01 (2026-10-27): nota para Paco

- Montaje: PPS del GPS de la **placa B** a las dos entradas (8588A maestro, 8558A esclavo), latiguillos de cada multímetro lo más cortos posible; disparo 8588A → entrada externa 8558A, como el 22/10.
- `fluke_capture.py` sin cambios: DIG:VOLT, intervalo 50 µs, apertura 49 µs, filtro OFF, 8558A a 10 V.
- `m3-10V-antes`: 08:23:02–08:25:02 UTC, 6 ráfagas × 200 000 lecturas, colas de error vacías.
- `m3-1000V-antes`: 08:25:02–08:27:01 UTC, ídem.
- El PPS se ve limpio en los dos: ~0,01 V → ~2,28 V, un flanco cada 20 000 lecturas.

## Resultado de `analysis/m2_fluke_phase.py m3`

`edges: 0`: descarta todos los flancos (113 a 10 V y 117 a 1000 V). El esclavo ve cada flanco **~0,98 lecturas antes en índice que el maestro**, es decir, un retraso de casi un intervalo de muestra, y `pair_edges` solo admite 0,5 lecturas de diferencia.

## Cálculo preliminar (no oficial)

Mismas funciones (`edge_positions`, `clean_edges`), emparejando con el maestro desplazado una lectura:

| Rango del maestro | Flancos emparejados / descartados | Retraso del esclavo | u de la media | Dispersión por flanco |
| --- | --- | --- | --- | --- |
| 10 V | 53 / 7 | 49 124,5 ns | 21,0 ns | 153 ns rms |
| 1000 V | 57 / 3 | 49 329,8 ns | 32,2 ns | 244 ns rms |

Diferencia entre rangos: ~205 ns. La dispersión por flanco es mayor que los ~82 ns que darían dos celdas de 200 ns independientes.

## Script ajustado y repetición (mismo día)

Cambio local en `analysis/m2_fluke_phase.py`, sin subir. `whole_reading_shift()` estima las lecturas enteras de retraso a partir de los flancos. `slave_lag()` empareja con ese desplazamiento y lo devuelve sumado, así que el resultado es el retraso total que M2 resta con `--lag-ns`. La salida de M3 añade `whole_reading_shifts`. M1 y M2 no cambian. La autocomprobación incluye 49 330 ns y 99 600 ns, que salen exactos, y los casos antiguos dan lo mismo que antes.

| Pasada (PPS de B) | UTC | Flancos / descartados | Desplazamiento | Retraso del esclavo | u | Por flanco |
| --- | --- | --- | --- | --- | --- | --- |
| m3-10V-antes | 08:23–08:25 | 53 / 7 | 1 | 49 124,5 ns | 21,0 | 153 ns |
| m3-10V-antes-r02 | 08:39–08:41 | 59 / 3 | 1 | 49 183,1 ns | 14,7 | 113 ns |
| m3-1000V-antes | 08:25–08:27 | 57 / 3 | 1 | 49 329,8 ns | 32,2 | 243 ns |
| m3-1000V-antes-r02 | 08:41–08:43 | 54 / 5 | 1 | 49 322,2 ns | 42,4 | 312 ns |

En 1000 V, el rango de M2, las dos pasadas coinciden: diferencia 7,6 ns, con una u combinada de 53 ns. En 10 V difieren 59 ns, unas 2,3 u combinadas: a vigilar con M3-después. La dispersión por flanco es mayor en 1000 V; probablemente influye el pulso de 2,3 V medido en un rango de 1000 V.

## Diagnóstico: el retraso es la apertura (carpeta `diag-apertura`, no son pasadas del ensayo)

Según el manual de programación del 8588A/8558A, `ROUTe:TOUTput` vale por defecto **ACO** ("pulse at end of acquisition"). Con **APE** ("squarewave active during acquisition") el flanco marca el inicio de la adquisición.

Pruebas con el PPS de B en los dos, maestro a 10 V, ACO e intervalo y apertura reducidos con 1 µs de hueco. Con 0,2 µs de hueco (10/9,8 µs) el 8558A pierde disparos y se queda armado; hubo que mandarle ABOR.

| Intervalo / apertura | UTC | Flancos | Retraso del esclavo | Exceso sobre la apertura |
| --- | --- | --- | --- | --- |
| 50 / 49 µs (M3 oficial, 10 V) | 08:23–08:41 | 53 + 59 | 49,12–49,18 µs | 0,12–0,18 µs |
| 10 / 9 µs | 10:50–10:52 | 18 | 9,133 µs ± 63 ns | 0,13 µs |
| 5 / 4 µs | 10:52–10:55 | 15 | 4,040 µs ± 49 ns | 0,04 µs |
| 3 / 2 µs | 10:55–11:01 | 12 | 2,100 µs ± 80 ns | 0,10 µs |

El esclavo arranca al final de cada adquisición del maestro, unos 0,1 µs después. Es del mismo orden que los ~0,1 µs de Lapuh et al. entre dos 8588A.

Propuesta: `fluke_capture.py --trig-out APE`, cambio local añadido. Envía `ROUT:TOUT APE` y `ROUT:TOUT:SLOP NEG` al maestro y `TRIG:EXT NEG,TTL` al esclavo, y lo registra en el JSON. Por defecto sigue en ACO.

## M3 con APE (mismo día, PPS de la placa A, 50 / 49 µs)

| Pasada | UTC | Flancos / descartados | Desplazamiento | Retraso del esclavo | u | Por flanco |
| --- | --- | --- | --- | --- | --- | --- |
| m3-10V-ape | 11:02–11:04 | 58 / 1 | 0 | 124,1 ns | 12,9 | 98 ns |
| m3-1000V-ape | 11:04–11:06 | 52 / 8 | 0 | 315,4 ns | 38,2 | 275 ns |

APE quita exactamente la apertura. Con ACO, restando 49 µs, quedan 124–183 ns a 10 V y 322–330 ns a 1000 V, en acuerdo con APE. El rango de 1000 V del maestro añade unos 190 ns frente al de 10 V. Ninguna ráfaga dio errores. El JSON registra `trig_out: APE`.

## M3 después de M2, con APE (PPS de la placa B, 50 / 49 µs)

| Pasada | UTC | Ráfagas | Flancos / descartados | Desplazamiento | Retraso del esclavo | u | Por flanco |
| --- | --- | --- | --- | --- | --- | --- | --- |
| m3-10V-despues-ape | 12:06–12:09 | 6 | 57 / 3 | 0 | 112,3 ns | 13,3 | 100 ns |
| m3-1000V-despues-ape | 12:08–12:28 | 60 | 552 / 45 | 0 | 326,4 ns | 8,9 | 210 ns |

Coincide con el M3 APE de la mañana (PPS de A): a 10 V la diferencia es de 11,8 ns con u combinada de 18,5 ns, y a 1000 V es de 11,0 ns con u combinada de 39 ns. Para M2 (1000 V) proponemos `--lag-ns 326`, de esta pasada, que es la de más flancos.

La placa B se reinició a las 12:11:28 UTC, en mitad de la pasada de 1000 V (ver incidencias de la sesión). Aun así salen 597 flancos de unos 600 esperados: el PPS del GPS siguió durante el reinicio.

**Pregunta:** ¿es esperable ese retraso de casi una lectura? Por ejemplo, que el 8588A dé su pulso de disparo al final de cada lectura y el 8558A mida en el siguiente. ¿Prefieres que el análisis lo admita, con un desplazamiento entero estimado de los datos, o cambiar la configuración del disparo y repetir M3? Los datos originales están intactos en esta carpeta.

# Changelog — 3Dii-U Builder MOD

Todas las versiones estables verificadas en consola real (Wii U) se registran aquí.
A partir de `v2.0.0`, no se sobrescriben tags publicados: cada mejora verificada avanza de versión (`v2.0.1`, `v2.0.2`, etc.).

## [v2.0.0] — Base estable MOD (2026-11-24)
### Añadido
- Interfaz del Builder traducida y adaptada al español (`builder/gui.py`, `builder/app.py`, `builder/updater.py`, `builder/compat.json`).
- Sistema de auto-actualización apuntando al repositorio `supermafari007/3Dii-U-Builder-MOD` con soporte de etiquetas `vX.Y.Z`.
- Empaquetado completo de huellas de shaders de vértices/geometría (`.vsf`) en `builder/make_app.py` (`152` recetas de vértices, `0` omitidos).
- Perfiles multirregión en `builder/compat.json` (USA/EUR/JPN para *New Super Mario Bros. 2*, *Mario Kart 7*, *A Link Between Worlds* y *Star Fox 64 3D*).
- Documentación reorganizada en `README.md` y herramienta local de diagnóstico de logs de Wii U (`tools/inspect_wiiu_logs.py`).

### Estabilidad
- Motor de runtime (`gsp.cpp`, `gx2_backend.cpp`, `kernel.cpp`, `y2r.cpp`), recompilador (`gen.py`) y política de modo mixto (`pipeline.py`) mantenidos en la base estable validada.
- Política de cambios: modificaciones en runtime/recompilador se prueban de una en una contra juegos canario (*NSMB2*, *Mario Kart 7*, *Metroid: Samus Returns* / *Majora's Mask 3D*) antes de publicar una nueva versión.

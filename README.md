# Voxify AI — VST3 для FL Studio (JUCE 8 / C++17)

## Сборка (Windows)
```
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```
JUCE скачивается автоматически (нужен интернет при первой настройке). VST3 копируется в
`C:\Program Files\Common Files\VST3`. В FL Studio: Options → Manage plugins → Find installed plugins.

## Цепочка
Noise Reduction → Auto-Tune → EQ → Compressor → De-Esser → Saturation → Exciter → Doubler →
Reverb/Delay → Stereo Width → Limiter. У каждого модуля свой ON и ручка 0–100; AI MIX масштабирует всё, кроме шумодава.

## Что реально сделано (честно)
- **Анализ вокала**: уровень, пики, шум, 6 спектральных полос, высота (автокорреляция), тональность (профили Krumhansl).
- **«AI» = эвристики DSP, не нейросеть.** Auto Mix берёт пресет стиля и сдвигает значения по измеренному спектру.
  Целевой наклон (`defaultTargetTilt` в Analysis.h) и пороги **не откалиброваны** — подстройте на реальных вокалах.
- **Пресеты артистов** (Madk1d, Villian, Morgenshtern, 9mice, Платина, Og Buda) — мои ручные приближения «в духе», не снятые цепочки.
- **Reference Match**: анализирует файл (WAV/FLAC, лучше акапелла) и использует его спектральный наклон как цель для Smart EQ.
- **Beat Detection** = темп хоста FL Studio (по одному вокалу BPM из аудио не определить).
- **Vocal vs Beat**: подайте бит в sidechain-вход плагина (роутинг в миксере FL) — коуч покажет разницу уровней.
- **Key Detection** — по самому вокалу, не по биту.
- **Before/After** — кнопка BEFORE (bypass с компенсацией задержки). **A/B** — два слота настроек. **UNDO** откатывает последний Auto Mix / смену стиля.
- **AI Vocal Coach** — текстовые подсказки по метрикам (на русском).

## Ограничения v0.1
- Код **не компилировался и не тестировался** (в среде не было JUCE) — возможны ошибки сборки; пришлите лог, поправлю.
- Auto-Tune — простой time-domain pitch shifter (2 окна): артефакты на сильной коррекции, моно-голос, задержка ~16 мс + 1.5 мс (репортится хосту).
- Сатурация без оверсэмплинга; лимитер короткий look-ahead, не true-peak.
- Нет отдельных Adlib-обработок кроме режима ADLIB (HPF 300 Гц, больше reverb/delay/width).

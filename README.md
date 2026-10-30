# X4Libris

Эта прошивка основана на [Witch(hunt) Reader](https://github.com/jpirnay/witchhunt-reader).

Она поддерживает устройство:

- Xteink X4 Pro на ESP32-S3.

## Скачать

- [X4Libris v1.0 — страница выпуска](https://github.com/NikolayNR70/X4Libris-X4Pro/releases/tag/X4Libris-v1.0)
- В выпуске находятся образ прошивки, совместимый пакет веб-интерфейса и контрольные суммы.

## Возможности

- Чтение EPUB, FB2, FB2.zip/FBZ, XTC/XTCH, TXT и Markdown.
- Прямое использование шрифтов TTF и OTF с карты памяти.
- Библиотека, обложки, недавние книги, поиск и закладки.
- Синхронизация KOReader и каталоги OPDS.
- Календарь, «Сегодня», статистика чтения и погода.
- Русскоязычный веб-интерфейс для управления книгами, настройками и шрифтами.

[Полное описание возможностей](FUNCTIONALITY-RU.md) · [Установка веб-интерфейса](docs/web-interface-ru.md) · [Сборка прошивки](BUILD-REPRODUCIBILITY.md)

## Сборка

Требуются PlatformIO Core и Python 3.13. Из папки репозитория выполните:

```powershell
py -3.13 -m platformio run -e x4pro_gh_release
```

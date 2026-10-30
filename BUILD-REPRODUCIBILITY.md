# X4Libris v1.0 — запись о сборке

## Целевая платформа

- Устройство: Xteink X4 Pro, ESP32-S3.
- Цель PlatformIO: `x4pro_gh_release`.
- Python: 3.13.
- Команда сборки: `py -3.13 -m platformio run -e x4pro_gh_release`.
- Исходный код: commit `284a924b906e47ffe79a7d0a067f1c3ddd6f44be`.
- Репозиторий и канал веб-пакета: `NikolayNR70/X4Libris-X4Pro`, `X4Libris-v1.0`.

## Комплект X4Libris v1.0

Сборка из зафиксированных исходников выполнена 30 октября 2026 года. Файлы комплекта находятся в `artifacts/review/X4Libris-v1.0-20261030-071649/`.

| Файл | Размер | SHA-256 |
|---|---:|---|
| `X4Libris-X4Pro-v1.0.bin` | 5 492 480 байт | `E43C780E2A09D9FCD015E7CF86BBBADC3EB4CD490A8D93F097B2F61F36786100` |
| `firmware-x4pro.bin` | 5 492 480 байт | `E43C780E2A09D9FCD015E7CF86BBBADC3EB4CD490A8D93F097B2F61F36786100` |
| `web-d50671bf7e39.bin` | 746 704 байта | `D50671BF7E39EA6C32D20C466BB2F27EC5EE9552373BF8A7DE1EEF30CD00A86A` |

Оба имени прошивки содержат побайтно одинаковый образ. Сборка PlatformIO успешна: ОЗУ 110 152 / 327 680 байт; раздел приложения 5 487 477 / 6 553 600 байт. `SHA256SUMS.txt` в папке комплекта подтверждает файлы.

Скрипт `scripts/package_x4libris_release.ps1` собирает прошивку, имя OTA, веб-пакет и манифест в новую папку `artifacts/review/`, не перезаписывая предыдущие комплекты.

## Структура истории

Описание функций и инструкции находятся в `FUNCTIONALITY-RU.md`, `docs/web-interface-ru.md` и `README.md`. Снимки старых исходных заметок сохранены в `docs/history/`.

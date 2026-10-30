# X4Libris v1.0 — запись о сборке

## Целевая платформа

- Устройство: Xteink X4 Pro, ESP32-S3.
- Цель PlatformIO: `x4pro_gh_release`.
- Python: 3.13.
- Команда сборки: `py -3.13 -m platformio run -e x4pro_gh_release`.
- Исходный код: commit `284a924b906e47ffe79a7d0a067f1c3ddd6f44be`.
- Репозиторий и канал веб-пакета: `NikolayNR70/X4Libris-X4Pro`, `X4Libris-v1.0`.

## Комплект X4Libris v1.0

Сборка из зафиксированного исходного кода выполнена 30 октября 2026 года. Комплект опубликован на странице [выпуска X4Libris v1.0](https://github.com/NikolayNR70/X4Libris-X4Pro/releases/tag/X4Libris-v1.0).

| Файл | Размер | SHA-256 |
|---|---:|---|
| `X4Libris-X4Pro-v1.0.bin` | 5 492 480 байт | `E43C780E2A09D9FCD015E7CF86BBBADC3EB4CD490A8D93F097B2F61F36786100` |
| `firmware-x4pro.bin` | 5 492 480 байт | `E43C780E2A09D9FCD015E7CF86BBBADC3EB4CD490A8D93F097B2F61F36786100` |
| `web-d50671bf7e39.bin` | 746 704 байта | `D50671BF7E39EA6C32D20C466BB2F27EC5EE9552373BF8A7DE1EEF30CD00A86A` |

Оба имени прошивки содержат побайтно одинаковый образ. Сборка PlatformIO успешна: ОЗУ 110 152 / 327 680 байт; раздел приложения 5 487 477 / 6 553 600 байт. Опубликованный `SHA256SUMS.txt` подтверждает файлы. Прямые ссылки: [образ прошивки](https://github.com/NikolayNR70/X4Libris-X4Pro/releases/download/X4Libris-v1.0/X4Libris-X4Pro-v1.0.bin), [имя для OTA](https://github.com/NikolayNR70/X4Libris-X4Pro/releases/download/X4Libris-v1.0/firmware-x4pro.bin), [веб-пакет](https://github.com/NikolayNR70/X4Libris-X4Pro/releases/download/X4Libris-v1.0/web-d50671bf7e39.bin), [контрольные суммы](https://github.com/NikolayNR70/X4Libris-X4Pro/releases/download/X4Libris-v1.0/SHA256SUMS.txt).

Скрипт `scripts/package_x4libris_release.ps1` собирает прошивку, имя OTA, веб-пакет и манифест в новую папку `artifacts/review/`, не перезаписывая предыдущие комплекты.

## Структура истории

Описание функций и инструкции находятся в `FUNCTIONALITY-RU.md`, `docs/web-interface-ru.md` и `README.md`. Снимки старых исходных заметок сохранены в `docs/history/`.

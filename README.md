# VkPublications

Qt5-приложение для управления публикацией вакансий в группах ВКонтакте:
импорт вакансий из XML, автоматическая публикация/репост по расписанию,
очистка стены сообщества. Публикация и очистка стены выполняются внешними
Python-воркерами (`publicate.py`, `clear_wall.py`), которые запускаются
приложением как отдельные процессы.

## Зависимости

- Qt5 (Widgets, Sql, Network) + qmake
- Приватные заголовки Qt5 (нужны для встроенной библиотеки QXlsx):
  Ubuntu/Debian — пакет `qtbase5-private-dev`
- Компилятор C++11
- Драйвер SQLite для Qt (`libqt5sql5-sqlite` в Ubuntu/Debian)

Пример установки на Ubuntu/Debian:

```bash
sudo apt-get install qtbase5-dev qt5-qmake qtbase5-dev-tools \
    qtbase5-private-dev libqt5sql5-sqlite build-essential
```

## Сборка

```bash
qmake VkPublications.pro
make -j"$(nproc)"
```

В репозиторий включены сторонние библиотеки в виде исходников (не
git-сабмодули, а copy-in исходников, как их и подключает `.pro`-файл):

- `QXlsx/` — экспорт в Excel ([QtExcel/QXlsx](https://github.com/QtExcel/QXlsx), MIT)
- `pugixml/` — разбор XML ([zeux/pugixml](https://github.com/zeux/pugixml), MIT)
- `sqlite/` — амальгамация SQLite ([azadkuh/sqlite-amalgamation](https://github.com/azadkuh/sqlite-amalgamation), public domain);
  используется, чтобы приложение могло регистрировать собственную SQL-функцию
  (`LIKE_NOCASE`) через C API sqlite3 поверх соединения, открытого драйвером Qt.

## Воркеры публикации

`publicate.py` и `clear_wall.py` запускаются приложением как
`./publicate/publicate.exe` и `./clear_wall/clear_wall.exe` рядом с
исполняемым файлом приложения — их нужно собрать отдельно (например,
PyInstaller) и положить в соответствующие подпапки рядом с бинарником.
Зависимости самих скриптов: `vk_api`, `requests`, `pandas`.

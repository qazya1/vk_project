import sqlite3
import vk_api
from vk_api.exceptions import VkApiError, ApiError
from datetime import datetime, timedelta
import time
import json
import re
import argparse
import requests
import random
from typing import List, Dict, Optional, Tuple
import os
import traceback
import pandas as pd
import sys

def show_error_window(message: str):
    """Показать окно с ошибкой в Windows"""
    try:
        import tkinter as tk
        from tkinter import messagebox
        
        # Создаем окно (скрытое)
        root = tk.Tk()
        root.withdraw()
        
        # Показываем сообщение об ошибке
        messagebox.showerror("Ошибка публикации вакансий", message)
        
        # Закрываем окно
        root.destroy()
    except ImportError:
        # Если tkinter не установлен, просто выводим в консоль
        print(f"Не удалось отобразить окно ошибки. tkinter не установлен.")
        print(f"Сообщение об ошибке: {message}")
    except Exception as e:
        print(f"Ошибка при отображении окна: {str(e)}")

def write_log(s):
    global log_file
    timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    log_file.write(f"[{timestamp}] {str(s)}\n")
    log_file.flush()
    # Windows-консоль может не поддерживать все Unicode-символы —
    # используем encode с заменой нечитаемых символов вместо print()
    try:
        line = f"[{timestamp}] {s}\n"
        sys.stdout.buffer.write(line.encode(sys.stdout.encoding or "utf-8", errors="replace"))
        sys.stdout.flush()
    except Exception:
        pass  # если вывод в консоль недоступен — игнорируем

def convert_html(html: str) -> str:
    """Конвертирует HTML-текст в форматированный plain text."""
    text = html
    
    # Заменяем теги
    text = text.replace("<BR/>", "\n").replace("<br/>", "\n")
    text = text.replace("<B>", "").replace("<b>", "")
    text = text.replace("</B>", "").replace("</b>", "")
    
    # Заменяем элементы списка
    text = text.replace('<ul style="list-style:none">', "").replace("</ul>", "")
    text = text.replace("<li>", "\n")
    
    # Заменяем HTML-сущности
    replacements = {
        "&nbsp;": " ", "&amp;": "&", "&lt;": "<", 
        "&gt;": ">", "&quot;": '"', "&apos;": "'"
    }
    for k, v in replacements.items():
        text = text.replace(k, v)
    
    # Удаляем оставшиеся HTML-теги
    text = re.sub(r'<[^>]*>', '', text)
    
    return text

def get_state():
    return "".join([random.choice("abcdefghijkmnlopqrstuvwzyx_-0123456789"+"abcdefghijkmnlopqrstuvwzyx".upper()) for i in range(random.randint(32, 64))])

def similarity_ratio(s1: str, s2: str) -> float:
    """Вычисляет коэффициент схожести строк от 0.0 до 1.0"""
    if not s1 or not s2:
        return 0.0
    s2 = s2[:-2]
    s1 = s1[:-2]
    n = 0
    min_len = min(len(s1), len(s2))
    for i in range(min_len):
        if s1[i] == s2[i]:
            n += 1
        else:
            break
    return n/min_len

class VKPostPublisher:
    def __init__(self, config: Dict):
        write_log("Инициализация VKPostPublisher...")
        self.config = config
        self.vk_session = None
        self.group_id = self.config['group_id']
        self.refresh_token = self.config['refresh_token']
        self.client_id = self.config['client_id']
        self.device_id = self.config['device_id']
        
        self.vk = None
        self.db_connection = None
        
        time.sleep(3)
        self._init_db_connection()
        
        # §13 ТЗ: синхронизация всех статусов при старте publicate.py
        self._sync_all_announcement_statuses('publisher_startup')
        
        self._init_vk_connection()
        
        # Параметры публикации
        self.post_interval = config.get('post_interval', 10)
        self.start_time = config.get('start_time', '08:00')
        self.end_time = config.get('end_time', '20:00')
        self.round_the_clock = config.get('round_the_clock', False)
        self.repost_enabled = config.get('repost_enabled', True)
        self.repost_interval_days = config.get('repost_interval_days', 3)
        self.merge_vacancies = config.get('merge_vacancies', True)
        self.merge_by_number = config.get('merge_by_number', True)
        self.hide_company_names = config.get('hide_company_names', False)
        self.vacancy_filter = config.get('vacancy_filter', "")
        self.salary_threshold = config.get('salary_threshold', 50000)
        self.salary_text = 'По договоренности'
        self.hide_additional_info = config.get('hide_additional_info', False)
        self.hide_address = config.get('hide_address', False)
        self.similarity_threshold = 0.8  # Порог схожести 80%
        self.delete_all_posts = config.get('delete_all_posts', False)
        self.merge_periodically = config.get('merge_periodically', False)
        self.merge_interval_days = config.get('merge_interval_days', 3)
        self.last_merge_date = None
        self.hide_salary = config.get('hide_salary', False)
        
        # Загружаем фильтры вакансий из файла
        self.allowed_first_words = self._load_allowed_first_words()
        write_log("VKPostPublisher инициализирован успешно")
    
    def _load_allowed_first_words(self) -> List[str]:
        """
        Загружает первые слова разрешённых вакансий из файла фильтрации.

        Поддерживаемые форматы XLSX:
          1) Колонка с заголовком "вакансия" (регистр не важен), значения ниже.
          2) Любая первая колонка, значения ниже.
          3) "Одно слово в A1" (когда список оформлен как заголовок колонки, а строк данных нет).
          4) "Список без заголовка" (если pandas съел первую строку как header) — подстраховка.
        """
        write_log(f"Попытка загрузки разрешенных первых слов вакансий из файла: {self.vacancy_filter}")

        if not self.vacancy_filter or not os.path.exists(self.vacancy_filter):
            write_log(f"Файл с названиями вакансий не найден: {self.vacancy_filter}")
            write_log("ВНИМАНИЕ: Без файла фильтрации будут публиковаться ВСЕ вакансии!")
            return []

        def _is_meaningful_text(s: str) -> bool:
            if s is None:
                return False
            s = str(s).strip()
            if not s:
                return False
            sl = s.lower()

            # pandas часто создаёт такие заголовки для пустых колонок
            if sl.startswith("unnamed"):
                return False

            # технические/служебные заголовки
            if sl in {
                "nan", "none", "null",
                "вакансия", "вакансии", "должность", "название", "наименование", "позиция", "профессия",
                "job", "jobs", "vacancy", "vacancies", "position", "title",
            }:
                return False

            # только цифры — не похоже на название вакансии
            if re.fullmatch(r"\d+", sl):
                return False

            return True

        try:
            file_extension = os.path.splitext(self.vacancy_filter)[1].lower()

            if file_extension == ".xlsx":
                write_log("Загрузка разрешенных названий вакансий из XLSX файла")
                df = pd.read_excel(self.vacancy_filter, dtype=str)

                # Ищем столбец "вакансия" (регистронезависимо)
                vacancy_column = None
                for col in df.columns:
                    if str(col).strip().lower() == "вакансия":
                        vacancy_column = col
                        break

                if vacancy_column is None and len(df.columns) > 0:
                    vacancy_column = df.columns[0]
                    write_log(f"Столбец 'вакансия' не найден, используем первый столбец: {vacancy_column}")
                else:
                    write_log(f"Найден столбец с вакансиями: {vacancy_column}")

                candidates: List[str] = []

                # ВАЖНО: если человек положил единственное слово в A1, pandas воспринимает его как заголовок.
                # Поэтому добавляем заголовок колонки как кандидата, если он выглядит осмысленно.
                if vacancy_column is not None and _is_meaningful_text(vacancy_column):
                    candidates.append(str(vacancy_column).strip())

                # Добавляем значения из выбранного столбца (если есть строки)
                if vacancy_column is not None and vacancy_column in df.columns and not df.empty:
                    for value in df[vacancy_column].dropna().astype(str).str.strip():
                        if _is_meaningful_text(value):
                            candidates.append(value)

                # Если кандидатов нет (например, файл без заголовка и pandas "съел" первую строку),
                # пробуем перечитать как таблицу без header и взять все непустые ячейки.
                if not candidates:
                    write_log("Не удалось извлечь значения стандартным способом — пробуем читать XLSX без заголовка (header=None)")
                    df_raw = pd.read_excel(self.vacancy_filter, header=None, dtype=str)
                    for value in df_raw.stack().dropna().astype(str).str.strip():
                        if _is_meaningful_text(value):
                            candidates.append(value)

                # Преобразуем в множество первых слов
                first_words = set()
                for text in candidates:
                    first_word = self._get_first_word(text)
                    if first_word and _is_meaningful_text(first_word):
                        first_words.add(first_word.lower())

                write_log(
                    f"Загружено {len(first_words)} разрешенных первых слов вакансий из XLSX файла: "
                    f"{sorted(list(first_words))[:50]}{'...' if len(first_words) > 50 else ''}"
                )
                return list(first_words)

            # Для обратной совместимости - текстовый файл
            write_log("Загрузка разрешенных первых слов из текстового файла")
            first_words = set()
            with open(self.vacancy_filter, "r", encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    if _is_meaningful_text(line):
                        first_word = self._get_first_word(line)
                        if first_word and _is_meaningful_text(first_word):
                            first_words.add(first_word.lower())

            write_log(f"Загружено {len(first_words)} разрешенных первых слов вакансий")
            return list(first_words)

        except Exception as e:
            write_log(f"Ошибка загрузки разрешенных первых слов вакансий: {str(e)}")
            write_log(f"Трассировка ошибки:\n{traceback.format_exc()}")
            return []

    
    def _get_first_word(self, text: str) -> str:
        """Извлекает первое слово из строки"""
        if not text:
            return ""
        # Убираем знаки препинания и разбиваем по пробелам
        clean_text = re.sub(r'[^\w\s]', ' ', text)
        words = clean_text.strip().split()
        return words[0] if words else ""
    
    def _is_vacancy_allowed(self, vacancy_name: str) -> bool:
        """Проверяет, разрешена ли вакансия для публикации по первому слову"""
        if not self.allowed_first_words:
            write_log("Нет фильтрации по первым словам - разрешены все вакансии")
            return True
        
        if not vacancy_name:
            write_log(f"Название вакансии пустое - не разрешено")
            return False
        
        first_word = self._get_first_word(vacancy_name)
        if not first_word:
            write_log(f"Не удалось извлечь первое слово из '{vacancy_name}' - не разрешено")
            return False
        
        first_word_lower = first_word.lower()
        
        # Проверяем сходство с каждым разрешенным первым словом
        for allowed_first_word in self.allowed_first_words:
            if similarity_ratio(first_word_lower, allowed_first_word) >= self.similarity_threshold:
                write_log(f"Вакансия '{vacancy_name}' разрешена (сходство '{first_word_lower}' с '{allowed_first_word}' >= {self.similarity_threshold})")
                return True
        
        write_log(f"Вакансия '{vacancy_name}' не разрешена (первое слово '{first_word_lower}' не совпадает ни с одним разрешенным)")
        return False
    
    def _should_merge_vacancies(self) -> bool:
        """Проверяет, нужно ли выполнять объединение вакансий"""
        write_log("Проверка необходимости объединения вакансий...")
        if not self.merge_periodically:
            write_log("Объединение периодически отключено - всегда объединяем")
            return True
        
        if self.last_merge_date is None:
            write_log("Первое объединение - выполняем")
            self.last_merge_date = datetime.now()
            return True
        
        next_merge_date = self.last_merge_date + timedelta(days=self.merge_interval_days)
        should_merge = datetime.now() >= next_merge_date
        write_log(f"Нужно объединять: {should_merge}")
        return should_merge
    
    def _update_merge_date(self):
        """Обновляет дату последнего объединения"""
        write_log("Обновление даты последнего объединения")
        self.last_merge_date = datetime.now()
    
    def get_access_token(self):
        """Функция для получения access token"""
        write_log("=== НАЧАЛО ОБНОВЛЕНИЯ ТОКЕНА ===")
        state = get_state()
        json_data = {
            "client_id": self.client_id,
            "grant_type": "refresh_token",
            "refresh_token": self.refresh_token,
            "device_id": self.device_id,
            "state": state
        }

        write_log(f"Параметры запроса:")
        write_log(f"  - client_id: {self.client_id}")
        write_log(f"  - grant_type: refresh_token")
        write_log(f"  - refresh_token: {'*' * 20}{self.refresh_token[-6:] if len(self.refresh_token) > 6 else ''}")
        write_log(f"  - device_id: {self.device_id[:10]}...{self.device_id[-10:]}")
        write_log(f"  - state: {state}")
        write_log(f"Endpoint: https://id.vk.com/oauth2/auth")

        try:
            write_log("Отправка POST запроса на обновление токена...")
            start_time = time.time()
            r = requests.post("https://id.vk.com/oauth2/auth", data=json_data)
            request_time = time.time() - start_time

            write_log(f"Запрос выполнен за {request_time:.2f} секунд")
            write_log(f"HTTP статус код: {r.status_code}")

            response = r.json()
            write_log("Ответ получен в формате JSON")
            write_log(f"Полный ответ сервера (JSON): {json.dumps(response, ensure_ascii=False)}")

            if 'error' in response:
                error_code = response.get('error', 'unknown_error')
                error_description = response.get('error_description', 'Описание ошибки отсутствует')
                raise Exception(f"VK OAuth ошибка: {error_code} - {error_description}")

            required_fields = ['access_token', 'refresh_token', 'expires_in']
            missing_fields = [field for field in required_fields if field not in response]
            if missing_fields:
                raise Exception(f"Ответ не содержит обязательных полей: {missing_fields}")

            if response.get("state") != state:
                write_log(f"ПРЕДУПРЕЖДЕНИЕ: коды состояния не совпадают: ожидалось {state}, получено {response.get('state')}")

            self.refresh_token = response["refresh_token"]

            cursor = self.db_connection.cursor()
            cursor.execute(
                "UPDATE vk_groups SET refresh_token = ? WHERE group_id = ?",
                (self.refresh_token, self.group_id)
            )
            self.db_connection.commit()
            write_log(f"Refresh token обновлен в SQLite для VK group_id={self.group_id}")

            write_log("=== ОБНОВЛЕНИЕ ТОКЕНА УСПЕШНО ЗАВЕРШЕНО ===")
            return response["access_token"]

        except requests.exceptions.RequestException as e:
            write_log(f"ОШИБКА СЕТИ ПРИ ЗАПРОСЕ ТОКЕНА: {str(e)}")
            raise Exception(f"Сетевая ошибка при запросе токена: {str(e)}")

        except json.JSONDecodeError as e:
            write_log(f"ОШИБКА: Не удалось распарсить ответ как JSON: {str(e)}")
            raise Exception(f"Некорректный JSON в ответе: {str(e)}")

        except Exception as e:
            write_log(f"НЕИЗВЕСТНАЯ ОШИБКА ПРИ ПОЛУЧЕНИИ ТОКЕНА: {str(e)}")
            write_log(f"Трассировка ошибки: {traceback.format_exc()}")
            raise

    def _init_vk_connection(self):
        """Инициализация подключения к VK API"""
        write_log("Инициализация подключения к VK API...")
        try:
            access_token = self.get_access_token()
            self.vk_session = vk_api.VkApi(token=access_token)
            self.vk = self.vk_session.get_api()
            write_log("Подключение к VK API успешно установлено")
        except Exception as e:
            error_msg = f"Ошибка подключения к VK API: {str(e)}"
            write_log(error_msg)
            write_log(f"Трассировка ошибки:\n{traceback.format_exc()}")
            show_error_window(f"Ошибка подключения к VK API:\n{str(e)}\n\nПроверьте параметры подключения.")
            raise Exception(error_msg)
    
    def _init_db_connection(self):
        """Инициализация подключения к SQLite"""
        write_log(f"Инициализация подключения к SQLite: {self.config.get('db_path', 'vacancies.db')}")
        try:
            db_path = self.config.get('db_path', 'vacancies.db')
            # timeout=30: ждать до 30 секунд если БД заблокирована
            self.db_connection = sqlite3.connect(db_path, timeout=30)
            self.db_connection.row_factory = sqlite3.Row

            self.db_connection.execute("PRAGMA foreign_keys = ON")
            # WAL + autocommit
            self.db_connection.execute("PRAGMA journal_mode = WAL")
            self.db_connection.execute("PRAGMA busy_timeout = 30000")
            # isolation_level=None: autocommit, скрипт не держит write-lock между операциями
            self.db_connection.isolation_level = None
            
            def icontains(text, pattern):
                if text is None or pattern is None:
                    return False
                return pattern.lower() in text.lower()
            
            self.db_connection.create_function("ICONTAINS", 2, icontains)
            
            # Миграция БД: новые поля и таблицы по ТЗ
            self._run_db_migration()
            
            write_log("Подключение к SQLite успешно установлено")
            
        except sqlite3.Error as e:
            error_msg = f"Ошибка подключения к SQLite: {str(e)}"
            write_log(error_msg)
            write_log(f"Трассировка ошибки:\n{traceback.format_exc()}")
            show_error_window(f"Ошибка подключения к базе данных:\n{str(e)}\n\nПроверьте путь к файлу базы данных.")
            raise Exception(error_msg)

    def _run_db_migration(self):
        """Выполняет миграцию БД: добавляет новые поля, таблицы, view по ТЗ."""
        write_log("Проверка и выполнение миграции БД...")
        cursor = self.db_connection.cursor()

        # --- announcements: новые колонки ---
        announcements_columns = {
            'status': "ALTER TABLE announcements ADD COLUMN status TEXT NOT NULL DEFAULT 'Активное'",
            'is_publication_blocked': "ALTER TABLE announcements ADD COLUMN is_publication_blocked INTEGER NOT NULL DEFAULT 0",
            'status_updated_at': "ALTER TABLE announcements ADD COLUMN status_updated_at TEXT",
            'status_update_source': "ALTER TABLE announcements ADD COLUMN status_update_source TEXT",
            'manual_finished_at': "ALTER TABLE announcements ADD COLUMN manual_finished_at TEXT",
            'manual_finished_reason': "ALTER TABLE announcements ADD COLUMN manual_finished_reason TEXT",
        }
        existing_ann_cols = {row[1] for row in cursor.execute("PRAGMA table_info(announcements)").fetchall()}
        for col_name, ddl in announcements_columns.items():
            if col_name not in existing_ann_cols:
                try:
                    cursor.execute(ddl)
                    write_log(f"Добавлена колонка announcements.{col_name}")
                except sqlite3.OperationalError:
                    pass

        # --- announcement_publications: новые колонки ---
        pub_columns = {
            'publication_state': "ALTER TABLE announcement_publications ADD COLUMN publication_state TEXT NOT NULL DEFAULT 'not_published'",
            'real_depublication_date': "ALTER TABLE announcement_publications ADD COLUMN real_depublication_date TEXT",
            'depublication_error': "ALTER TABLE announcement_publications ADD COLUMN depublication_error TEXT",
            'last_state_updated_at': "ALTER TABLE announcement_publications ADD COLUMN last_state_updated_at TEXT",
        }
        existing_pub_cols = {row[1] for row in cursor.execute("PRAGMA table_info(announcement_publications)").fetchall()}
        for col_name, ddl in pub_columns.items():
            if col_name not in existing_pub_cols:
                try:
                    cursor.execute(ddl)
                    write_log(f"Добавлена колонка announcement_publications.{col_name}")
                except sqlite3.OperationalError:
                    pass

        # --- announcement_invoice_blocks: создать, если нет ---
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS announcement_invoice_blocks (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                announcement_id INTEGER NOT NULL,
                block_number INTEGER NOT NULL,
                depublication_date TEXT NOT NULL,
                account_number TEXT NOT NULL,
                account_date TEXT NOT NULL,
                xml_file TEXT,
                created_at TEXT DEFAULT CURRENT_TIMESTAMP,
                UNIQUE(announcement_id, account_number),
                FOREIGN KEY (announcement_id) REFERENCES announcements(id) ON DELETE CASCADE
            )
        """)

        # --- View: announcement_latest_invoice_block ---
        cursor.execute("DROP VIEW IF EXISTS announcement_latest_invoice_block")
        cursor.execute("""
            CREATE VIEW IF NOT EXISTS announcement_latest_invoice_block AS
            SELECT b.*
            FROM announcement_invoice_blocks b
            INNER JOIN (
                SELECT announcement_id, MAX(block_number) AS max_block_number
                FROM announcement_invoice_blocks
                GROUP BY announcement_id
            ) lb
                ON lb.announcement_id = b.announcement_id
               AND lb.max_block_number = b.block_number
        """)

        # --- Лог-таблицы ---
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS announcement_status_log (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                announcement_id INTEGER NOT NULL,
                old_status TEXT,
                new_status TEXT NOT NULL,
                source TEXT NOT NULL,
                comment TEXT,
                created_at TEXT DEFAULT CURRENT_TIMESTAMP,
                FOREIGN KEY (announcement_id) REFERENCES announcements(id) ON DELETE CASCADE
            )
        """)
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS announcement_publication_state_log (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                announcement_id INTEGER NOT NULL,
                group_id INTEGER NOT NULL,
                old_state TEXT,
                new_state TEXT NOT NULL,
                vk_post_id TEXT,
                source TEXT NOT NULL,
                error TEXT,
                created_at TEXT DEFAULT CURRENT_TIMESTAMP
            )
        """)

        # --- Миграция существующих publication_state ---
        if 'publication_state' not in existing_pub_cols:
            # Заполняем publication_state на основе имеющихся данных
            cursor.execute("""
                UPDATE announcement_publications
                SET publication_state = CASE
                    WHEN real_depublication_date IS NOT NULL THEN 'deleted'
                    WHEN (vk_id IS NOT NULL OR vk_union_vacancy_id IS NOT NULL OR vk_union_acc_number_id IS NOT NULL)
                         AND real_depublication_date IS NULL THEN 'published'
                    ELSE 'not_published'
                END
                WHERE publication_state = 'not_published'
            """)
            write_log("Миграция publication_state выполнена")

        self.db_connection.commit()
        write_log("Миграция БД завершена")

    def _log_status_change(self, announcement_id: int, old_status: Optional[str],
                           new_status: str, source: str, comment: str = None):
        """Записывает изменение статуса объявления в лог-таблицу."""
        try:
            self.db_connection.execute("""
                INSERT INTO announcement_status_log
                    (announcement_id, old_status, new_status, source, comment, created_at)
                VALUES (?, ?, ?, ?, ?, DATETIME('now', 'localtime'))
            """, (announcement_id, old_status, new_status, source, comment))
        except sqlite3.Error as e:
            write_log(f"Ошибка записи в announcement_status_log: {e}")

    def _log_publication_state_change(self, announcement_id: int, group_id: int,
                                      old_state: Optional[str], new_state: str,
                                      vk_post_id: Optional[str], source: str,
                                      error: str = None):
        """Записывает изменение состояния публикации в лог-таблицу."""
        try:
            self.db_connection.execute("""
                INSERT INTO announcement_publication_state_log
                    (announcement_id, group_id, old_state, new_state, vk_post_id, source, error, created_at)
                VALUES (?, ?, ?, ?, ?, ?, ?, DATETIME('now', 'localtime'))
            """, (announcement_id, group_id, old_state, new_state, vk_post_id, source, error))
        except sqlite3.Error as e:
            write_log(f"Ошибка записи в announcement_publication_state_log: {e}")

    def _sync_announcement_status(self, announcement_id: int, source: str):
        """Пересчитывает глобальный статус одного объявления по последнему XML-блоку (§7.1 ТЗ)."""
        cursor = self.db_connection.cursor()
        cursor.execute("""
            SELECT depublication_date FROM announcement_latest_invoice_block
            WHERE announcement_id = ?
        """, (announcement_id,))
        row = cursor.fetchone()

        if row is None or row['depublication_date'] is None:
            write_log(f"[sync_status] Объявление {announcement_id}: нет XML-блока или depublication_date — статус не меняется")
            return

        dep_date = self._parse_date_value(row['depublication_date'])
        if dep_date is None:
            write_log(f"[sync_status] Объявление {announcement_id}: не удалось распарсить depublication_date='{row['depublication_date']}'")
            return

        today = datetime.now().date()
        new_status = 'Активное' if dep_date.date() > today else 'Завершенное'

        cursor.execute("SELECT status FROM announcements WHERE id = ?", (announcement_id,))
        ann_row = cursor.fetchone()
        if ann_row is None:
            return
        old_status = ann_row['status']

        if old_status != new_status:
            cursor.execute("""
                UPDATE announcements
                SET status = ?,
                    status_updated_at = DATETIME('now', 'localtime'),
                    status_update_source = ?
                WHERE id = ?
            """, (new_status, source, announcement_id))
            self._log_status_change(announcement_id, old_status, new_status, source)
            write_log(f"[sync_status] Объявление {announcement_id}: {old_status} -> {new_status} (source={source})")
        else:
            # Обновляем только timestamp
            cursor.execute("""
                UPDATE announcements
                SET status_updated_at = DATETIME('now', 'localtime'),
                    status_update_source = ?
                WHERE id = ?
            """, (source, announcement_id))

        self.db_connection.commit()

    def _sync_all_announcement_statuses(self, source: str):
        """Пересчитывает статусы всех объявлений (§7.2 ТЗ)."""
        write_log(f"[sync_all] Начало полной синхронизации статусов (source={source})...")
        cursor = self.db_connection.cursor()
        cursor.execute("SELECT id FROM announcements")
        ids = [row['id'] for row in cursor.fetchall()]
        changed = 0
        for ann_id in ids:
            old = cursor.execute("SELECT status FROM announcements WHERE id=?", (ann_id,)).fetchone()
            old_status = old['status'] if old else None
            self._sync_announcement_status(ann_id, source)
            new = cursor.execute("SELECT status FROM announcements WHERE id=?", (ann_id,)).fetchone()
            if new and old_status != new['status']:
                changed += 1
        write_log(f"[sync_all] Синхронизация завершена: {len(ids)} объявлений проверено, {changed} изменено")

    def _sync_expired_announcement_statuses(self, source: str):
        """Быстро завершает только просроченные объявления (§7.3 ТЗ)."""
        write_log(f"[sync_expired] Начало синхронизации просроченных (source={source})...")
        cursor = self.db_connection.cursor()
        today = datetime.now().strftime('%Y-%m-%d')

        cursor.execute("""
            SELECT a.id, a.status, lib.depublication_date
            FROM announcements a
            INNER JOIN announcement_latest_invoice_block lib ON lib.announcement_id = a.id
            WHERE a.status != 'Завершенное'
              AND DATE(lib.depublication_date) <= DATE(?)
        """, (today,))
        rows = cursor.fetchall()

        changed = 0
        for row in rows:
            ann_id = row['id']
            old_status = row['status']
            cursor.execute("""
                UPDATE announcements
                SET status = 'Завершенное',
                    status_updated_at = DATETIME('now', 'localtime'),
                    status_update_source = ?
                WHERE id = ?
            """, (source, ann_id))
            self._log_status_change(ann_id, old_status, 'Завершенное', source)
            changed += 1

        if changed:
            self.db_connection.commit()
        write_log(f"[sync_expired] Завершено просроченных: {changed}")

    def _get_latest_announcement_snapshot(self, announcement_id: int, group_id: int) -> Optional[Dict]:
        """Перечитывает актуальное состояние объявления из БД перед публикацией (§15.2 ТЗ)."""
        cursor = self.db_connection.cursor()
        cursor.execute(self._latest_invoice_block_cte() + """
            SELECT a.id, a.status, a.is_publication_blocked,
                   lib.depublication_date AS latest_depublication_date,
                   lib.account_number AS latest_account_number,
                   p.publication_state
            FROM announcements a
            LEFT JOIN latest_invoice_block lib ON lib.announcement_id = a.id
            LEFT JOIN announcement_publications p
                ON p.announcement_id = a.id AND p.group_id = ?
            WHERE a.id = ?
        """, (group_id, announcement_id))
        row = cursor.fetchone()
        return dict(row) if row else None

    def _can_publish(self, snapshot: Dict) -> bool:
        """Проверяет, можно ли публиковать объявление по новым правилам (§15 ТЗ)."""
        if snapshot is None:
            return False

        if snapshot.get('status') != 'Активное':
            return False

        dep_date = self._parse_date_value(snapshot.get('latest_depublication_date'))
        if dep_date is None or dep_date.date() <= datetime.now().date():
            return False

        if snapshot.get('is_publication_blocked', 0) == 1:
            return False

        pub_state = snapshot.get('publication_state', 'not_published')
        if pub_state in ('published', 'delete_pending', 'delete_failed'):
            return False

        return True

    def _depublish_announcement_everywhere(self, announcement_id: int, reason: str):
        """Удаляет все VK-посты объявления во всех группах и обновляет состояния (§20 ТЗ)."""
        write_log(f"[depublish_all] Депубликация объявления {announcement_id} во всех группах (reason={reason})")
        cursor = self.db_connection.cursor()
        current_datetime = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

        cursor.execute("""
            SELECT * FROM announcement_publications
            WHERE announcement_id = ? AND publication_state = 'published'
        """, (announcement_id,))
        pubs = [dict(row) for row in cursor.fetchall()]

        for pub in pubs:
            grp = pub['group_id']
            old_state = pub.get('publication_state', 'published')
            errors = []

            # Удаляем одиночный пост
            if pub.get('vk_id'):
                if not self._delete_vk_post(grp, pub['vk_id']):
                    errors.append(f"Не удалось удалить одиночный пост {pub['vk_id']}")

            # Удаляем объединённый по названию
            if pub.get('vk_union_vacancy_id'):
                if not self._delete_vk_post(grp, pub['vk_union_vacancy_id']):
                    errors.append(f"Не удалось удалить объединённый пост {pub['vk_union_vacancy_id']}")

            # Удаляем объединённый по счёту
            if pub.get('vk_union_acc_number_id'):
                if not self._delete_vk_post(grp, pub['vk_union_acc_number_id']):
                    errors.append(f"Не удалось удалить объединённый пост по счёту {pub['vk_union_acc_number_id']}")

            if errors:
                new_state = 'delete_failed'
                error_text = '; '.join(errors)
                cursor.execute("""
                    UPDATE announcement_publications
                    SET publication_state = 'delete_failed',
                        depublication_error = ?,
                        real_depublication_date = ?,
                        last_state_updated_at = ?
                    WHERE announcement_id = ? AND group_id = ?
                """, (error_text, current_datetime, current_datetime, announcement_id, grp))
            else:
                new_state = 'deleted'
                cursor.execute("""
                    UPDATE announcement_publications
                    SET publication_state = 'deleted',
                        real_depublication_date = ?,
                        last_state_updated_at = ?
                    WHERE announcement_id = ? AND group_id = ?
                """, (current_datetime, current_datetime, announcement_id, grp))

            self._log_publication_state_change(
                announcement_id, grp, old_state, new_state,
                str(pub.get('vk_id', '')), reason,
                '; '.join(errors) if errors else None
            )

            # Зеркальное обновление старого поля status для совместимости
            mirror_status = 'Завершённое' if new_state == 'deleted' else 'Ошибка удаления'
            cursor.execute("""
                UPDATE announcement_publications SET status = ?
                WHERE announcement_id = ? AND group_id = ?
            """, (mirror_status, announcement_id, grp))

            time.sleep(1)

        self.db_connection.commit()
    
    def _delete_all_posts(self, group_id: int):
        """Удаляет все посты из группы VK"""
        write_log(f"Начало удаления всех постов из группы {group_id}...")
        try:
            write_log(f"Удаление всех постов из группы {group_id}...")
            try:
                posts = self.vk.wall.get(owner_id=-group_id, count=100)['items']
                write_log(f"Получено {len(posts)} постов для удаления")
            except ApiError:
                write_log("Ошибка API при получении постов, переинициализация соединения...")
                self._init_vk_connection()
                posts = self.vk.wall.get(owner_id=-group_id, count=100)['items']
                write_log(f"Получено {len(posts)} постов для удаления после переинициализации")
            
            total_deleted = 0
            while posts:
                for post in posts:
                    try:
                        self.vk.wall.delete(
                            owner_id=-group_id,
                            post_id=post['id']
                        )
                        write_log(f"Удален пост {post['id']}")
                        total_deleted += 1
                        time.sleep(1)
                    except VkApiError as e:
                        write_log(f"Ошибка при удалении поста {post['id']}: {str(e)}")
                        write_log(f"Трассировка ошибки:\n{traceback.format_exc()}")
                    except ApiError:
                        write_log(f"Ошибка API при удалении поста {post['id']}, переинициализация...")
                        self._init_vk_connection()
                        self.vk.wall.delete(
                            owner_id=-group_id,
                            post_id=post['id']
                        )
                        write_log(f"Удален пост {post['id']} после переинициализации")
                        total_deleted += 1
                        time.sleep(1)
                
                try:
                    posts = self.vk.wall.get(owner_id=-group_id, count=100)['items']
                    write_log(f"Получено еще {len(posts)} постов для удаления")
                except ApiError:
                    write_log("Ошибка API при получении следующих постов, переинициализация...")
                    self._init_vk_connection()
                    posts = self.vk.wall.get(owner_id=-group_id, count=100)['items']
                    write_log(f"Получено {len(posts)} постов для удаления после переинициализации")
            
            write_log(f"Все посты удалены. Всего удалено: {total_deleted}")
        
        except VkApiError as e:
            error_msg = f"Ошибка при удалении постов: {str(e)}"
            write_log(error_msg)
            write_log(f"Трассировка ошибки:\n{traceback.format_exc()}")
            show_error_window(f"Ошибка удаления постов:\n{str(e)}")
            raise Exception(error_msg)
    
    def _is_posting_time(self) -> bool:
        """Проверяет, можно ли публиковать в текущее время"""
        now = datetime.now().time()
        start = datetime.strptime(self.start_time, '%H:%M').time()
        end = datetime.strptime(self.end_time, '%H:%M').time()
        
        is_time = self.round_the_clock or (start <= now <= end)
        write_log(f"Можно публиковать: {is_time}")
        return is_time
    
    def _latest_invoice_block_cte(self) -> str:
        """SQL CTE для получения последнего блока счета/депубликации объявления."""
        return """
            WITH latest_invoice_block AS (
                SELECT b.*
                FROM announcement_invoice_blocks b
                INNER JOIN (
                    SELECT announcement_id, MAX(block_number) AS max_block_number
                    FROM announcement_invoice_blocks
                    GROUP BY announcement_id
                ) lb
                    ON lb.announcement_id = b.announcement_id
                   AND lb.max_block_number = b.block_number
            )
        """

    def _parse_date_value(self, value) -> Optional[datetime]:
        """Парсит дату из БД/старых данных: YYYY-MM-DD, YYYY-MM-DD HH:MM:SS или DD.MM.YYYY."""
        if value is None:
            return None
        if isinstance(value, datetime):
            return value
        raw = str(value).strip()
        if not raw:
            return None

        for fmt in ('%Y-%m-%d %H:%M:%S', '%Y-%m-%d', '%d.%m.%Y', '%d.%m.%y'):
            try:
                return datetime.strptime(raw[:19] if fmt == '%Y-%m-%d %H:%M:%S' else raw[:10], fmt)
            except ValueError:
                continue
        return None

    def _get_effective_depublication_date(self, post: Dict) -> Optional[datetime]:
        """Возвращает актуальную дату депубликации: последний XML-блок, затем fallback на поле карточки."""
        return self._parse_date_value(
            post.get('latest_depublication_date') or post.get('depublication_date')
        )

    def _get_effective_account_number(self, vacancy: Dict) -> str:
        """Возвращает актуальный номер счета: последний XML-блок, затем fallback на поле карточки."""
        return str(vacancy.get('latest_account_number') or vacancy.get('account_number') or '').strip()

    def _should_publish(self, post: Dict) -> bool:
        """Проверяет, нужно ли публиковать пост.

        Новая модель статусов (§15, §24.3 ТЗ):
        - announcements.status = 'Активное'
        - publication_state != 'published' / 'delete_pending' / 'delete_failed'
        - latest_depublication_date > today
        - is_publication_blocked = 0
        """
        vacancy_id = post.get('id', 'unknown')

        # --- Глобальный статус объявления ---
        status = post.get('status', '')
        if status != 'Активное':
            write_log(f"Вакансия ID {vacancy_id} не подходит по статусу: '{status}' (ожидается 'Активное')")
            return False

        # --- Блокировка публикации ---
        if post.get('is_publication_blocked', 0) == 1:
            write_log(f"Вакансия ID {vacancy_id}: публикация заблокирована вручную")
            return False

        # --- Дата депубликации из последнего XML-блока ---
        depub_date = self._get_effective_depublication_date(post)
        if depub_date is None:
            write_log(f"Вакансия ID {vacancy_id}: нет даты депубликации — публикация запрещена")
            return False
        if depub_date.date() <= datetime.now().date():
            write_log(f"Вакансия ID {vacancy_id}: дата депубликации истекла ({depub_date.date()})")
            return False

        # --- Дата начала публикации (§15.1 ТЗ) ---
        pub_date = self._parse_date_value(post.get('publication_date'))
        if pub_date and pub_date.date() > datetime.now().date():
            write_log(f"Вакансия ID {vacancy_id}: дата начала публикации в будущем ({pub_date.date()})")
            return False

        # --- Состояние публикации в группе ---
        pub_state = post.get('publication_state', 'not_published')
        if pub_state in ('published', 'delete_pending', 'delete_failed'):
            # Для репостов: если published и репосты включены — проверяем интервал
            if pub_state == 'published' and self.repost_enabled and post.get('real_publication_date'):
                try:
                    last_pub = datetime.strptime(post['real_publication_date'], '%Y-%m-%d %H:%M:%S')
                    time_since = datetime.now() - last_pub
                    if time_since >= timedelta(days=self.repost_interval_days):
                        write_log(f"Вакансия ID {vacancy_id}: готова к репосту (прошло {time_since.days} дней)")
                        # Не блокируем — пойдёт как репост
                    else:
                        write_log(f"Вакансия ID {vacancy_id}: уже опубликована, до репоста {self.repost_interval_days - time_since.days} дней")
                        return False
                except Exception as e:
                    write_log(f"Ошибка проверки даты репоста для вакансии ID {vacancy_id}: {e}")
                    return False
            else:
                write_log(f"Вакансия ID {vacancy_id}: publication_state='{pub_state}', публикация не требуется")
                return False

        # --- XLSX-фильтр ---
        vacancy_name = post.get('vacancy', '')
        if not self._is_vacancy_allowed(vacancy_name):
            write_log(f"Вакансия ID {vacancy_id} не разрешена для публикации: '{vacancy_name}'")
            return False

        write_log(f"Вакансия ID {vacancy_id} подходит для публикации")
        return True
    def _get_vacancies_from_db(self) -> List[Dict]:
        """Получает вакансии из новой БД вместе с актуальным XML-блоком и графиком работы."""
        write_log("Загрузка вакансий из базы данных...")
        try:
            cursor = self.db_connection.cursor()
            query = self._latest_invoice_block_cte() + """
                SELECT a.*, p.*,
                       a.status AS status,
                       a.is_publication_blocked AS is_publication_blocked,
                       p.publication_state AS publication_state,
                       lib.depublication_date AS latest_depublication_date,
                       lib.account_number AS latest_account_number,
                       lib.account_date AS latest_account_date,
                       lib.block_number AS latest_invoice_block_number,
                       lib.depublication_date AS effective_depublication_date,
                       lib.account_number AS effective_account_number,
                       lib.account_date AS effective_account_date,
                       (SELECT GROUP_CONCAT(s.work_days || ' ' || COALESCE(s.work_hours, ''), '; ')
                        FROM announcement_schedules s
                        WHERE s.announcement_id = a.id) AS work_schedule
                FROM announcements a
                LEFT JOIN latest_invoice_block lib ON lib.announcement_id = a.id
                LEFT JOIN (
                    SELECT * FROM announcement_publications
                    WHERE group_id = ?
                ) p ON a.id = p.announcement_id
            """
            cursor.execute(query, (self.config['group_id'],))
            vacancies = [dict(row) for row in cursor.fetchall()]
            write_log(f"Загружено {len(vacancies)} вакансий из БД")
            return vacancies

        except sqlite3.Error as e:
            error_msg = f"Ошибка чтения данных из базы данных: {str(e)}"
            write_log(error_msg)
            write_log(f"Трассировка ошибки:\n{traceback.format_exc()}")
            show_error_window(f"Ошибка чтения данных из базы данных:\n{str(e)}")
            raise Exception(error_msg)
    def _update_vacancy_in_db(self, vacancy_id: int, updates: Dict):
        """Обновляет или создаёт запись о публикации вакансии в базе данных.
        
        §16, §24.4 ТЗ: после публикации писать publication_state='published',
        а не status='Активное' как раньше. Старое поле status обновляется
        зеркально для совместимости.
        """
        write_log(f"Обновление/создание записи для вакансии ID {vacancy_id}")
        group_id = self.config['group_id']
        try:
            cursor = self.db_connection.cursor()
            
            # Проверяем, существует ли запись
            cursor.execute(
                "SELECT publication_state FROM announcement_publications WHERE announcement_id = ? AND group_id = ?",
                (vacancy_id, group_id)
            )
            existing = cursor.fetchone()
            old_state = existing['publication_state'] if existing else None
            
            if existing is None:
                # Создаём запись со значениями по умолчанию
                write_log(f"Запись для вакансии {vacancy_id} и группы {group_id} не найдена, создаём новую")
                cursor.execute("""
                    INSERT INTO announcement_publications 
                    (announcement_id, group_id, status, publication_state, last_state_updated_at)
                    VALUES (?, ?, 'Еще не опубликованное', 'not_published', DATETIME('now', 'localtime'))
                """, (vacancy_id, group_id))
                self.db_connection.commit()
                write_log(f"Создана новая запись для вакансии {vacancy_id}")
                old_state = 'not_published'
            
            # Формируем и выполняем UPDATE
            if updates:
                # Зеркальное обновление: publication_state <-> status (§26.3 ТЗ)
                actual_updates = dict(updates)
                
                # Если обновляем publication_state — зеркально обновляем старый status
                if 'publication_state' in actual_updates:
                    new_pub_state = actual_updates['publication_state']
                    mirror_map = {
                        'not_published': 'Еще не опубликованное',
                        'published': 'Активное',
                        'deleted': 'Завершённое',
                        'delete_failed': 'Ошибка удаления',
                        'delete_pending': 'Завершённое',
                    }
                    actual_updates.setdefault('status', mirror_map.get(new_pub_state, 'Еще не опубликованное'))
                    actual_updates['last_state_updated_at'] = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
                
                # Если обновляем старый status без publication_state — зеркально обновляем publication_state
                if 'status' in actual_updates and 'publication_state' not in actual_updates:
                    old_status_val = actual_updates['status']
                    reverse_map = {
                        'Активное': 'published',
                        'Завершённое': 'deleted',
                        'Завершенное': 'deleted',
                        'Еще не опубликованное': 'not_published',
                        'Ещё не опубликованное': 'not_published',
                        'Не опубликованное': 'not_published',
                        'Ошибка удаления': 'delete_failed',
                    }
                    if old_status_val in reverse_map:
                        actual_updates['publication_state'] = reverse_map[old_status_val]
                        actual_updates['last_state_updated_at'] = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

                set_parts = [f"{key} = ?" for key in actual_updates.keys()]
                values = list(actual_updates.values())
                values.extend([vacancy_id, group_id])
                query = f"""
                    UPDATE announcement_publications 
                    SET {', '.join(set_parts)} 
                    WHERE announcement_id = ? AND group_id = ?
                """
                cursor.execute(query, values)
                self.db_connection.commit()
                
                # Логируем изменение publication_state
                new_state = actual_updates.get('publication_state')
                if new_state and new_state != old_state:
                    self._log_publication_state_change(
                        vacancy_id, group_id, old_state, new_state,
                        str(actual_updates.get('vk_id', '')), 'publisher_publish'
                    )
                
                write_log(f"Данные вакансии {vacancy_id} успешно обновлены")
            else:
                write_log(f"Нет полей для обновления вакансии {vacancy_id}")
                
        except sqlite3.Error as e:
            error_msg = f"Ошибка обновления/создания записи для вакансии ID {vacancy_id}: {str(e)}"
            write_log(error_msg)
            write_log(f"Трассировка ошибки:\n{traceback.format_exc()}")
            show_error_window(f"Ошибка обновления базы данных:\n{str(e)}")
            raise Exception(error_msg)
    
    def _get_vacancy_icons(self, vacancy: Dict) -> str:
        """Возвращает иконки для вакансии в зависимости от тега atribute"""
        rubr_atryb = vacancy.get('atribute') or ''
        if rubr_atryb is None:
            rubr_atryb = ''
        rubr_atryb = str(rubr_atryb).lower()
        vacancy_id = vacancy.get('id', 'unknown')
        
        icons = {
            'lenta': ['📌', '📜'],
            'srochno': ['⚠️', '🔔'],
            'skrepka': ['📎', '🖇️'],
        }
        
        for tag, icon_pair in icons.items():
            if tag in rubr_atryb:
                # Безопасное получение значения atribute для подсчёта
                count = 0
                for v in self._get_vacancies_from_db():
                    attr_val = v.get('atribute') or ''
                    if tag in str(attr_val).lower():
                        count += 1
                icon = icon_pair[count % 2]
                return icon
        
        return ''
    
    def _get_salary_icon(self, salary: str) -> str:
        """Возвращает иконку для зарплаты в зависимости от формата"""
        if not salary or salary == 'По договорённости':
            return '💰'
        
        if 'руб./месяц' in salary:
            return '💵'
        elif 'руб./смена' in salary:
            return '⏱️'
        else:
            return '💲'
    
    def _format_high_salary(self, vacancy_name: str, salary: str) -> str:
        """Добавляет восклицательные знаки для вакансий с высокой зарплатой"""
        if not isinstance(salary, (int, float)):
            try:
                salary_num = float(''.join(filter(str.isdigit, str(salary))))
            except:
                return vacancy_name
        
        if salary_num >= self.salary_threshold * 2:
            return f"‼️‼️ {vacancy_name} ‼️"
        
        return vacancy_name
    
    def _apply_salary_threshold(self, salary) -> str:
        """
        Приводит зарплату к тексту для поста:
        - если зарплата ниже порога (по числам) -> 'По договорённости'
        - иначе возвращает исходное значение (как строку)
        """
        if salary is None:
            return ""

        # Число
        if isinstance(salary, (int, float)):
            return self.salary_text if float(salary) < self.salary_threshold else str(salary)

        s = str(salary).strip()
        if not s:
            return ""

        # Уже "по договор..."
        if s.lower().startswith("по договор"):
            return self.salary_text

        # Вытаскиваем все числа из строки (учитываем пробелы в "30 000")
        nums = []
        for token in re.findall(r"\d[\d\s]*", s):
            n = token.replace(" ", "")
            if n.isdigit():
                nums.append(int(n))

        # Если чисел нет — оставляем как есть
        if not nums:
            return s

        # Логика порога: если даже максимальное число в строке ниже порога — считаем "ниже порога"
        # (так диапазон "30 000–60 000" не заменится при пороге 50 000, потому что max=60000)
        if self.hide_salary and max(nums) < self.salary_threshold:
            return self.salary_text

        return s
    
    def _generate_single_post(self, vacancy: Dict) -> str:
        """Генерирует текст одиночного поста с учетом нового форматирования"""
        vacancy_id = vacancy.get('id', 'unknown')
        write_log(f"Генерация одиночного поста для вакансии ID {vacancy_id}")
        
        vacancy_name = vacancy.get('vacancy', '')
        salary = self._apply_salary_threshold(vacancy.get('salary'))
        
        rubr_icon = self._get_vacancy_icons(vacancy)
        salary_icon = self._get_salary_icon(salary)
        vacancy_name = self._format_high_salary(vacancy_name, salary)
        
        post_parts = []
        if rubr_icon:
            post_parts.append(f"{rubr_icon} {vacancy_name.upper()}")
        else:
            post_parts.append(f"🏢 {vacancy_name.upper()}")
        
        post_parts.append(f"{salary_icon} Зарплата: {salary}")
        
        if not self.hide_company_names and vacancy.get('company'):
            post_parts.append(f"🏭 Компания: {vacancy['company']}")
        
        if not self.hide_address:
            address_parts = []
            if vacancy.get('common_post_address'):
                address_parts.append(vacancy['common_post_address'])
            
            if address_parts:
                post_parts.append(f"📍 Адрес: {', '.join(address_parts)}")
        
        if not self.hide_additional_info:
            for field, title in [('responcibilities', '📌 Обязанности'), 
                               ('requirements', '📝 Требования'), 
                               ('conditions', '📋 Условия')]:
                if vacancy.get(field):
                    post_parts.append(f"\n{convert_html(vacancy[field])}")
        
        if self.hide_additional_info or not(vacancy['conditions'] and ("график работы:" in vacancy['conditions'].lower())
            or vacancy['requirements'] and ("график работы:" in vacancy['requirements'].lower())
            or vacancy['responcibilities'] and ("график работы:" in vacancy['responcibilities'].lower())):
            if vacancy.get('work_schedule', '') and not(self.hide_additional_info):
                post_parts.append(f"🕒  {vacancy['work_schedule']}")
        
        contact_parts = []
        if vacancy.get('phone'):
            contact_parts.append(f"📞 {vacancy['phone']}")
        if vacancy.get('contact_man'):
            contact_parts.append(f"👤 {vacancy['contact_man']}")
        
        if contact_parts:
            post_parts.append("\n" + " | ".join(contact_parts))
        
        result = "\n".join(post_parts)
        write_log(f"Сгенерирован одиночный пост для ID {vacancy_id}")
        return result
    
    def _generate_merged_post(self, vacancies: List[Dict]) -> str:
        """Генерирует текст объединенного поста с новым форматированием"""
        write_log(f"Генерация объединенного поста для {len(vacancies)} вакансий")
        if not vacancies:
            write_log("Нет вакансий для объединения")
            return ""
        
        main_vacancy_name = vacancies[0].get('vacancy', '').split()[0]
        rubr_icon = self._get_vacancy_icons(vacancies[0])
        
        post_parts = []
        if rubr_icon:
            post_parts.append(f"{rubr_icon} Вакансии: {main_vacancy_name.upper()}")
        else:
            post_parts.append(f"🔹 Вакансии: {main_vacancy_name.upper()}")
        
        post_parts.append("")
        
        for i, vacancy in enumerate(vacancies, 1):
            vacancy_part = []
            salary = self._apply_salary_threshold(vacancy.get('salary').upper())
            
            vacancy_part.append(f"{i}. {vacancy.get('vacancy', '')}")
            vacancy_part.append(f"{self._get_salary_icon(salary)} Зарплата: {salary}")
            if ('work_schedule' in vacancy):
                if vacancy.get('work_schedule', ''):
                    vacancy_part.append(f"🕒  {vacancy.get('work_schedule')}")
            
            if not self.hide_company_names and vacancy.get('company'):
                vacancy_part.append(f"🏭 Компания: {vacancy['company']}")
            
            contact_parts = []
            if vacancy.get('phone'):
                contact_parts.append(f"📞 {vacancy.get('phone')}")
            if vacancy.get('contact_man'):
                contact_parts.append(f"👤 {vacancy.get('contact_man')}")
            if contact_parts:
                vacancy_part.append(" | ".join(contact_parts))
            post_parts.append("\n".join(vacancy_part))
            post_parts.append("")
        
        result = "\n".join(post_parts).strip()
        write_log(f"Сгенерирован объединенный пост для {len(vacancies)} вакансий")
        return result
    
    def _post_to_vk(self, group_id: int, text: str) -> Tuple[Optional[str], Optional[int]]:
        """Публикует пост в группу VK"""
        write_log(f"Публикация поста в группу {group_id}")
        try:
            result = self.vk.wall.post(
                owner_id=-group_id,
                from_group=1,
                message=text
            )
            
            post_id = result.get('post_id')
            write_log(f"Опубликован пост {post_id}")
            return (f"https://vk.com/wall-{group_id}_{post_id}", post_id) if post_id else (None, None)
        
        except ApiError:
            write_log("Ошибка API при публикации, переинициализация соединения...")
            self._init_vk_connection()
            result = self.vk.wall.post(
                owner_id=-group_id,
                from_group=1,
                message=text
            )
            
            post_id = result.get('post_id')
            write_log(f"Опубликован пост {post_id} после переинициализации")
            return (f"https://vk.com/wall-{group_id}_{post_id}", post_id) if post_id else (None, None)
        
        except VkApiError as e:
            error_msg = f"Ошибка публикации: {str(e)}"
            write_log(error_msg)
            write_log(f"Трассировка ошибки:\n{traceback.format_exc()}")
            show_error_window(f"Ошибка публикации в VK:\n{str(e)}")
            raise Exception(error_msg)
    
    def _delete_vk_post(self, group_id: int, post_id: int) -> bool:
        """Удаляет пост из группы VK"""
        write_log(f"Попытка удаления поста {post_id} из группы {group_id}")
        try:
            try:
                self.vk.wall.delete(owner_id=-group_id, post_id=post_id)
                write_log(f"Пост {post_id} успешно удален")
                return True
            except ApiError:
                write_log(f"Ошибка API при удалении поста {post_id}, переинициализация...")
                self._init_vk_connection()
                self.vk.wall.delete(owner_id=-group_id, post_id=post_id)
                write_log(f"Пост {post_id} успешно удален после переинициализации")
                return True
        except VkApiError as e:
            error_msg = f"Ошибка при удалении поста {post_id}: {str(e)}"
            write_log(error_msg)
            write_log(f"Трассировка ошибки:\n{traceback.format_exc()}")
            show_error_window(f"Ошибка удаления поста из VK:\n{str(e)}")
            return False
    
    def _process_single_vacancies(self, vacancies: List[Dict], group_id: int):
        """Обрабатывает одиночные вакансии"""
        write_log(f"Обработка {len(vacancies)} одиночных вакансий")
        for vacancy in vacancies:
            vacancy_id = vacancy.get('id', 'unknown')
            write_log(f"Обработка одиночной вакансии ID {vacancy_id}")
            
            if not self._should_publish(vacancy):
                write_log(f"Вакансия ID {vacancy_id} не подходит для публикации, пропускаем")
                continue
            
            # §15.2 ТЗ: перечитать состояние перед VK API вызовом
            snapshot = self._get_latest_announcement_snapshot(vacancy_id, group_id)
            if not self._can_publish(snapshot):
                write_log(f"Вакансия ID {vacancy_id}: повторная проверка перед публикацией не пройдена, пропускаем")
                continue
            
            # Если это репост — удаляем старый пост
            if vacancy.get('publication_state') == 'published' and vacancy.get('vk_id'):
                write_log(f"Репост вакансии ID {vacancy_id}: удаляем старый пост {vacancy['vk_id']}")
                self._delete_vk_post(group_id, vacancy['vk_id'])
            
            post_text = self._generate_single_post(vacancy)
            post_url, post_id = self._post_to_vk(group_id, post_text)
            
            if post_url and post_id:
                # §16, §24.4 ТЗ: publication_state='published', не менять announcements.status
                updates = {
                    'publication_state': 'published',
                    'vk_single_link': post_url,
                    'vk_id': post_id,
                    'real_publication_date': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
                }
                self._update_vacancy_in_db(vacancy['id'], updates)
                write_log(f"Опубликована одиночная вакансия ID {vacancy['id']}, пост {post_id}")
            else:
                write_log(f"Ошибка публикации вакансии ID {vacancy['id']}")
            write_log(f"Ожидание {self.post_interval} минут перед следующим постом")
            time.sleep(self.post_interval * 60)
    
    def _process_reposts(self, group_id: int):
        """Обрабатывает повторные публикации (репосты) с учетом интервала (§17 ТЗ)."""
        write_log("Начало обработки репостов...")
        try:
            cutoff_date = (datetime.now() - timedelta(days=self.repost_interval_days)).strftime('%Y-%m-%d %H:%M:%S')
            current_date = datetime.now().strftime('%Y-%m-%d')
            current_datetime = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

            write_log(f"Поиск кандидатов на репост с датой публикации до {cutoff_date}")

            cursor = self.db_connection.cursor()
            cursor.execute(self._latest_invoice_block_cte() + """
                SELECT a.*, p.*,
                       a.status AS status,
                       a.is_publication_blocked AS is_publication_blocked,
                       p.publication_state AS publication_state,
                       lib.depublication_date AS latest_depublication_date,
                       lib.account_number AS latest_account_number,
                       lib.account_date AS latest_account_date,
                       lib.block_number AS latest_invoice_block_number,
                       lib.depublication_date AS effective_depublication_date,
                       lib.account_number AS effective_account_number,
                       lib.account_date AS effective_account_date
                FROM announcements a
                LEFT JOIN latest_invoice_block lib ON lib.announcement_id = a.id
                LEFT JOIN (SELECT * FROM announcement_publications WHERE group_id=?) p
                    ON a.id = p.announcement_id
                WHERE
                    a.status = 'Активное'
                    AND a.is_publication_blocked = 0
                    AND p.publication_state = 'published'
                    AND p.real_publication_date <= ?
                    AND lib.depublication_date > ?
            """, (self.config['group_id'], cutoff_date, current_date))

            repost_candidates = [dict(row) for row in cursor.fetchall()]

            if repost_candidates:
                write_log(f"Найдено {len(repost_candidates)} кандидатов на репост")
            else:
                write_log("Кандидатов на репост не найдено")

            reposted_count = 0
            for vacancy in repost_candidates:
                vacancy_id = vacancy.get('id', 'unknown')
                write_log(f"Проверка вакансии ID {vacancy_id} для репоста")

                # Проверяем, разрешена ли вакансия для публикации
                vacancy_name = vacancy.get('vacancy', '')
                if not self._is_vacancy_allowed(vacancy_name):
                    write_log(f"Вакансия ID {vacancy_id} не разрешена для публикации: '{vacancy_name}' - пропускаем")
                    continue

                if vacancy.get('real_publication_date'):
                    try:
                        last_publication = datetime.strptime(vacancy['real_publication_date'], '%Y-%m-%d %H:%M:%S')
                        time_since_last = datetime.now() - last_publication
                        required_interval = timedelta(days=self.repost_interval_days)

                        if time_since_last < required_interval:
                            write_log(f"Вакансия ID {vacancy_id} опубликована {time_since_last.days} дней назад, требуется {self.repost_interval_days} дней - пропускаем")
                            continue
                        else:
                            write_log(f"Вакансия ID {vacancy_id} готова к репосту: прошло {time_since_last.days} дней")
                    except Exception as e:
                        write_log(f"Ошибка проверки даты репоста для вакансии {vacancy_id}: {str(e)}")
                        write_log(f"Трассировка ошибки:\n{traceback.format_exc()}")
                        continue

                # §17 ТЗ: удаляем старый пост, создаём новый, publication_state остаётся 'published'
                if vacancy.get('vk_id'):
                    write_log(f"Удаление старого поста {vacancy['vk_id']} для вакансии {vacancy_id}")
                    self._delete_vk_post(group_id, vacancy['vk_id'])
                    write_log(f"Удален старый пост {vacancy['vk_id']} для репоста вакансии {vacancy_id}")
                else:
                    write_log(f"У вакансии ID {vacancy_id} нет vk_id для удаления")

                post_text = self._generate_single_post(vacancy)
                post_url, post_id = self._post_to_vk(group_id, post_text)

                if post_url and post_id:
                    updates = {
                        'publication_state': 'published',
                        'vk_single_link': post_url,
                        'vk_id': post_id,
                        'real_publication_date': current_datetime
                    }
                    self._update_vacancy_in_db(vacancy['id'], updates)
                    write_log(f"Репост вакансии {vacancy_id} выполнен успешно, новый пост {post_id}")
                    reposted_count += 1
                else:
                    write_log(f"Ошибка репоста вакансии {vacancy_id}")
                write_log(f"Ожидание {self.post_interval} минут перед следующим репостом")
                time.sleep(self.post_interval * 60)

            write_log(f"Обработка репостов завершена. Успешно репостнуто: {reposted_count} вакансий")

        except sqlite3.Error as e:
            error_msg = f"Ошибка обработки репостов: {str(e)}"
            write_log(error_msg)
            write_log(f"Трассировка ошибки:\n{traceback.format_exc()}")
            show_error_window(f"Ошибка обработки репостов:\n{str(e)}")
            raise Exception(error_msg)
    def _cleanup_expired_vacancies(self):
        """Очищает объявления с истекшей актуальной датой депубликации (§18 ТЗ).
        
        1. Ставит announcements.status = 'Завершенное'
        2. Удаляет VK-посты (одиночные, объединённые)
        3. publication_state = 'deleted' / 'delete_failed'
        4. Заполняет real_depublication_date
        5. Ссылки на посты НЕ удаляет
        """
        write_log("Начало очистки просроченных вакансий...")
        try:
            current_date = datetime.now().strftime('%Y-%m-%d')
            current_datetime = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

            cursor = self.db_connection.cursor()

            cursor.execute(self._latest_invoice_block_cte() + """
                SELECT a.id,
                       a.status AS ann_status,
                       p.vk_id,
                       p.vk_union_vacancy_id,
                       p.vk_union_acc_number_id,
                       p.publication_state,
                       p.group_id AS pub_group_id,
                       lib.depublication_date AS latest_depublication_date,
                       lib.depublication_date AS effective_depublication_date
                FROM announcements a
                INNER JOIN latest_invoice_block lib ON lib.announcement_id = a.id
                LEFT JOIN (SELECT * FROM announcement_publications WHERE group_id=?) p
                    ON a.id = p.announcement_id
                WHERE
                    DATE(lib.depublication_date) <= DATE(?)
                    AND (
                        p.publication_state = 'published'
                        OR (a.status != 'Завершенное')
                    )
            """, (self.config["group_id"], current_date,))

            expired_vacancies = [dict(row) for row in cursor.fetchall()]

            if expired_vacancies:
                write_log(f"Найдено {len(expired_vacancies)} просроченных объявлений для очистки")
            else:
                write_log("Просроченных объявлений не найдено")
                return 0

            cleaned_count = 0
            for vacancy in expired_vacancies:
                vacancy_id = vacancy.get('id', 'unknown')
                old_ann_status = vacancy.get('ann_status', '')
                old_pub_state = vacancy.get('publication_state', 'not_published')
                write_log(f"Обработка просроченной вакансии ID {vacancy_id}")

                errors = []

                # Удаляем VK-посты только если есть опубликованные
                if old_pub_state == 'published':
                    if vacancy.get('vk_id'):
                        write_log(f"Удаление одиночного поста VK {vacancy['vk_id']} для вакансии {vacancy_id}")
                        if not self._delete_vk_post(self.config.get('group_id'), vacancy['vk_id']):
                            errors.append(f"Не удалось удалить пост {vacancy['vk_id']}")

                    if vacancy.get('vk_union_vacancy_id'):
                        write_log(f"Удаление объединенного поста VK {vacancy['vk_union_vacancy_id']} для вакансии {vacancy_id}")
                        if not self._delete_vk_post(self.config.get('group_id'), vacancy['vk_union_vacancy_id']):
                            errors.append(f"Не удалось удалить объединённый пост {vacancy['vk_union_vacancy_id']}")

                    if vacancy.get('vk_union_acc_number_id'):
                        write_log(f"Удаление объединенного поста по счету VK {vacancy['vk_union_acc_number_id']} для вакансии {vacancy_id}")
                        if not self._delete_vk_post(self.config.get('group_id'), vacancy['vk_union_acc_number_id']):
                            errors.append(f"Не удалось удалить объединённый пост по счёту {vacancy['vk_union_acc_number_id']}")

                    # Обновляем publication_state
                    grp = vacancy.get('pub_group_id', self.config.get('group_id'))
                    if errors:
                        new_pub_state = 'delete_failed'
                        error_text = '; '.join(errors)
                        cursor.execute("""
                            UPDATE announcement_publications
                            SET publication_state = 'delete_failed',
                                depublication_error = ?,
                                real_depublication_date = ?,
                                last_state_updated_at = ?,
                                status = 'Завершённое'
                            WHERE announcement_id = ? AND group_id = ?
                        """, (error_text, current_datetime, current_datetime, vacancy_id, grp))
                    else:
                        new_pub_state = 'deleted'
                        cursor.execute("""
                            UPDATE announcement_publications
                            SET publication_state = 'deleted',
                                real_depublication_date = ?,
                                last_state_updated_at = ?,
                                status = 'Завершённое'
                            WHERE announcement_id = ? AND group_id = ?
                        """, (current_datetime, current_datetime, vacancy_id, grp))

                    self._log_publication_state_change(
                        vacancy_id, grp, old_pub_state, new_pub_state,
                        str(vacancy.get('vk_id', '')), 'depublication_expired',
                        '; '.join(errors) if errors else None
                    )

                # §18.2 п.1: Ставим announcements.status = 'Завершенное' (всегда, даже при ошибке удаления)
                if old_ann_status != 'Завершенное':
                    cursor.execute("""
                        UPDATE announcements
                        SET status = 'Завершенное',
                            status_updated_at = DATETIME('now', 'localtime'),
                            status_update_source = 'depublication_expired'
                        WHERE id = ?
                    """, (vacancy_id,))
                    self._log_status_change(vacancy_id, old_ann_status, 'Завершенное', 'depublication_expired')

                self.db_connection.commit()
                write_log(f"Обработана просроченная вакансия {vacancy_id}")
                cleaned_count += 1
                time.sleep(1)

            write_log(f"Очистка просроченных вакансий завершена. Обработано: {cleaned_count} вакансий")
            return cleaned_count

        except sqlite3.Error as e:
            error_msg = f"Ошибка при очистке просроченных вакансий: {str(e)}"
            write_log(error_msg)
            write_log(f"Трассировка ошибки:\n{traceback.format_exc()}")
            show_error_window(f"Ошибка очистки просроченных вакансий:\n{str(e)}")
            return 0
    def _find_similar_vacancies_by_name(self, vacancies: List[Dict]) -> Dict[str, List[Dict]]:
        """Находит схожие вакансии для объединения по первому слову названия"""
        write_log("Поиск схожих вакансий для объединения по первому слову названия...")
        similar_vacancies = {}
        
        if not vacancies:
            write_log("Нет вакансий для поиска схожих")
            return similar_vacancies
        
        # Фильтруем только те вакансии, которые подходят для публикации
        publishable_vacancies = [v for v in vacancies if self._should_publish(v)]
        write_log(f"Для поиска схожих доступно {len(publishable_vacancies)} вакансий")
        
        if len(publishable_vacancies) < 3:
            write_log(f"Слишком мало вакансий для объединения: {len(publishable_vacancies)} < 3")
            return similar_vacancies
        
        # Создаем словарь для группировки по похожим первым словам
        groups = {}
        
        for vacancy in publishable_vacancies:
            vacancy_name = vacancy.get('vacancy', '')
            first_word = self._get_first_word(vacancy_name).lower()
            
            if not first_word:
                continue
            
            # Ищем группу с похожим первым словом
            found_group = False
            for group_key in groups.keys():
                if similarity_ratio(first_word, group_key) >= self.similarity_threshold:
                    groups[group_key].append(vacancy)
                    found_group = True
                    write_log(f"Добавлена вакансия '{vacancy_name}' в группу '{group_key}' (сходство с '{first_word}')")
                    break
            
            # Если не нашли похожую группу, создаем новую
            if not found_group:
                groups[first_word] = [vacancy]
                write_log(f"Создана новая группа по первому слову '{first_word}' для вакансии '{vacancy_name}'")
        
        # Фильтруем группы: оставляем только те, где минимум 3 вакансии с разными ИНН
        for group_key, group_vacancies in groups.items():
            if len(group_vacancies) >= 3:
                # Проверяем уникальность ИНН
                unique_inns = set()
                for vacancy in group_vacancies:
                    inn = vacancy.get('inn', '')
                    if inn:
                        unique_inns.add(inn)
                
                if len(unique_inns) >= 3:
                    similar_vacancies[group_key] = group_vacancies
                    write_log(f"Группа '{group_key}' подходит для объединения: {len(group_vacancies)} вакансий, {len(unique_inns)} уникальных ИНН")
                else:
                    write_log(f"Группа '{group_key}' не подходит: недостаточно уникальных ИНН ({len(unique_inns)} из 3)")
            else:
                write_log(f"Группа '{group_key}' не подходит: недостаточно вакансий ({len(group_vacancies)} из 3)")
        
        write_log(f"Всего найдено {len(similar_vacancies)} групп для объединения по названию")
        return similar_vacancies
    
    def _should_create_new_union(self, first_word: str, vacancies: List[Dict]) -> bool:
        """Проверяет, нужно ли создавать новое объединенное объявление"""
        write_log(f"Проверка необходимости нового объединения для группы '{first_word}'")
        if not vacancies:
            write_log("Нет вакансий для объединения")
            return True
        
        sample_vacancy = vacancies[0]
        last_union_date = sample_vacancy.get('real_publication_union_vacancy_date')
        
        if not last_union_date:
            write_log(f"Для группы '{first_word}' нет даты последнего объединения - создаем новое")
            return True
        
        try:
            if isinstance(last_union_date, str):
                last_union_date = datetime.strptime(last_union_date, '%Y-%m-%d %H:%M:%S')
            
            time_since_last_union = datetime.now() - last_union_date
            required_interval = timedelta(days=self.merge_interval_days)
            
            if time_since_last_union < required_interval:
                write_log(f"Для объединения группы '{first_word}' не прошло достаточно времени: {time_since_last_union.days} дней из {self.merge_interval_days}")
                return False
            
            write_log(f"Для объединения группы '{first_word}' прошло достаточно времени: {time_since_last_union.days} дней")
            return True
        except Exception as e:
            write_log(f"Ошибка проверки даты объединения: {str(e)}")
            write_log(f"Трассировка ошибки:\n{traceback.format_exc()}")
            return True
    
    def _delete_old_union_posts(self, vacancies: List[Dict], group_id: int):
        """Удаляет старые объединенные посты для этих вакансий"""
        write_log(f"Удаление старых объединенных постов для {len(vacancies)} вакансий")
        for vacancy in vacancies:
            vacancy_id = vacancy.get('id', 'unknown')
            union_post_id = vacancy.get('vk_union_vacancy_id')
            if union_post_id:
                write_log(f"Удаление старого объединенного поста {union_post_id} для вакансии {vacancy_id}")
                if self._delete_vk_post(group_id, union_post_id):
                    write_log(f"Удален старый объединенный пост {union_post_id} для вакансии {vacancy_id}")
                else:
                    write_log(f"Не удалось удалить старый объединенный пост {union_post_id} для вакансии {vacancy_id}")
                
                updates = {
                    'vk_union_vacancy_link': None,
                    'vk_union_vacancy_id': None
                }
                self._update_vacancy_in_db(vacancy['id'], updates)
                write_log(f"Обновлена запись вакансии {vacancy_id} - очищены данные объединенного поста")
            else:
                write_log(f"У вакансии {vacancy_id} нет старого объединенного поста для удаления")
    
    def _process_merged_vacancies_by_name(self, merged_vacancies: Dict[str, List[Dict]], group_id: int):
        """Обрабатывает объединенные вакансии по названию"""
        write_log(f"Обработка {len(merged_vacancies)} групп объединенных вакансий по названию")
        for first_word, vacancies in merged_vacancies.items():
            write_log(f"Обработка объединения для группы '{first_word}' ({len(vacancies)} вакансий)")
            
            # Проверяем условия для объединения
            if len(vacancies) < 3:
                write_log(f"Пропускаем группу '{first_word}' - недостаточно вакансий: {len(vacancies)} < 3")
                continue
            
            # Проверяем уникальность ИНН
            unique_inns = set()
            for vacancy in vacancies:
                inn = vacancy.get('inn', '')
                if inn:
                    unique_inns.add(inn)
            
            if len(unique_inns) < 3:
                write_log(f"Пропускаем группу '{first_word}' - недостаточно уникальных ИНН: {len(unique_inns)} < 3")
                continue
            
            if not self._should_create_new_union(first_word, vacancies):
                write_log(f"Пропускаем объединение для группы '{first_word}' - не прошло достаточно времени")
                continue
            
            self._delete_old_union_posts(vacancies, group_id)
            
            post_text = self._generate_merged_post(vacancies)
            post_url, post_id = self._post_to_vk(group_id, post_text)
            
            
            if post_url and post_id:
                current_time = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
                for vacancy in vacancies:
                    updates = {
                        'publication_state': 'published',
                        'vk_union_vacancy_link': post_url,
                        'vk_union_vacancy_id': post_id,
                        'real_publication_union_vacancy_date': current_time
                    }
                    self._update_vacancy_in_db(vacancy['id'], updates)
                
                write_log(f"Создано новое объединенное объявление для группы '{first_word}' с {len(vacancies)} вакансиями ({len(unique_inns)} уникальных ИНН), пост {post_id}")
            else:
                write_log(f"Ошибка создания объединенного объявления для группы '{first_word}'")
            write_log(f"Ожидание {self.post_interval} минут перед следующим объединенным постом")
            time.sleep(self.post_interval * 60)
    
    def _filter_single_vacancies(self, all_vacancies: List[Dict], merged_vacancies: Dict[str, List[Dict]]) -> List[Dict]:
        """Фильтрует одиночные вакансии (исключая те, что вошли в объединения)"""
        write_log("Фильтрация одиночных вакансий...")
        
        # Собираем ID вакансий, которые уже вошли в объединения
        merged_ids = set()
        for group in merged_vacancies.values():
            for vacancy in group:
                merged_ids.add(vacancy['id'])
        
        # Отбираем только те вакансии, которые подходят для публикации и не вошли в объединения
        single_vacancies = []
        for vacancy in all_vacancies:
            if self._should_publish(vacancy) and vacancy['id'] not in merged_ids:
                single_vacancies.append(vacancy)
        
        write_log(f"Отфильтровано {len(single_vacancies)} одиночных вакансий")
        return single_vacancies
    
    def _find_similar_vacancies_by_account(self, vacancies: List[Dict]) -> Dict[str, List[Dict]]:
        """Находит вакансии для объединения по номеру счёта (старая логика)"""
        write_log("Поиск вакансий для объединения по номеру счёта...")
        similar_vacancies = {}
        
        # Группируем вакансии по номеру счёта
        account_groups = {}
        for vacancy in vacancies:
            if not self._should_publish(vacancy):
                continue
            
            account_number = self._get_effective_account_number(vacancy)
            if account_number:
                if account_number not in account_groups:
                    account_groups[account_number] = []
                account_groups[account_number].append(vacancy)
        
        # Оставляем только группы с 2+ вакансиями
        for account_number, account_vacancies in account_groups.items():
            if len(account_vacancies) >= 2:
                similar_vacancies[account_number] = account_vacancies
                write_log(f"Найдено {len(account_vacancies)} вакансий для счёта '{account_number}'")
            else:
                write_log(f"Для счёта '{account_number}' найдено только {len(account_vacancies)} вакансий (требуется минимум 2)")
        
        write_log(f"Всего найдено {len(similar_vacancies)} групп для объединения по счету")
        return similar_vacancies
    
    def _should_create_new_union_by_account(self, account_number: str, vacancies: List[Dict]) -> bool:
        """Проверяет, нужно ли создавать новое объединенное объявление по номеру счёта"""
        write_log(f"Проверка необходимости нового объединения по счету '{account_number}'")
        if not vacancies:
            write_log("Нет вакансий для объединения по счету")
            return True
        
        sample_vacancy = vacancies[0]
        last_union_date = sample_vacancy.get('real_publication_union_acc_number_date')
        
        if not last_union_date:
            write_log(f"Для вакансий счета '{account_number}' нет даты последнего объединения - создаем новое")
            return True
        
        try:
            if isinstance(last_union_date, str):
                last_union_date = datetime.strptime(last_union_date, '%Y-%m-%d %H:%M:%S')
            
            time_since_last_union = datetime.now() - last_union_date
            required_interval = timedelta(days=self.merge_interval_days)
            
            if time_since_last_union < required_interval:
                write_log(f"Для объединения по счёту '{account_number}' не прошло достаточно времени: {time_since_last_union.days} дней из {self.merge_interval_days}")
                return False
            
            write_log(f"Для объединения по счету '{account_number}' прошло достаточно времени: {time_since_last_union.days} дней")
            return True
        except Exception as e:
            write_log(f"Ошибка проверки даты объединения по счёту: {str(e)}")
            write_log(f"Трассировка ошибки:\n{traceback.format_exc()}")
            return True
    
    def _generate_merged_post_by_account(self, vacancies: List[Dict]) -> str:
        """Генерирует текст объединенного поста по номеру счёта"""
        write_log(f"Генерация объединенного поста по счету для {len(vacancies)} вакансий")
        if not vacancies:
            write_log("Нет вакансий для объединения по счету")
            return ""
        
        post_parts = []
        if not self.hide_company_names and vacancies[0].get('company'):
            post_parts.append(f"📋 {vacancies[0]['company']}:")
        else:
            post_parts.append("📋 Вакансии:")
        post_parts.append("")
        
        for i, vacancy in enumerate(vacancies, 1):
            vacancy_part = []
            salary = self._apply_salary_threshold(vacancy.get('salary'))
            
            vacancy_part.append(f"{i}. {vacancy.get('vacancy', '').upper()}")
            vacancy_part.append(f"{self._get_salary_icon(salary)} Зарплата: {salary}")
            if ('work_schedule' in vacancy):
                if vacancy.get('work_schedule', ''):
                    vacancy_part.append(f"🕒  {vacancy.get('work_schedule')}")
            
            
            post_parts.append("\n".join(vacancy_part))
            post_parts.append("")
        
        contact_parts = []
        if vacancies[0].get('phone'):
            contact_parts.append(f"📞 {vacancies[0]['phone']}")
        if vacancies[0].get('contact_man'):
            contact_parts.append(f"👤 {vacancies[0]['contact_man']}")
        
        if contact_parts:
            post_parts.append("\n" + " | ".join(contact_parts))
        
        result = "\n".join(post_parts).strip()
        write_log(f"Сгенерирован объединенный пост по счету для {len(vacancies)} вакансий")
        return result
    
    def _delete_old_union_posts_by_account(self, vacancies: List[Dict], group_id: int):
        """Удаляет старые объединенные посты по номеру счёта"""
        write_log(f"Удаление старых объединенных постов по счету для {len(vacancies)} вакансий")
        for vacancy in vacancies:
            vacancy_id = vacancy.get('id', 'unknown')
            union_post_id = vacancy.get('vk_union_acc_number_id')
            if union_post_id:
                write_log(f"Удаление старого объединенного поста по счету {union_post_id} для вакансии {vacancy_id}")
                if self._delete_vk_post(group_id, union_post_id):
                    write_log(f"Удален старый объединенный пост по счёту {union_post_id} для вакансии {vacancy_id}")
                else:
                    write_log(f"Не удалось удалить старый объединенный пост по счету {union_post_id} для вакансии {vacancy_id}")
                
                updates = {
                    'vk_union_acc_number_link': None,
                    'vk_union_acc_number_id': None
                }
                self._update_vacancy_in_db(vacancy['id'], updates)
                write_log(f"Обновлена запись вакансии {vacancy_id} - очищены данные объединенного поста по счету")
            else:
                write_log(f"У вакансии {vacancy_id} нет старого объединенного поста по счету для удаления")
    
    def _process_merged_vacancies_by_account(self, merged_vacancies: Dict[str, List[Dict]], group_id: int):
        """Обрабатывает объединенные вакансии по номеру счёта"""
        write_log(f"Обработка {len(merged_vacancies)} групп объединенных вакансий по счету")
        for account_number, vacancies in merged_vacancies.items():
            write_log(f"Обработка объединения по счету '{account_number}' ({len(vacancies)} вакансий)")
            
            if not self._should_create_new_union_by_account(account_number, vacancies):
                write_log(f"Пропускаем объединение по счёту '{account_number}' - не прошло достаточно времени")
                continue
            
            self._delete_old_union_posts_by_account(vacancies, group_id)
            
            post_text = self._generate_merged_post_by_account(vacancies)
            post_url, post_id = self._post_to_vk(group_id, post_text)
            
            
            if post_url and post_id:
                current_time = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
                for vacancy in vacancies:
                    updates = {
                        'publication_state': 'published',
                        'vk_union_acc_number_link': post_url,
                        'vk_union_acc_number_id': post_id,
                        'real_publication_union_acc_number_date': current_time
                    }
                    self._update_vacancy_in_db(vacancy['id'], updates)
                
                write_log(f"Создано новое объединенное объявление по счёту '{account_number}' с {len(vacancies)} вакансиями, пост {post_id}")
            else:
                write_log(f"Ошибка создания объединенного объявления по счету '{account_number}'")
            write_log(f"Ожидание {self.post_interval} минут перед следующим объединенным постом по счету")
            time.sleep(self.post_interval * 60)
    
    def run(self, group_id: int):
        """Основной метод запуска публикации"""
        write_log(f"=== ЗАПУСК ПУБЛИКАЦИИ ДЛЯ ГРУППЫ {group_id} ===")
        try:
            self.config['group_id'] = group_id
            write_log(f"Group ID {group_id} сохранен в конфиг")
            
            if self.delete_all_posts:
                write_log("Опция удаления всех постов включена")
                self._delete_all_posts(group_id)
            else:
                write_log("Опция удаления всех постов отключена")
            
            cycle_count = 0
            while True:
                cycle_count += 1
                try:
                    write_log(f"=== ЦИКЛ ПУБЛИКАЦИИ #{cycle_count} ===")
                    
                    # §14 ТЗ: синхронизация просроченных в начале каждого цикла
                    write_log("ШАГ 0a: Синхронизация просроченных статусов")
                    self._sync_expired_announcement_statuses('publisher_cycle')
                    
                    # 0. Очищаем просроченные объявления
                    write_log("ШАГ 0: Очистка просроченных объявлений")
                    cleaned_count = self._cleanup_expired_vacancies()
                    if cleaned_count > 0:
                        write_log(f"Очищено {cleaned_count} просроченных объявлений")
                    else:
                        write_log("Просроченных объявлений не найдено")
                    
                    # 1. Загружаем вакансии из базы данных
                    write_log("ШАГ 1: Загрузка вакансий из БД")
                    vacancies = self._get_vacancies_from_db()
                    write_log(f"Загружено {len(vacancies)} вакансий из БД")
                    
                    # 2. Обрабатываем репосты (только если включены)
                    if self.repost_enabled:
                        write_log("ШАГ 2: Обработка репостов")
                        self._process_reposts(group_id)
                    else:
                        write_log("ШАГ 2: Репосты отключены, пропускаем")
                    
                    # 3. Находим схожие вакансии для объединения
                    write_log("ШАГ 3: Поиск вакансий для объединения")
                    merged_vacancies_by_name = {}
                    merged_vacancies_by_account = {}
                    
                    if (self.merge_vacancies or self.merge_by_number) and self._should_merge_vacancies():
                        write_log("Объединение вакансий включено, выполняем поиск...")
                        if self.merge_vacancies:
                            merged_vacancies_by_name = self._find_similar_vacancies_by_name(vacancies)
                            write_log(f"Найдено {len(merged_vacancies_by_name)} групп для объединения по названию")
                        if self.merge_by_number:
                            merged_vacancies_by_account = self._find_similar_vacancies_by_account(vacancies)
                            write_log(f"Найдено {len(merged_vacancies_by_account)} групп для объединения по счёту")
                        
                        self._update_merge_date()
                        write_log("Дата последнего объединения обновлена")
                    else:
                        write_log("Объединение вакансий отключено или не требуется в этом цикле")
                    
                    # 4. Фильтруем одиночные вакансии
                    write_log("ШАГ 4: Фильтрация одиночных вакансий")
                    
                    # Собираем ID вакансий, которые уже вошли в объединения
                    merged_ids = set()
                    merged_ids.update({v['id'] for group in merged_vacancies_by_name.values() for v in group if v is not None})
                    merged_ids.update({v['id'] for group in merged_vacancies_by_account.values() for v in group if v is not None})
                    
                    single_vacancies = []
                    for vacancy in vacancies:
                        #if self._should_publish(vacancy) and vacancy['id'] not in merged_ids:
                        if self._should_publish(vacancy):
                            single_vacancies.append(vacancy)
                    
                    write_log(f"Для одиночной публикации подходит {len(single_vacancies)} вакансий")
                    write_log(f"Всего ID вакансий в объединениях: {len(merged_ids)}")
                    
                    # Если нет новых вакансий для публикации - ждем
                    if not single_vacancies and not merged_vacancies_by_name and not merged_vacancies_by_account:
                        write_log("Нет новых вакансий для публикации, ждем 60 секунд...")
                        time.sleep(60)
                        continue
                    
                    # 5. Публикуем объединенные и одиночные вакансии
                    write_log("ШАГ 5: Публикация вакансий")
                    batch_size = 10
                    published_single = 0
                    published_merged_name = 0
                    published_merged_account = 0
                    
                    # Сначала публикуем все объединенные по названию
                    write_log("Публикация объединенных вакансий по названию...")
                    for first_word, merged_group in merged_vacancies_by_name.items():
                        if not self._is_posting_time():
                            write_log("Вне времени публикации, ждем 60 секунд...")
                            time.sleep(60)
                            continue
                        
                        self._process_merged_vacancies_by_name({first_word: merged_group}, group_id)
                        published_merged_name += 1
                        
                        # После каждого объединенного по названию - пачка одиночных
                        for i in range(published_single, len(single_vacancies), batch_size):
                            if not self._is_posting_time():
                                write_log("Вне времени публикации, ждем 60 секунд...")
                                time.sleep(60)
                                continue
                            
                            batch = single_vacancies[i:i + batch_size]
                            self._process_single_vacancies(batch, group_id)
                            published_single += len(batch)
                            write_log(f"Ожидание {self.post_interval} минут перед следующей пачкой")
                            time.sleep(self.post_interval * 60)
                    
                    # Затем публикуем все объединенные по номеру счёта
                    write_log("Публикация объединенных вакансий по счету...")
                    for account_number, merged_group in merged_vacancies_by_account.items():
                        if not self._is_posting_time():
                            write_log("Вне времени публикации, ждем 60 секунд...")
                            time.sleep(60)
                            continue
                        
                        self._process_merged_vacancies_by_account({account_number: merged_group}, group_id)
                        published_merged_account += 1
                        
                        # После каждого объединенного по счёту - пачка одиночных
                        for i in range(published_single, len(single_vacancies), batch_size):
                            if not self._is_posting_time():
                                write_log("Вне времени публикации, ждем 60 секунд...")
                                time.sleep(60)
                                continue
                            
                            batch = single_vacancies[i:i + batch_size]
                            self._process_single_vacancies(batch, group_id)
                            published_single += len(batch)
                    
                    # Если не было объединенных - публикуем все одиночные
                    if not merged_vacancies_by_name and not merged_vacancies_by_account:
                        write_log("Объединенных вакансий нет, публикуем только одиночные...")
                        for i in range(0, len(single_vacancies), batch_size):
                            if not self._is_posting_time():
                                write_log("Вне времени публикации, ждем 60 секунд...")
                                time.sleep(60)
                                continue
                            
                            batch = single_vacancies[i:i + batch_size]
                            self._process_single_vacancies(batch, group_id)
                            published_single += len(batch)
                    
                    write_log(f"ЦИКЛ #{cycle_count} ЗАВЕРШЕН. ИТОГИ:")
                    write_log(f"  - Опубликовано одиночных: {published_single}")
                    write_log(f"  - Опубликовано объединенных по названию: {published_merged_name}")
                    write_log(f"  - Опубликовано объединенных по счету: {published_merged_account}")
                    write_log(f"  - Всего опубликовано: {published_single + published_merged_name + published_merged_account}")
                    
                    write_log(f"=== КОНЕЦ ЦИКЛА ПУБЛИКАЦИИ #{cycle_count} ===\n")
                    
                    # Пауза перед следующим циклом проверки БД
                    write_log("Ожидание 60 секунд перед следующим циклом...")
                    time.sleep(60)
                    
                except Exception as e:
                    write_log(f"ОШИБКА В ЦИКЛЕ ПУБЛИКАЦИИ #{cycle_count}: {str(e)}")
                    write_log(f"Трассировка ошибки:\n{traceback.format_exc()}")
                    show_error_window(f"Ошибка в цикле публикации #{cycle_count}:\n{str(e)}\n\nСм. подробности в лог-файле.")
                    write_log("Ожидание 60 секунд перед повторной попыткой...")
                    time.sleep(60)
                
                finally:
                    # Проверяем соединение с БД
                    try:
                        self.db_connection.execute("SELECT 1")
                        write_log("Проверка соединения с БД: УСПЕШНО")
                    except sqlite3.Error as e:
                        write_log(f"Проверка соединения с БД: ОШИБКА - {str(e)}")
                        write_log("Переинициализация соединения с БД...")
                        self._init_db_connection()
        
        except Exception as e:
            error_msg = f"КРИТИЧЕСКАЯ ОШИБКА: {str(e)}"
            write_log(error_msg)
            write_log(f"Трассировка ошибки:\n{traceback.format_exc()}")
            show_error_window(f"Критическая ошибка в работе скрипта:\n{str(e)}\n\nРабота скрипта остановлена.\nСм. подробности в лог-файле.")
            write_log("=== ЗАВЕРШЕНИЕ РАБОТЫ ===")
            raise

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Публикация вакансий в VK')
    
    # Обязательные аргументы
    parser.add_argument('group_id', type=int, help='ID группы VK')
    parser.add_argument('refresh_token', type=str, help='Refresh Токен VK API')
    parser.add_argument('client_id', type=str, help='ID клиента (приложения) VK API')
    parser.add_argument('device_id', type=str, help='ID устройства VK API')
    
    # Аргументы базы данных
    parser.add_argument('--db_path', type=str, default='vk_publications.db', 
                       help='Путь к файлу SQLite (по умолчанию: vk_publications.db)')
    
    # Остальные аргументы
    parser.add_argument('--post_interval', type=int, default=10, 
                       help='Интервал между постами в минутах')
    parser.add_argument('--start_time', type=str, default='08:00', 
                       help='Время начала публикации (HH:MM)')
    parser.add_argument('--end_time', type=str, default='20:00', 
                       help='Время окончания публикации (HH:MM)')
    parser.add_argument('--round_the_clock', action='store_true', 
                       help='Публиковать круглосуточно')
    parser.add_argument('--no_repost', action='store_false', dest='repost_enabled',
                       help='Отключить повторную публикацию')
    parser.add_argument('--repost_interval_days', type=int, default=3, 
                       help='Интервал повторной публикации в днях')
    parser.add_argument('--no_merge_vacancies', action='store_false', dest='merge_vacancies',
                       help='Отключить объединение вакансий')
    parser.add_argument('--no_merge_by_number', action='store_false', dest='merge_by_number',
                       help='Отключить объединение по номеру счёта')
    parser.add_argument('--hide_company_names', action='store_true', 
                       help='Скрывать названия компаний')
    parser.add_argument('--salary_threshold', type=int, default=50000, 
                       help='Порог для замены зарплаты')
    parser.add_argument('--vacancy_filter', type=str, default="", 
                       help='XLSX файл с разрешенными названиями вакансий для публикации')
    parser.add_argument('--hide_additional_info', action='store_true', 
                       help='Скрывать дополнительную информацию')
    parser.add_argument('--hide_address', action='store_true', 
                       help='Скрывать адрес')
    parser.add_argument('--delete_all_posts', action='store_true',
                       help='Удалить все посты из группы перед началом работы')
    parser.add_argument('--hide_salary', action='store_true',
                       help='Скрывать низкую зарплату')
    parser.add_argument('--merge_periodically', action='store_true',
                       help='Объединять вакансии периодически раз в несколько дней')
    parser.add_argument('--merge_interval_days', type=int, default=3,
                       help='Интервал объединения вакансий в днях')
    
    args = parser.parse_args()
    
    log_filename = f"logs/{time.strftime('%Y-%m-%d %H-%M-%S')}.log"
    log_file = open(log_filename, "w", encoding="utf-8")
    write_log(f"Лог-файл создан: {log_filename}")
    
    write_log("ПАРАМЕТРЫ ЗАПУСКА:")
    write_log(f"  group_id: {args.group_id}")
    write_log(f"  client_id: {args.client_id}")
    write_log(f"  device_id: {args.device_id}")
    write_log(f"  db_path: {args.db_path}")
    write_log(f"  post_interval: {args.post_interval}")
    write_log(f"  start_time: {args.start_time}")
    write_log(f"  end_time: {args.end_time}")
    write_log(f"  round_the_clock: {args.round_the_clock}")
    write_log(f"  repost_enabled: {args.repost_enabled}")
    write_log(f"  repost_interval_days: {args.repost_interval_days}")
    write_log(f"  merge_vacancies: {args.merge_vacancies}")
    write_log(f"  merge_by_number: {args.merge_by_number}")
    write_log(f"  hide_company_names: {args.hide_company_names}")
    write_log(f"  salary_threshold: {args.salary_threshold}")
    write_log(f"  vacancy_filter: {args.vacancy_filter}")
    write_log(f"  hide_additional_info: {args.hide_additional_info}")
    write_log(f"  hide_address: {args.hide_address}")
    write_log(f"  delete_all_posts: {args.delete_all_posts}")
    write_log(f"  hide_salary: {args.hide_salary}")
    write_log(f"  merge_periodically: {args.merge_periodically}")
    write_log(f"  merge_interval_days: {args.merge_interval_days}")
    
    config = {
        'group_id': args.group_id,
        'refresh_token': args.refresh_token,
        'client_id': args.client_id,
        'device_id': args.device_id,
        'db_path': args.db_path,
        'post_interval': args.post_interval,
        'start_time': args.start_time,
        'end_time': args.end_time,
        'round_the_clock': args.round_the_clock,
        'repost_enabled': args.repost_enabled,
        'repost_interval_days': args.repost_interval_days,
        'merge_vacancies': args.merge_vacancies,
        'merge_by_number': args.merge_by_number,
        'hide_company_names': args.hide_company_names,
        'salary_threshold': args.salary_threshold,
        'vacancy_filter': args.vacancy_filter,
        'hide_additional_info': args.hide_additional_info,
        'hide_address': args.hide_address,
        'delete_all_posts': args.delete_all_posts,
        'hide_salary': args.hide_salary,
        'merge_periodically': args.merge_periodically,
        'merge_interval_days': args.merge_interval_days,
    }
    
    try:
        os.mkdir("logs")
        write_log("Создана директория logs")
    except FileExistsError:
        write_log("Директория logs уже существует")
    
    try:
        publisher = VKPostPublisher(config)
        write_log("Публикатор успешно создан, запуск основного цикла...")
        publisher.run(args.group_id)
    except Exception as e:
        error_msg = f"Ошибка запуска: {str(e)}"
        write_log(error_msg)
        write_log(f"Трассировка ошибки:\n{traceback.format_exc()}")
        show_error_window(f"Ошибка запуска скрипта:\n{str(e)}\n\nСм. подробности в лог-файле: {log_filename}")
    finally:
        write_log("=== ЗАВЕРШЕНИЕ РАБОТЫ СКРИПТА ===")
        log_file.close()
"""
parse_subscribers.py — Парсинг подписчиков сообществ VK.

Запуск:
    python parse_subscribers.py <group_id> <refresh_token> <client_id> <device_id>
        --db_path vk_publications.db
        --mode parse                # parse | remove_deactivated
        --interval 1800             # интервал между полными парсингами (сек)
"""
import sqlite3
import vk_api
from vk_api.exceptions import VkApiError, ApiError
from datetime import datetime
import time
import json
import argparse
import requests
import random
import os
import sys
import traceback
from typing import List, Dict, Optional, Set

# ═══════════════════════════════════════════════════════════════════════════
# LOGGING
# ═══════════════════════════════════════════════════════════════════════════

log_file = None

def write_log(s):
    global log_file
    timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    line = f"[{timestamp}] {s}\n"
    if log_file:
        log_file.write(line)
        log_file.flush()
    try:
        sys.stdout.buffer.write(line.encode(sys.stdout.encoding or "utf-8", errors="replace"))
        sys.stdout.flush()
    except Exception:
        pass

def show_error_window(message: str):
    try:
        import tkinter as tk
        from tkinter import messagebox
        root = tk.Tk()
        root.withdraw()
        messagebox.showerror("Ошибка парсинга подписчиков", message)
        root.destroy()
    except Exception:
        print(f"Ошибка: {message}")

def get_state():
    chars = "abcdefghijkmnlopqrstuvwzyx_-0123456789" + "abcdefghijkmnlopqrstuvwzyx".upper()
    return "".join(random.choice(chars) for _ in range(random.randint(32, 64)))

# ═══════════════════════════════════════════════════════════════════════════
# MAIN CLASS
# ═══════════════════════════════════════════════════════════════════════════

class VKSubscriberParser:

    def __init__(self, config: Dict):
        write_log("Инициализация VKSubscriberParser...")
        self.config = config
        self.group_id = config['group_id']           # VK group_id (числовой)
        self.refresh_token = config['refresh_token']
        self.client_id = config['client_id']
        self.device_id = config['device_id']
        self.interval = config.get('interval', 1800)
        self.mode = config.get('mode', 'parse')

        self.vk_session = None
        self.vk = None
        self.db = None

        time.sleep(3)
        self._init_db()
        self._init_vk()
        write_log("VKSubscriberParser инициализирован")

    # ─── VK Auth ──────────────────────────────────────────────────────────

    def get_access_token(self) -> str:
        write_log("Обновление VK access token...")
        state = get_state()
        data = {
            "client_id": self.client_id,
            "grant_type": "refresh_token",
            "refresh_token": self.refresh_token,
            "device_id": self.device_id,
            "state": state,
        }
        try:
            r = requests.post("https://id.vk.com/oauth2/auth", data=data)
            response = r.json()
            if 'error' in response:
                raise Exception(f"VK OAuth: {response.get('error')} — {response.get('error_description','')}")
            self.refresh_token = response["refresh_token"]
            # Обновляем refresh_token в БД
            cur = self.db.cursor()
            cur.execute("UPDATE vk_groups SET refresh_token = ? WHERE group_id = ?",
                        (self.refresh_token, self.group_id))
            self.db.commit()
            write_log("Access token получен")
            return response["access_token"]
        except Exception as e:
            write_log(f"Ошибка получения токена: {e}")
            raise

    def _init_vk(self):
        write_log("Подключение к VK API...")
        try:
            token = self.get_access_token()
            self.vk_session = vk_api.VkApi(token=token)
            self.vk = self.vk_session.get_api()
            write_log("VK API подключен")
        except Exception as e:
            write_log(f"Ошибка VK API: {e}")
            show_error_window(f"Ошибка VK API:\n{e}")
            raise

    # ─── SQLite ───────────────────────────────────────────────────────────

    def _init_db(self):
        db_path = self.config.get('db_path', 'vk_publications.db')
        write_log(f"Подключение к SQLite: {db_path}")
        try:
            self.db = sqlite3.connect(db_path, timeout=30)
            self.db.row_factory = sqlite3.Row
            self.db.execute("PRAGMA foreign_keys = ON")
            self.db.execute("PRAGMA journal_mode = WAL")
            self.db.execute("PRAGMA busy_timeout = 30000")
            self.db.isolation_level = None
            self._ensure_tables()
            write_log("SQLite подключен")
        except sqlite3.Error as e:
            write_log(f"Ошибка SQLite: {e}")
            show_error_window(f"Ошибка БД:\n{e}")
            raise

    def _ensure_tables(self):
        cur = self.db.cursor()
        cur.executescript("""
            CREATE TABLE IF NOT EXISTS subscribers (
                id                INTEGER PRIMARY KEY AUTOINCREMENT,
                vk_user_id        INTEGER NOT NULL,
                first_name        TEXT,
                last_name         TEXT,
                profile_link      TEXT,
                is_deactivated    INTEGER NOT NULL DEFAULT 0,
                deactivated_type  TEXT,
                created_at        TEXT DEFAULT (datetime('now','localtime')),
                UNIQUE(vk_user_id)
            );

            CREATE TABLE IF NOT EXISTS subscriber_group_membership (
                id                INTEGER PRIMARY KEY AUTOINCREMENT,
                subscriber_id     INTEGER NOT NULL,
                group_record_id   INTEGER NOT NULL,
                subscribed_at     TEXT,
                unsubscribed_at   TEXT,
                is_active         INTEGER NOT NULL DEFAULT 1,
                last_seen_at      TEXT,
                removed_by_admin  INTEGER NOT NULL DEFAULT 0,
                UNIQUE(subscriber_id, group_record_id),
                FOREIGN KEY (subscriber_id) REFERENCES subscribers(id) ON DELETE CASCADE,
                FOREIGN KEY (group_record_id) REFERENCES vk_groups(id) ON DELETE CASCADE
            );

            CREATE TABLE IF NOT EXISTS subscriber_event_log (
                id                INTEGER PRIMARY KEY AUTOINCREMENT,
                subscriber_id     INTEGER NOT NULL,
                group_record_id   INTEGER NOT NULL,
                event_type        TEXT NOT NULL,
                event_datetime    TEXT NOT NULL,
                details           TEXT,
                FOREIGN KEY (subscriber_id) REFERENCES subscribers(id) ON DELETE CASCADE,
                FOREIGN KEY (group_record_id) REFERENCES vk_groups(id) ON DELETE CASCADE
            );

            CREATE TABLE IF NOT EXISTS subscriber_parsing_state (
                group_record_id   INTEGER PRIMARY KEY,
                last_full_parse_at TEXT,
                total_members     INTEGER DEFAULT 0,
                status            TEXT DEFAULT 'idle',
                error_message     TEXT,
                FOREIGN KEY (group_record_id) REFERENCES vk_groups(id) ON DELETE CASCADE
            );

            CREATE INDEX IF NOT EXISTS idx_sub_vk_user ON subscribers(vk_user_id);
            CREATE INDEX IF NOT EXISTS idx_sub_membership_group ON subscriber_group_membership(group_record_id);
            CREATE INDEX IF NOT EXISTS idx_sub_membership_active ON subscriber_group_membership(is_active);
            CREATE INDEX IF NOT EXISTS idx_sub_event_subscriber ON subscriber_event_log(subscriber_id);
            CREATE INDEX IF NOT EXISTS idx_sub_event_datetime ON subscriber_event_log(event_datetime);
        """)
        write_log("Таблицы подписчиков созданы/проверены")

    # ─── Получение group_record_id из vk_groups ──────────────────────────

    def _get_group_record_id(self) -> int:
        cur = self.db.cursor()
        cur.execute("SELECT id FROM vk_groups WHERE group_id = ?", (self.group_id,))
        row = cur.fetchone()
        if row is None:
            raise Exception(f"Сообщество VK group_id={self.group_id} не найдено в таблице vk_groups")
        return row['id']

    # ═══════════════════════════════════════════════════════════════════════
    # PARSING
    # ═══════════════════════════════════════════════════════════════════════

    def _fetch_all_members(self) -> List[Dict]:
        """Получает всех подписчиков сообщества через VK API execute (25000 за запрос)."""
        write_log(f"Получение подписчиков для group_id={self.group_id}...")

        # Сначала узнаём общее количество
        try:
            info = self.vk.groups.getMembers(group_id=self.group_id, count=0)
            total = info['count']
        except ApiError:
            write_log("Ошибка API, переподключение...")
            self._init_vk()
            info = self.vk.groups.getMembers(group_id=self.group_id, count=0)
            total = info['count']

        write_log(f"Всего подписчиков по данным VK: {total}")

        if total == 0:
            return []

        members = []
        offset = 0

        while offset < total:
            # Через execute: до 25 вызовов × 1000 = 25000 за один запрос
            code = '''
            var members = [];
            var offset = ''' + str(offset) + ''';
            var total = ''' + str(total) + ''';
            var i = 0;
            var go = 1;
            while (go) {
                if (i >= 25) { go = 0; }
                if (offset >= total) { go = 0; }
                if (go == 1) {
                    var batch = API.groups.getMembers({
                        "group_id": ''' + str(self.group_id) + ''',
                        "offset": offset,
                        "count": 1000,
                        "fields": "first_name,last_name,deactivated"
                    });
                    if (batch) {
                        if (batch.items) {
                            members = members + batch.items;
                        }
                    }
                    offset = offset + 1000;
                    i = i + 1;
                }
            };
            return members;
            '''

            try:
                result = self.vk.execute(code=code)
            except ApiError:
                write_log(f"Ошибка execute на offset={offset}, переподключение...")
                self._init_vk()
                result = self.vk.execute(code=code)

            if result:
                members.extend(result)

            offset += 25000
            write_log(f"  Получено: {len(members)} / {total}")
            time.sleep(0.4)

        write_log(f"Получено {len(members)} подписчиков")
        return members

    # ─── DIFF Logic ───────────────────────────────────────────────────────

    def _get_active_db_subscribers(self, group_record_id: int) -> Dict[int, Dict]:
        """Возвращает {vk_user_id: {subscriber_id, is_deactivated, ...}} для активных подписчиков."""
        cur = self.db.cursor()
        cur.execute("""
            SELECT s.id AS subscriber_id, s.vk_user_id, s.is_deactivated, s.deactivated_type
            FROM subscribers s
            JOIN subscriber_group_membership m ON m.subscriber_id = s.id
            WHERE m.group_record_id = ? AND m.is_active = 1
        """, (group_record_id,))
        return {row['vk_user_id']: dict(row) for row in cur.fetchall()}

    def _get_all_db_subscribers(self, group_record_id: int) -> Dict[int, Dict]:
        """Все подписчики (включая отписавшихся) для проверки повторной подписки."""
        cur = self.db.cursor()
        cur.execute("""
            SELECT s.id AS subscriber_id, s.vk_user_id, m.is_active
            FROM subscribers s
            JOIN subscriber_group_membership m ON m.subscriber_id = s.id
            WHERE m.group_record_id = ?
        """, (group_record_id,))
        return {row['vk_user_id']: dict(row) for row in cur.fetchall()}

    def _upsert_subscriber(self, vk_user_id: int, member: Dict) -> int:
        """INSERT OR UPDATE подписчика, возвращает subscriber.id."""
        cur = self.db.cursor()
        first_name = member.get('first_name', '')
        last_name = member.get('last_name', '')
        is_deact = 1 if 'deactivated' in member else 0
        deact_type = member.get('deactivated')
        profile_link = f"https://vk.com/id{vk_user_id}"

        cur.execute("SELECT id FROM subscribers WHERE vk_user_id = ?", (vk_user_id,))
        row = cur.fetchone()
        if row:
            cur.execute("""
                UPDATE subscribers
                SET first_name = ?, last_name = ?, is_deactivated = ?,
                    deactivated_type = ?, profile_link = ?
                WHERE id = ?
            """, (first_name, last_name, is_deact, deact_type, profile_link, row['id']))
            return row['id']
        else:
            cur.execute("""
                INSERT INTO subscribers (vk_user_id, first_name, last_name,
                    profile_link, is_deactivated, deactivated_type)
                VALUES (?, ?, ?, ?, ?, ?)
            """, (vk_user_id, first_name, last_name, profile_link, is_deact, deact_type))
            return cur.lastrowid

    def _add_membership(self, subscriber_id: int, group_record_id: int, now: str):
        cur = self.db.cursor()
        cur.execute("""
            INSERT INTO subscriber_group_membership
                (subscriber_id, group_record_id, subscribed_at, is_active, last_seen_at)
            VALUES (?, ?, ?, 1, ?)
            ON CONFLICT(subscriber_id, group_record_id) DO UPDATE SET
                is_active = 1,
                unsubscribed_at = NULL,
                last_seen_at = ?,
                subscribed_at = COALESCE(subscriber_group_membership.subscribed_at, ?)
        """, (subscriber_id, group_record_id, now, now, now, now))

    def _mark_unsubscribed(self, subscriber_id: int, group_record_id: int, now: str):
        cur = self.db.cursor()
        cur.execute("""
            UPDATE subscriber_group_membership
            SET is_active = 0, unsubscribed_at = ?
            WHERE subscriber_id = ? AND group_record_id = ?
        """, (now, subscriber_id, group_record_id))

    def _update_last_seen(self, subscriber_id: int, group_record_id: int, now: str):
        cur = self.db.cursor()
        cur.execute("""
            UPDATE subscriber_group_membership
            SET last_seen_at = ?
            WHERE subscriber_id = ? AND group_record_id = ?
        """, (now, subscriber_id, group_record_id))

    def _log_event(self, subscriber_id: int, group_record_id: int,
                   event_type: str, event_datetime: str, details: str = None):
        cur = self.db.cursor()
        cur.execute("""
            INSERT INTO subscriber_event_log
                (subscriber_id, group_record_id, event_type, event_datetime, details)
            VALUES (?, ?, ?, ?, ?)
        """, (subscriber_id, group_record_id, event_type, event_datetime, details))

    def _update_parsing_state(self, group_record_id: int, total: int,
                              status: str, error: str = None):
        now = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        cur = self.db.cursor()
        cur.execute("""
            INSERT INTO subscriber_parsing_state
                (group_record_id, last_full_parse_at, total_members, status, error_message)
            VALUES (?, ?, ?, ?, ?)
            ON CONFLICT(group_record_id) DO UPDATE SET
                last_full_parse_at = ?,
                total_members = ?,
                status = ?,
                error_message = ?
        """, (group_record_id, now, total, status, error,
              now, total, status, error))

    # ─── Process diff ─────────────────────────────────────────────────────

    def _process_diff(self, group_record_id: int, vk_members: List[Dict]):
        now = datetime.now().strftime('%d.%m.%Y %H:%M')

        db_active = self._get_active_db_subscribers(group_record_id)
        db_active_ids: Set[int] = set(db_active.keys())
        db_all = self._get_all_db_subscribers(group_record_id)

        vk_map: Dict[int, Dict] = {}
        vk_ids: Set[int] = set()
        for m in vk_members:
            uid = m.get('id', 0)
            if uid > 0:
                vk_ids.add(uid)
                vk_map[uid] = m

        new_ids = vk_ids - set(db_all.keys())      # совсем новые
        returned_ids = (vk_ids & set(db_all.keys())) - db_active_ids  # вернувшиеся
        left_ids = db_active_ids - vk_ids            # отписались
        still_active_ids = db_active_ids & vk_ids    # остались

        # ── Новые подписчики ──
        write_log(f"Новых подписчиков: {len(new_ids)}")
        for vk_id in new_ids:
            m = vk_map[vk_id]
            sub_id = self._upsert_subscriber(vk_id, m)
            self._add_membership(sub_id, group_record_id, now)
            self._log_event(sub_id, group_record_id, 'subscribed', now)

        # ── Вернувшиеся (были отписаны, снова в списке) ──
        write_log(f"Вернувшихся подписчиков: {len(returned_ids)}")
        for vk_id in returned_ids:
            m = vk_map[vk_id]
            sub_id = self._upsert_subscriber(vk_id, m)
            self._add_membership(sub_id, group_record_id, now)
            self._log_event(sub_id, group_record_id, 'subscribed', now,
                            'Повторная подписка')

        # ── Отписавшиеся ──
        write_log(f"Отписавшихся: {len(left_ids)}")
        for vk_id in left_ids:
            sub_info = db_active[vk_id]
            sub_id = sub_info['subscriber_id']
            self._mark_unsubscribed(sub_id, group_record_id, now)
            self._log_event(sub_id, group_record_id, 'unsubscribed', now)

        # ── Проверка смены статуса «собачка» у оставшихся ──
        deact_changed = 0
        for vk_id in still_active_ids:
            m = vk_map[vk_id]
            sub_info = db_active[vk_id]
            sub_id = sub_info['subscriber_id']

            was_deact = sub_info['is_deactivated']
            now_deact = 1 if 'deactivated' in m else 0

            # Обновляем last_seen
            self._update_last_seen(sub_id, group_record_id, now)

            if was_deact != now_deact:
                deact_type = m.get('deactivated')
                self._upsert_subscriber(vk_id, m)  # обновляет is_deactivated
                if now_deact:
                    self._log_event(sub_id, group_record_id, 'deactivated', now,
                                    deact_type or 'banned')
                else:
                    self._log_event(sub_id, group_record_id, 'reactivated', now)
                deact_changed += 1

        write_log(f"Изменений статуса «собачка»: {deact_changed}")
        self.db.commit()

        write_log(f"ИТОГО: +{len(new_ids)} новых, +{len(returned_ids)} вернувшихся, "
                  f"-{len(left_ids)} отписавшихся, {deact_changed} изменений статуса")

    # ═══════════════════════════════════════════════════════════════════════
    # REMOVE DEACTIVATED
    # ═══════════════════════════════════════════════════════════════════════

    def _remove_deactivated(self, group_record_id: int):
        """Удаляет всех собачек из сообщества VK."""
        write_log("Удаление собачек из сообщества...")
        cur = self.db.cursor()
        cur.execute("""
            SELECT s.vk_user_id, s.id AS subscriber_id
            FROM subscribers s
            JOIN subscriber_group_membership m ON m.subscriber_id = s.id
            WHERE m.group_record_id = ? AND m.is_active = 1 AND s.is_deactivated = 1
        """, (group_record_id,))
        rows = cur.fetchall()
        write_log(f"Найдено {len(rows)} собачек для удаления")

        removed = 0
        now = datetime.now().strftime('%d.%m.%Y %H:%M')
        for row in rows:
            vk_uid = row['vk_user_id']
            sub_id = row['subscriber_id']
            try:
                self.vk.groups.removeUser(
                    group_id=self.group_id,
                    user_id=vk_uid
                )
                write_log(f"Удалён пользователь VK id{vk_uid}")

                # Обновляем БД
                cur.execute("""
                    UPDATE subscriber_group_membership
                    SET is_active = 0, removed_by_admin = 1, unsubscribed_at = ?
                    WHERE subscriber_id = ? AND group_record_id = ?
                """, (now, sub_id, group_record_id))
                self._log_event(sub_id, group_record_id, 'removed_by_admin', now)
                removed += 1
                time.sleep(0.5)

            except ApiError:
                write_log(f"Ошибка API при удалении {vk_uid}, переподключение...")
                self._init_vk()
                try:
                    self.vk.groups.removeUser(group_id=self.group_id, user_id=vk_uid)
                    cur.execute("""
                        UPDATE subscriber_group_membership
                        SET is_active = 0, removed_by_admin = 1, unsubscribed_at = ?
                        WHERE subscriber_id = ? AND group_record_id = ?
                    """, (now, sub_id, group_record_id))
                    self._log_event(sub_id, group_record_id, 'removed_by_admin', now)
                    removed += 1
                    time.sleep(0.5)
                except Exception as e2:
                    write_log(f"Повторная ошибка удаления {vk_uid}: {e2}")

            except Exception as e:
                write_log(f"Ошибка удаления {vk_uid}: {e}")

        self.db.commit()
        write_log(f"Удаление завершено: {removed} из {len(rows)}")

    # ═══════════════════════════════════════════════════════════════════════
    # RUN
    # ═══════════════════════════════════════════════════════════════════════

    def run(self):
        group_record_id = self._get_group_record_id()
        write_log(f"group_record_id = {group_record_id} (VK group_id={self.group_id})")

        if self.mode == 'remove_deactivated':
            write_log("=== РЕЖИМ: УДАЛЕНИЕ СОБАЧЕК ===")
            # Сначала парсим актуальный список, чтобы обновить статусы
            members = self._fetch_all_members()
            self._process_diff(group_record_id, members)
            self._remove_deactivated(group_record_id)
            write_log("=== УДАЛЕНИЕ СОБАЧЕК ЗАВЕРШЕНО ===")
            return

        # mode == 'parse'
        write_log("=== РЕЖИМ: ПАРСИНГ ПОДПИСЧИКОВ ===")
        cycle = 0
        while True:
            cycle += 1
            try:
                write_log(f"=== ЦИКЛ ПАРСИНГА #{cycle} ===")
                self._update_parsing_state(group_record_id, 0, 'running')

                members = self._fetch_all_members()
                self._process_diff(group_record_id, members)
                self._update_parsing_state(group_record_id, len(members), 'idle')

                write_log(f"=== ЦИКЛ #{cycle} ЗАВЕРШЁН. Ожидание {self.interval} сек ===")
                time.sleep(self.interval)

            except Exception as e:
                write_log(f"ОШИБКА В ЦИКЛЕ #{cycle}: {e}")
                write_log(traceback.format_exc())
                self._update_parsing_state(group_record_id, 0, 'error', str(e))
                write_log("Ожидание 60 сек перед повтором...")
                time.sleep(60)

            finally:
                try:
                    self.db.execute("SELECT 1")
                except sqlite3.Error:
                    write_log("Переподключение к SQLite...")
                    self._init_db()


# ═══════════════════════════════════════════════════════════════════════════
# MAIN
# ═══════════════════════════════════════════════════════════════════════════

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Парсинг подписчиков VK сообществ')

    parser.add_argument('group_id', type=int, help='ID группы VK')
    parser.add_argument('refresh_token', type=str, help='Refresh Token VK API')
    parser.add_argument('client_id', type=str, help='Client ID VK API')
    parser.add_argument('device_id', type=str, help='Device ID VK API')

    parser.add_argument('--db_path', type=str, default='vk_publications.db',
                        help='Путь к файлу SQLite')
    parser.add_argument('--mode', type=str, default='parse',
                        choices=['parse', 'remove_deactivated'],
                        help='Режим работы: parse или remove_deactivated')
    parser.add_argument('--interval', type=int, default=1800,
                        help='Интервал между полными парсингами (секунды)')

    args = parser.parse_args()

    try:
        os.mkdir("logs")
    except FileExistsError:
        pass

    log_filename = f"logs/subscribers_{time.strftime('%Y-%m-%d_%H-%M-%S')}.log"
    log_file = open(log_filename, "w", encoding="utf-8")
    write_log(f"Лог-файл: {log_filename}")
    write_log(f"ПАРАМЕТРЫ: group_id={args.group_id}, mode={args.mode}, "
              f"interval={args.interval}, db_path={args.db_path}")

    config = {
        'group_id': args.group_id,
        'refresh_token': args.refresh_token,
        'client_id': args.client_id,
        'device_id': args.device_id,
        'db_path': args.db_path,
        'mode': args.mode,
        'interval': args.interval,
    }

    try:
        subscriber_parser = VKSubscriberParser(config)
        subscriber_parser.run()
    except Exception as e:
        write_log(f"КРИТИЧЕСКАЯ ОШИБКА: {e}")
        write_log(traceback.format_exc())
        show_error_window(f"Критическая ошибка:\n{e}\n\nСм. лог: {log_filename}")
    finally:
        write_log("=== ЗАВЕРШЕНИЕ РАБОТЫ ===")
        if log_file:
            log_file.close()

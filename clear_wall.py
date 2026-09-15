import vk_api
from vk_api.exceptions import VkApiError, ApiError
import time
import argparse
import requests
import random
import sqlite3
from datetime import datetime, date
from typing import Dict, Optional, Set


def get_state() -> str:
    alphabet = "abcdefghijkmnlopqrstuvwzyx_-0123456789" + "abcdefghijkmnlopqrstuvwzyx".upper()
    return "".join(random.choice(alphabet) for _ in range(random.randint(32, 64)))


# Поля VK-поста в таблице announcement_publications.
# Порядок важен: одиночный пост идёт первым, чтобы при expired-режиме
# мы обрабатывали его независимо от объединённых.
_VK_POST_FIELDS = ("vk_id", "vk_union_vacancy_id", "vk_union_acc_number_id")


class VKPostPublisher:
    """
    Утилита очистки стены VK-сообщества.

    Режимы (--mode):
        all     — удалить ВСЕ посты (поведение по умолчанию, как раньше).
        expired — удалить только те посты, у которых дата депубликации
                  последнего счётного блока объявления ≤ сегодня (ТЗ §3).
    """

    def __init__(self, config: Dict, mode: str = "all"):
        if mode not in ("all", "expired"):
            raise ValueError(f"Неизвестный режим: {mode!r}. Ожидается 'all' или 'expired'.")

        self.config = config
        self.mode = mode
        self.group_id: int = config["group_id"]
        self.refresh_token: str = config["refresh_token"]
        self.client_id: str = config["client_id"]
        self.device_id: str = config["device_id"]

        self.vk_session = None
        self.vk = None
        self.db_connection: Optional[sqlite3.Connection] = None

        time.sleep(3)
        self._init_db_connection()
        self._init_vk_connection()

    # ──────────────────────────────────────────────────────────────────────────
    # Инициализация
    # ──────────────────────────────────────────────────────────────────────────

    def get_access_token(self) -> str:
        """Обновляет и возвращает access_token через refresh_token."""
        state = get_state()
        payload = {
            "client_id": self.client_id,
            "grant_type": "refresh_token",
            "refresh_token": self.refresh_token,
            "device_id": self.device_id,
            "state": state,
        }
        response = requests.post("https://id.vk.com/oauth2/auth", data=payload).json()
        print(response)

        self.refresh_token = response["refresh_token"]
        cursor = self.db_connection.cursor()
        cursor.execute(
            "UPDATE vk_groups SET refresh_token = ? WHERE group_id = ?",
            (self.refresh_token, self.group_id),
        )
        self.db_connection.commit()
        return response["access_token"]

    def _init_vk_connection(self) -> None:
        try:
            token = self.get_access_token()
            self.vk_session = vk_api.VkApi(token=token)
            self.vk = self.vk_session.get_api()
        except Exception as e:
            raise Exception(f"Ошибка подключения к VK API: {e}")

    def _init_db_connection(self) -> None:
        try:
            db_path = self.config.get("db_path", "vacancies.db")
            self.db_connection = sqlite3.connect(db_path)
            self.db_connection.row_factory = sqlite3.Row
            self.db_connection.execute("PRAGMA foreign_keys = ON")
            print(f"Подключение к SQLite установлено: {db_path}")
            self._ensure_new_columns()
        except sqlite3.Error as e:
            raise Exception(f"Ошибка подключения к SQLite: {e}")

    def _ensure_new_columns(self) -> None:
        """Добавляет отсутствующие колонки (миграция на лету)."""
        cursor = self.db_connection.cursor()

        pub_cols = {row[1] for row in cursor.execute("PRAGMA table_info(announcement_publications)").fetchall()}
        new_pub_cols = {
            "publication_state": "ALTER TABLE announcement_publications ADD COLUMN publication_state TEXT NOT NULL DEFAULT 'not_published'",
            "real_depublication_date": "ALTER TABLE announcement_publications ADD COLUMN real_depublication_date TEXT",
            "depublication_error": "ALTER TABLE announcement_publications ADD COLUMN depublication_error TEXT",
            "last_state_updated_at": "ALTER TABLE announcement_publications ADD COLUMN last_state_updated_at TEXT",
        }
        for col, ddl in new_pub_cols.items():
            if col not in pub_cols:
                try:
                    cursor.execute(ddl)
                except sqlite3.OperationalError:
                    pass

        ann_cols = {row[1] for row in cursor.execute("PRAGMA table_info(announcements)").fetchall()}
        new_ann_cols = {
            "status": "ALTER TABLE announcements ADD COLUMN status TEXT NOT NULL DEFAULT 'Активное'",
            "is_publication_blocked": "ALTER TABLE announcements ADD COLUMN is_publication_blocked INTEGER NOT NULL DEFAULT 0",
            "status_updated_at": "ALTER TABLE announcements ADD COLUMN status_updated_at TEXT",
            "status_update_source": "ALTER TABLE announcements ADD COLUMN status_update_source TEXT",
            "manual_finished_at": "ALTER TABLE announcements ADD COLUMN manual_finished_at TEXT",
            "manual_finished_reason": "ALTER TABLE announcements ADD COLUMN manual_finished_reason TEXT",
        }
        for col, ddl in new_ann_cols.items():
            if col not in ann_cols:
                try:
                    cursor.execute(ddl)
                except sqlite3.OperationalError:
                    pass

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
        self.db_connection.commit()

    # ──────────────────────────────────────────────────────────────────────────
    # Обновление БД после удаления поста
    # ──────────────────────────────────────────────────────────────────────────

    def _update_vacancy_depublication(
        self,
        post_id: int,
        group_id: int,
        source: str = "depublication_all",
    ) -> int:
        """
        Обновляет статус публикаций, связанных с удалённым постом (§19 ТЗ).

        source:
            'depublication_all'     — пост удалён в режиме «очистить всё»
            'depublication_expired' — пост удалён как просроченный (ТЗ §3)
        """
        try:
            now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            cursor = self.db_connection.cursor()
            total_updated = 0

            for vk_field in _VK_POST_FIELDS:
                cursor.execute(
                    f"""
                    SELECT announcement_id, publication_state
                    FROM announcement_publications
                    WHERE group_id = ? AND {vk_field} = ? AND publication_state = 'published'
                    """,
                    (group_id, post_id),
                )
                rows = cursor.fetchall()

                for row in rows:
                    ann_id = row["announcement_id"]
                    old_state = row["publication_state"]

                    cursor.execute(
                        f"""
                        UPDATE announcement_publications
                        SET publication_state     = 'deleted',
                            real_depublication_date = ?,
                            last_state_updated_at  = ?,
                            status                 = 'Завершённое'
                        WHERE group_id = ? AND {vk_field} = ? AND publication_state = 'published'
                        """,
                        (now, now, group_id, post_id),
                    )
                    total_updated += cursor.rowcount

                    cursor.execute(
                        """
                        UPDATE announcements
                        SET status              = 'Завершенное',
                            status_updated_at   = ?,
                            status_update_source = ?
                        WHERE id = ?
                        """,
                        (now, source, ann_id),
                    )

                    cursor.execute(
                        """
                        INSERT INTO announcement_publication_state_log
                            (announcement_id, group_id, old_state, new_state, vk_post_id, source, created_at)
                        VALUES (?, ?, ?, 'deleted', ?, ?, ?)
                        """,
                        (ann_id, group_id, old_state, str(post_id), source, now),
                    )

                    cursor.execute(
                        """
                        INSERT INTO announcement_status_log
                            (announcement_id, old_status, new_status, source, created_at)
                        VALUES (
                            ?,
                            (SELECT status FROM announcements WHERE id = ?),
                            'Завершенное',
                            ?,
                            ?
                        )
                        """,
                        (ann_id, ann_id, source, now),
                    )

            self.db_connection.commit()

            if total_updated > 0:
                print(f"  БД: обновлено {total_updated} публикаций для поста {post_id} (группа {group_id})")

            return total_updated

        except sqlite3.Error as e:
            print(f"  Ошибка обновления БД для поста {post_id}: {e}")
            return 0

    # ──────────────────────────────────────────────────────────────────────────
    # Поиск просроченных постов
    # ──────────────────────────────────────────────────────────────────────────

    def _get_expired_post_ids(self, group_id: int) -> Set[int]:
        """
        Возвращает множество VK post_id, у которых дата депубликации последнего
        счётного блока объявления ≤ сегодня (ТЗ §3).

        Учитывает три типа ссылок: vk_id, vk_union_vacancy_id, vk_union_acc_number_id.
        Использует VIEW announcement_latest_invoice_block если он существует;
        иначе строит inline subquery (совместимость со старой БД).
        """
        cursor = self.db_connection.cursor()
        today = date.today().strftime("%Y-%m-%d")
        post_ids: Set[int] = set()

        # Проверяем, существует ли VIEW
        cursor.execute(
            "SELECT name FROM sqlite_master WHERE type IN ('view','table') AND name = 'announcement_latest_invoice_block'"
        )
        has_view = cursor.fetchone() is not None

        if has_view:
            latest_join = """
                JOIN announcement_latest_invoice_block lib
                  ON lib.announcement_id = p.announcement_id
            """
        else:
            # Inline subquery: берём последний блок для каждого объявления
            latest_join = """
                JOIN (
                    SELECT ib.announcement_id, ib.depublication_date
                    FROM announcement_invoice_blocks ib
                    INNER JOIN (
                        SELECT announcement_id, MAX(block_number) AS max_block
                        FROM announcement_invoice_blocks
                        GROUP BY announcement_id
                    ) lb ON lb.announcement_id = ib.announcement_id
                         AND lb.max_block       = ib.block_number
                ) lib ON lib.announcement_id = p.announcement_id
            """

        for field in _VK_POST_FIELDS:
            sql = f"""
                SELECT DISTINCT p.{field}
                FROM announcement_publications p
                {latest_join}
                WHERE p.publication_state = 'published'
                  AND p.group_id          = ?
                  AND p.{field}           IS NOT NULL
                  AND DATE(lib.depublication_date) <= DATE(?)
            """
            try:
                cursor.execute(sql, (group_id, today))
                for row in cursor.fetchall():
                    if row[0] is not None:
                        post_ids.add(int(row[0]))
            except sqlite3.Error as e:
                print(f"  Предупреждение: ошибка запроса для поля {field}: {e}")

        return post_ids

    # ──────────────────────────────────────────────────────────────────────────
    # Режим ALL: удалить все посты
    # ──────────────────────────────────────────────────────────────────────────

    def _delete_all_posts(self, group_id: int) -> None:
        """Удаляет ВСЕ посты из группы и обновляет БД."""
        print(f"[Режим: all] Удаление всех постов из группы {group_id}...")

        posts = self._fetch_posts(group_id)
        print(f"Найдено {len(posts)} постов для удаления")

        total_deleted = 0
        total_updated = 0

        while posts:
            for post in posts:
                pid = post["id"]
                try:
                    total_updated += self._update_vacancy_depublication(pid, group_id, source="depublication_all")
                    self.vk.wall.delete(owner_id=-group_id, post_id=pid)
                    print(f"Удалён пост {pid}")
                    total_deleted += 1
                    time.sleep(1)
                except ApiError:
                    print(f"Ошибка API при удалении поста {pid}, переинициализация...")
                    self._init_vk_connection()
                    total_updated += self._update_vacancy_depublication(pid, group_id, source="depublication_all")
                    self.vk.wall.delete(owner_id=-group_id, post_id=pid)
                    print(f"Удалён пост {pid} (после переинициализации)")
                    total_deleted += 1
                    time.sleep(1)
                except VkApiError as e:
                    print(f"Ошибка VkApiError при удалении поста {pid}: {e}")

            posts = self._fetch_posts(group_id)
            if posts:
                print(f"Найдено ещё {len(posts)} постов для удаления")

        print(f"Готово. Удалено постов: {total_deleted}, обновлено записей в БД: {total_updated}")

    # ──────────────────────────────────────────────────────────────────────────
    # Режим EXPIRED: удалить только просроченные
    # ──────────────────────────────────────────────────────────────────────────

    def _delete_expired_posts(self, group_id: int) -> None:
        """
        Удаляет из группы только те посты, чьи объявления уже просрочены (ТЗ §3).
        После удаления:
            I.  real_depublication_date заполняется текущей датой
            II. publication_state → 'deleted'
            III. announcements.status → 'Завершенное'
        """
        print(f"[Режим: expired] Поиск просроченных постов в группе {group_id}...")
        today_str = date.today().strftime("%d.%m.%Y")
        print(f"Дата сравнения: {today_str}")

        expired_ids = self._get_expired_post_ids(group_id)

        if not expired_ids:
            print("Просроченных постов не найдено — ничего не удаляется.")
            return

        sorted_ids = sorted(expired_ids)
        print(f"Найдено {len(sorted_ids)} просроченных постов: {sorted_ids}")

        total_deleted = 0
        total_updated = 0

        for post_id in sorted_ids:
            try:
                updated = self._update_vacancy_depublication(
                    post_id, group_id, source="depublication_expired"
                )
                total_updated += updated

                self.vk.wall.delete(owner_id=-group_id, post_id=post_id)
                print(f"Удалён просроченный пост {post_id} (обновлено записей: {updated})")
                total_deleted += 1
                time.sleep(1)

            except ApiError:
                print(f"Ошибка API при удалении поста {post_id}, переинициализация...")
                self._init_vk_connection()
                try:
                    updated = self._update_vacancy_depublication(
                        post_id, group_id, source="depublication_expired"
                    )
                    total_updated += updated
                    self.vk.wall.delete(owner_id=-group_id, post_id=post_id)
                    print(f"Удалён просроченный пост {post_id} (после переинициализации, обновлено: {updated})")
                    total_deleted += 1
                    time.sleep(1)
                except Exception as e:
                    print(f"Не удалось удалить пост {post_id}: {e}")

            except VkApiError as e:
                print(f"Ошибка VkApiError при удалении поста {post_id}: {e}")

        print(
            f"Готово. Удалено просроченных постов: {total_deleted}, "
            f"обновлено записей в БД: {total_updated}"
        )

    # ──────────────────────────────────────────────────────────────────────────
    # Вспомогательные методы
    # ──────────────────────────────────────────────────────────────────────────

    def _fetch_posts(self, group_id: int) -> list:
        """Получает до 100 постов со стены группы с автоповтором при ошибке API."""
        try:
            return self.vk.wall.get(owner_id=-group_id, count=100)["items"]
        except ApiError:
            print("Ошибка API при получении постов, переинициализация...")
            self._init_vk_connection()
            return self.vk.wall.get(owner_id=-group_id, count=100)["items"]

    # ──────────────────────────────────────────────────────────────────────────
    # Точка входа
    # ──────────────────────────────────────────────────────────────────────────

    def run(self, group_id: int) -> None:
        """Запускает очистку в нужном режиме."""
        try:
            if self.mode == "expired":
                self._delete_expired_posts(group_id)
            else:
                self._delete_all_posts(group_id)
        except Exception as e:
            print(f"Критическая ошибка: {e}")


# ──────────────────────────────────────────────────────────────────────────────
# CLI
# ──────────────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Очистка/депубликация постов из стены VK-сообщества",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Режимы (--mode):
  all      Удалить ВСЕ посты со стены (поведение по умолчанию)
  expired  Удалить только посты с истёкшей датой депубликации (ТЗ §3)

Примеры:
  clear_wall.py 123456 <token> <client_id> <device_id>
  clear_wall.py 123456 <token> <client_id> <device_id> --mode expired
  clear_wall.py 123456 <token> <client_id> <device_id> --mode expired --db_path /data/vacancies.db
""",
    )

    # Обязательные аргументы
    parser.add_argument("group_id",      type=int, help="ID группы VK")
    parser.add_argument("refresh_token", type=str, help="Refresh-токен VK API")
    parser.add_argument("client_id",     type=str, help="ID приложения VK API")
    parser.add_argument("device_id",     type=str, help="ID устройства VK API")

    # Опциональные аргументы
    parser.add_argument(
        "--mode",
        type=str,
        choices=["all", "expired"],
        default="all",
        help="Режим очистки: all — все посты, expired — только просроченные (по умолчанию: all)",
    )
    parser.add_argument(
        "--db_path",
        type=str,
        default="vacancies.db",
        help="Путь к файлу SQLite (по умолчанию: vacancies.db)",
    )

    args = parser.parse_args()

    config = {
        "group_id":     args.group_id,
        "client_id":    args.client_id,
        "refresh_token": args.refresh_token,
        "device_id":    args.device_id,
        "db_path":      args.db_path,
    }

    print(f"Группа:   {args.group_id}")
    print(f"Режим:    {args.mode}")
    print(f"База:     {args.db_path}")
    print("-" * 40)

    publisher = VKPostPublisher(config, mode=args.mode)
    publisher.run(args.group_id)

#include "databasemanager.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>
#include <QtSql>

#include "xmlimportservice.h"
#include "xlsxdocument.h"
#include "xlsxformat.h"
#include "sqlite/sqlite3.h"


namespace {

bool hasColumn(QSqlDatabase &db, const QString &tableName, const QString &columnName)
{
    return db.record(tableName).indexOf(columnName) >= 0;
}

bool ensureVkGroupsSchema(QSqlDatabase &db)
{
    const QStringList alterStatements = {
        "ALTER TABLE vk_groups ADD COLUMN refresh_token TEXT",
        "ALTER TABLE vk_groups ADD COLUMN client_id TEXT",
        "ALTER TABLE vk_groups ADD COLUMN device_id TEXT",
        "ALTER TABLE vk_groups ADD COLUMN filter_file_path TEXT"
    };

    const QStringList columnNames = {
        "refresh_token",
        "client_id",
        "device_id",
        "filter_file_path"
    };

    QSqlQuery query(db);
    for (int i = 0; i < columnNames.size(); ++i) {
        if (hasColumn(db, "vk_groups", columnNames.at(i))) {
            continue;
        }
        if (!query.exec(alterStatements.at(i))) {
            qDebug() << "Ошибка миграции vk_groups:" << query.lastError().text();
            return false;
        }
    }
    return true;
}

QString backgroundJobsGroupColumnName()
{
    return QStringLiteral("group_record_id");
}

bool ensureBackgroundJobsSchema(QSqlDatabase &db)
{
    QSqlQuery query(db);
    const QString createBackgroundJobs = R"(
        CREATE TABLE IF NOT EXISTS background_jobs (
            group_record_id INTEGER NOT NULL,
            task_type TEXT NOT NULL,
            status TEXT NOT NULL DEFAULT 'running',
            process_id INTEGER DEFAULT -1,
            started_at TEXT,
            finished_at TEXT,
            last_error TEXT,
            executable_path TEXT,
            detached INTEGER NOT NULL DEFAULT 1,
            PRIMARY KEY (group_record_id, task_type),
            FOREIGN KEY (group_record_id) REFERENCES vk_groups(id) ON DELETE CASCADE
        );
    )";

    if (!query.exec(createBackgroundJobs)) {
        qDebug() << "Ошибка создания таблицы background_jobs:" << query.lastError().text();
        return false;
    }

    query.exec("CREATE INDEX IF NOT EXISTS idx_background_jobs_status ON background_jobs(task_type, status);");
    return true;
}

}
#include <QFile>
#include <QtSql>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QStringList>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QDir>
#include <QDateTime>
#include "xlsxdocument.h"
#include "xlsxformat.h"
#include "sqlite/sqlite3.h"
#include "group.h"

namespace {

// Функция сравнения:
void likeNoCase(sqlite3_context* context, int argc, sqlite3_value** argv) {
    if (argc != 2) return;

    const char* str = reinterpret_cast<const char*>(sqlite3_value_text(argv[0]));
    const char* pattern = reinterpret_cast<const char*>(sqlite3_value_text(argv[1]));

    if (!str || !pattern) {
        sqlite3_result_int(context, 0);
        return;
    }

    QString qstr = QString::fromUtf8(str);
    QString qpattern = QString::fromUtf8(pattern);

    //qDebug() << "LIKE_NOCASE: str =" << qstr << "pattern =" << qpattern;

    // Простая реализация для шаблонов вида "текст%"
    if (qpattern.endsWith('%') && qpattern.startsWith('%')) {
        QString main = qpattern.left(qpattern.length() - 1);
        main = main.right(main.length() - 1);
        bool match = qstr.contains(main, Qt::CaseInsensitive);
        sqlite3_result_int(context, match ? 1 : 0);
    }
    else if (qpattern.endsWith('%')) {
        QString prefix = qpattern.left(qpattern.length() - 1);
        bool match = qstr.startsWith(prefix, Qt::CaseInsensitive);
        //qDebug() << "Prefix match:" << prefix << "->" << match;
        sqlite3_result_int(context, match ? 1 : 0);
    } else {
        // Для других случаев используем прямое сравнение
        bool match = (QString::compare(qstr, qpattern, Qt::CaseInsensitive) == 0);
        //qDebug() << "Exact match:" << match;
        sqlite3_result_int(context, match ? 1 : 0);
    }
}

bool connectWithDB(QSqlDatabase &db, const QString &dbPath) {
    // Проверяем существует ли файл БД, если нет - создаём
    bool dbExists = QFile::exists(dbPath);

    // Подключаемся к БД SQLite
    db.setDatabaseName(dbPath);

    if (!db.open()) {
        qDebug() << "Не удалось открыть базу данных:" << db.lastError().text();
        return false;
    }


    if (!dbExists) {
        QSqlQuery createQuery;
        // Создаем/мигрируем структуру (и для новой, и для существующей БД)

        // 1) Базовая таблица объявлений (общие данные)
        // Оставляем совместимость со старой схемой: если таблица уже существует, CREATE TABLE IF NOT EXISTS ничего не сломает.
        QString createAnnouncements = R"(
            CREATE TABLE IF NOT EXISTS announcements (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                vacancy TEXT NOT NULL,
                publication_date TEXT NOT NULL,
                depublication_date TEXT NOT NULL,
                account_number TEXT NOT NULL,
                account_date TEXT NOT NULL,
                atribute TEXT,
                inn TEXT NOT NULL,
                company TEXT NOT NULL,
                phone TEXT NOT NULL,
                email TEXT NOT NULL,
                contact_man TEXT,
                address TEXT,
                house_number TEXT,
                responcibilities TEXT,
                requirements TEXT,
                conditions TEXT,
                work_schedule TEXT,
                salary TEXT NOT NULL,
                vacancy_key TEXT
            );
        )";
        if (!createQuery.exec(createAnnouncements)) {
            qDebug() << "Ошибка создания таблицы announcements:" << createQuery.lastError().text();
            return false;
        }

        // 2) Таблица групп (group_id - из вк)
        QString createGroups = R"(
            CREATE TABLE IF NOT EXISTS vk_groups (
                id INTEGER PRIMARY KEY,
                group_id INTEGER UNIQUE,
                group_link TEXT,
                name TEXT,
                refresh_token TEXT,
                client_id TEXT,
                device_id TEXT,
                filter_file_path TEXT,
                created_at TEXT DEFAULT (datetime('now'))
            );
        )";
        if (!createQuery.exec(createGroups)) {
            qDebug() << "Ошибка создания таблицы vk_groups:" << createQuery.lastError().text();
            return false;
        }
        if (!ensureVkGroupsSchema(db)) {
            return false;
        }
        if (!ensureBackgroundJobsSchema(db)) {
            return false;
        }

        // 3) Таблица состояния публикации объявления в конкретной группе
        QString createPub = R"(
            CREATE TABLE IF NOT EXISTS announcement_publications (
                group_id INTEGER NOT NULL,
                announcement_id INTEGER NOT NULL,
                status TEXT NOT NULL DEFAULT "Не опубликованное",
                vk_single_link TEXT,
                vk_id INTEGER,
                real_publication_date TEXT,
                real_depublication_date TEXT,
                vk_union_vacancy_link TEXT,
                vk_union_vacancy_id INTEGER,
                vk_union_acc_number_link TEXT,
                vk_union_acc_number_id INTEGER,
                real_publication_union_vacancy_date TEXT,
                real_publication_union_acc_number_date TEXT,
                PRIMARY KEY (group_id, announcement_id),
                FOREIGN KEY (group_id) REFERENCES vk_groups(group_id) ON DELETE CASCADE ON UPDATE CASCADE,
                FOREIGN KEY (announcement_id) REFERENCES announcements(id) ON DELETE CASCADE
            );
        )";
        if (!createQuery.exec(createPub)) {
            qDebug() << "Ошибка создания таблицы announcement_publications:" << createQuery.lastError().text();
            return false;
        }

        // Индексы
        createQuery.exec("CREATE INDEX IF NOT EXISTS idx_ann_vacancy_key ON announcements(vacancy_key);");
        createQuery.exec("CREATE INDEX IF NOT EXISTS idx_pub_group_status ON announcement_publications(group_id, status);");
        createQuery.exec("CREATE INDEX IF NOT EXISTS idx_pub_group_vk_id ON announcement_publications(group_id, vk_id);");
        createQuery.exec("CREATE INDEX IF NOT EXISTS idx_pub_group_union_vac_id ON announcement_publications(group_id, vk_union_vacancy_id);");
        createQuery.exec("CREATE INDEX IF NOT EXISTS idx_pub_group_union_acc_id ON announcement_publications(group_id, vk_union_acc_number_id);");

        // 4) Триггеры для импорта (announcements)
        // - запрещаем добавлять записи с PUBLOFF <= today
        // - запрещаем добавлять клоны (inn+vacancy) со счетом не новее существующего
        // ВАЖНО: НЕ удаляем старые записи в триггере (удаление ломает кейсы ТЗ с "вторым объявлением по новому счету")
        QString trg1 = R"(
            CREATE TRIGGER IF NOT EXISTS trg_ann_before_insert_guard
            BEFORE INSERT ON announcements
            FOR EACH ROW
            BEGIN
                -- Нормализуем vacancy_key (если не задан)
                SELECT CASE
                    WHEN NEW.vacancy_key IS NULL OR NEW.vacancy_key = ''
                    THEN (SELECT NEW.vacancy_key = lower(trim(NEW.inn)) || '|' || lower(trim(NEW.vacancy)))
                END;

                -- PUBLOFF должен быть строго больше текущей даты
                SELECT CASE
                    WHEN date(NEW.depublication_date) <= date('now')
                    THEN RAISE(ABORT, 'Нельзя добавить объявление с датой завершения публикации меньше или равной текущей')
                END;

                -- Клон = совпадение (inn+vacancy) и в БД уже есть запись с account_date >= NEW.account_date
                SELECT RAISE(ABORT, 'Клон объявления: в базе уже есть запись с более поздней датой счета')
                FROM announcements a
                WHERE a.vacancy_key = (lower(trim(NEW.inn)) || '|' || lower(trim(NEW.vacancy)))
                  AND date(a.account_date) >= date(NEW.account_date)
                LIMIT 1;
            END;
        )";
        if (!createQuery.exec(trg1)) {
            qDebug() << "Ошибка создания триггера trg_ann_before_insert_guard:" << createQuery.lastError().text();
            return false;
        }

        // Автозаполнение vacancy_key после вставки (на случай если SQLite не позволит присвоение NEW.vacancy_key)
        QString trg2 = R"(
            CREATE TRIGGER IF NOT EXISTS trg_ann_after_insert_set_key
            AFTER INSERT ON announcements
            FOR EACH ROW
            BEGIN
                UPDATE announcements
                   SET vacancy_key = lower(trim(inn)) || '|' || lower(trim(vacancy))
                 WHERE id = NEW.id
                   AND (vacancy_key IS NULL OR vacancy_key = '');
            END;
        )";
        createQuery.exec(trg2);

        QString createTriggerSql = R"(
            CREATE TRIGGER before_announcements_insert
            BEFORE INSERT ON announcements
            FOR EACH ROW
            BEGIN
                -- Проверка 1: Дата завершения публикации должна быть в будущем
                SELECT CASE
                    WHEN date(NEW.depublication_date) <= date('now')
                    THEN RAISE(ABORT, 'Нельзя добавить объявление с датой завершения публикации меньше или равной текущей')
                END;

                -- Проверка 2: Если существует объявление с таким же INN и вакансией и более поздней датой счета - отмена
                SELECT RAISE(ABORT, 'Объявление с таким INN и вакансией уже существует с более поздней датой счета')
                FROM announcements
                WHERE inn = NEW.inn
                  AND vacancy = NEW.vacancy
                  AND date(account_date) >= date(NEW.account_date)
                LIMIT 1;

                -- Удаляем старые записи, если они устарели по дате счёта
                DELETE FROM announcements
                WHERE inn = NEW.inn
                  AND vacancy = NEW.vacancy
                  AND date(account_date) < date(NEW.account_date);
            END;
        )";

        if (!createQuery.exec(createTriggerSql)) {
            qDebug() << "Ошибка создания триггера:" << createQuery.lastError().text();
            return false;
        }

        qDebug() << "База данных успешно создана и инициализирована";

    }
    QVariant handle = db.driver()->handle();
    sqlite3* db2 = *static_cast<sqlite3* const*>(handle.data());
    sqlite3_initialize();
    if (db2) {
        //qDebug() << "creating locale";
        //qDebug() <<
                    sqlite3_create_function(db2, "LIKE_NOCASE", 2, SQLITE_UTF8, nullptr,
                                            &likeNoCase, nullptr, nullptr);
        //qDebug() << "created locale";
    }
    // Включаем поддержку внешних ключей
    QSqlQuery pragmaQuery;
    if (!pragmaQuery.exec("PRAGMA foreign_keys = ON;")) {
        qDebug() << "Ошибка включения поддержки внешних ключей:" << pragmaQuery.lastError().text();
        return false;
    }
    if (!ensureVkGroupsSchema(db)) {
        return false;
    }
    if (!ensureBackgroundJobsSchema(db)) {
        return false;
    }
    return true;
}


QString getPercentsOfWord(const QString &word, float percents)
{
    const int len = word.length();
    const float needLen = len * percents;
    const int realPartLen = qRound(needLen);
    return word.left(realPartLen);
}

QString getPercentsOfWord(const QString &word)
{
    return getPercentsOfWord(word, 0.7f);
}

QVector<QVector<QVariant>> queryAnnouncementsData(QSqlDatabase &db, int groupId, int &numRecords, QMap<QString, QString> columnsFilters,
                                  int limit, int page, QDate dateStart, QDate dateFinish,
                                  QString status, int id, QString columnSort,
                                  bool sortAsc,
                                  bool showMergedVacancy,
                                  bool showMergedAccount,
                                  const QStringList& allowedFirstWords)
{
    QString req = "SELECT "
            "a.id, "
            "a.vacancy, "
            "COALESCE(p.status, 'Не опубликованное') AS status, "
            "p.vk_single_link, "
            "a.publication_date, a.depublication_date, "
            "a.account_number, a.account_date, a.atribute, a.inn, a.company, a.phone, a.email, "
            "a.contact_man, a.address, a.house_number, a.responcibilities, a.requirements, a.conditions, "
            "a.work_schedule, a.salary, "
            "p.vk_union_vacancy_link, p.vk_union_acc_number_link, "
            "p.real_publication_date, p.real_depublication_date "
            "FROM announcements a "
            "LEFT JOIN (SELECT * FROM announcement_publications LEFT JOIN vk_groups ON announcement_publications.group_id=vk_groups.group_id WHERE id=" + QString::number(groupId) + ") p "
            "  ON p.announcement_id = a.id "
            "WHERE (";

    // Маппинг колонок на реальные поля/выражения в запросе (a.* или p.*)
    QMap<QString, QString> colExpr;
    colExpr["id"] = "a.id";
    colExpr["vacancy"] = "a.vacancy";
    colExpr["status"] = "COALESCE(p.status, 'Не опубликованное')";
    colExpr["vk_single_link"] = "p.vk_single_link";
    colExpr["publication_date"] = "a.publication_date";
    colExpr["depublication_date"] = "a.depublication_date";
    colExpr["account_number"] = "a.account_number";
    colExpr["account_date"] = "a.account_date";
    colExpr["atribute"] = "a.atribute";
    colExpr["inn"] = "a.inn";
    colExpr["company"] = "a.company";
    colExpr["phone"] = "a.phone";
    colExpr["email"] = "a.email";
    colExpr["contact_man"] = "a.contact_man";
    colExpr["address"] = "a.address";
    colExpr["house_number"] = "a.house_number";
    colExpr["responcibilities"] = "a.responcibilities";
    colExpr["requirements"] = "a.requirements";
    colExpr["conditions"] = "a.conditions";
    colExpr["work_schedule"] = "a.work_schedule";
    colExpr["salary"] = "a.salary";
    colExpr["vk_union_vacancy_link"] = "p.vk_union_vacancy_link";
    colExpr["vk_union_acc_number_link"] = "p.vk_union_acc_number_link";
    colExpr["real_publication_date"] = "p.real_publication_date";
    colExpr["real_depublication_date"] = "p.real_depublication_date";

    // Формирование условий фильтрации (без изменений)
    QString filters = "";
    for (auto i = columnsFilters.begin(); i != columnsFilters.end(); ++i)
    {
        if (!filters.isEmpty()) filters += " AND ";
        filters += "LIKE_NOCASE(" + colExpr.value(i.key(), i.key()) + ", '%" + i.value() + "%')";
    }
    req += filters;

    if (id > 0)
    {
        if (!filters.isEmpty()) req += " AND ";
        req += "id=" + QString::number(id);
    }

    if (!status.isEmpty())
    {
        if (req[req.length()-1] != '(') req += " AND ";
        req += "COALESCE(p.status, 'Не опубликованное') = '" + status + "'";
    }

    if (req[req.length()-1] != '(') req += " AND ";
    req += "((publication_date BETWEEN '" + dateStart.toString("yyyy-MM-dd") + "' AND '" + dateFinish.toString("yyyy-MM-dd") + "') OR ";
    req += "(depublication_date BETWEEN '" + dateStart.toString("yyyy-MM-dd") + "' AND '" + dateFinish.toString("yyyy-MM-dd") +"') OR ";
    req += "(publication_date < '" + dateStart.toString("yyyy-MM-dd") + "' AND depublication_date > '" + dateFinish.toString("yyyy-MM-dd") +"'))) ";


    // После всех существующих условий добавляем фильтрацию по первым словам
        if (!allowedFirstWords.isEmpty()) {
            // Убедимся, что у нас уже есть условия
            if (req[req.length()-1] != '(') {
                req += " AND ";
            }

            req += "(";
            for (int i = 0; i < allowedFirstWords.size(); ++i) {
                if (i > 0) req += " OR ";

                // Используем LIKE для поиска по началу строки
                // ПРИМЕЧАНИЕ: SQLite не поддерживает REGEXP из коробки, используем LIKE
                // Для учета регистра используем функцию LOWER
                QString firstWord = allowedFirstWords[i];

                // Создаем шаблон для LIKE: слово может быть с любым продолжением
                req += QString("LIKE_NOCASE(LOWER(a.vacancy), '%1%')")
                       .arg(getPercentsOfWord(firstWord).toLower());
            }
            req += ")";
        }
    // Добавьте условия для объединенных записей
        if (showMergedVacancy) {
            if (req[req.length()-1] != '(') req += " AND ";
            req += "vk_union_vacancy_link IS NOT NULL AND vk_union_vacancy_link != '' ";
        }

        if (showMergedAccount) {
            if (req[req.length()-1] != '(') req += " AND ";
            req += "vk_union_acc_number_link IS NOT NULL AND vk_union_acc_number_link != '' ";
        }

    // Определяем поле для сортировки с учетом реальных дат
        QString sortField = colExpr.value(columnSort, columnSort);

        if (columnSort == "publication_date") {
            // Используем COALESCE для выбора реальной даты, если она есть, иначе обычную
            sortField = "COALESCE(p.real_publication_date, a.publication_date)";
        } else if (columnSort == "depublication_date") {
            sortField = "COALESCE(p.real_depublication_date, a.depublication_date)";
        }

        req += "ORDER BY " + sortField + " ";
        if (sortAsc) req += "ASC ";
        else req += "DESC ";

    // Получение общего количества записей
    QSqlQuery query_count(db);
    QString countReq = "SELECT COUNT(*) FROM (" + req + ") AS t";
    if (query_count.exec(countReq) && query_count.next()) {
        numRecords = query_count.value(0).toInt();
    } else {
        numRecords = 0;
    }

    // Добавление ограничений для постраничного вывода
    req += "LIMIT " + QString::number(limit) + " OFFSET " + QString::number(limit*(page-1));
    //qDebug() << allowedFirstWords;
    //qDebug() << req;

    QSqlQuery query(db);
    QVector<QVector<QVariant>> data;
    if (query.exec(req))
    {
        while (query.next()) {
            QVector<QVariant> vectorRow;
            for (int i = 0; i < 25; ++i) {
                vectorRow.append(query.value(i));
            }
            //qDebug() << vectorRow;
            data.append(vectorRow);
        }
    }
    else {
        //qDebug() << "Failed to get data:" << query.lastError().text();
    }

    return data;
}

bool exportAnnouncementsToExcel(QSqlDatabase &db, QString basePath, int groupId)
{
    if (groupId <= 0) {
        qDebug() << "Invalid group ID";
        return false;
    }

    // Проверяем подключение к БД
    if (!db.isOpen()) {
        qDebug() << "Database is not connected!";
        return false;
    }

    // 1. Получаем общее количество записей для выбранной группы
    QSqlQuery queryCount(db);
    QString countReq = "SELECT COUNT(DISTINCT a.id) "
                       "FROM announcements a "
                       "LEFT JOIN (SELECT * FROM announcement_publications "
                       "LEFT JOIN vk_groups ON announcement_publications.group_id=vk_groups.group_id "
                       "WHERE vk_groups.id=:id) ap "
                       "ON a.id = ap.announcement_id";

    queryCount.prepare(countReq);
    queryCount.bindValue(":id", groupId);

    if (!queryCount.exec() || !queryCount.next()) {
        qDebug() << "Count query error:" << queryCount.lastError().text();
        return false;
    }

    int totalRecords = queryCount.value(0).toInt();
    const int RECORDS_PER_FILE = 15000;
    int fileCount = (totalRecords + RECORDS_PER_FILE - 1) / RECORDS_PER_FILE;

    // 2. Определяем структуру запроса для данных (с JOIN таблиц)
    QString dataQueryBase = "SELECT "
                            "a.id, a.vacancy, COALESCE(ap.status, 'Не опубликованное') AS status, "
                            "ap.vk_single_link, a.publication_date, a.depublication_date, "
                            "a.account_number, a.account_date, a.atribute, a.inn, a.company, a.phone, a.email, "
                            "a.contact_man, a.address, a.house_number, a.responcibilities, a.requirements, a.conditions, "
                            "a.work_schedule, a.salary, ap.vk_union_vacancy_link, ap.vk_union_acc_number_link, "
                            "ap.real_publication_date, ap.real_depublication_date, ap.vk_id, ap.vk_union_vacancy_id, "
                            "ap.vk_union_acc_number_id, ap.real_publication_union_vacancy_date, ap.real_publication_union_acc_number_date "
                            "FROM announcements a "
                            "LEFT JOIN (SELECT * FROM announcement_publications "
                            "LEFT JOIN vk_groups ON announcement_publications.group_id=vk_groups.group_id "
                            "WHERE vk_groups.id=:id) ap "
                            "ON a.id = ap.announcement_id "
                            "ORDER BY a.vacancy";

    // 3. Получаем названия колонок (выполняем запрос с LIMIT 1)
    QSqlQuery columnQuery(db);
    columnQuery.prepare(dataQueryBase + " LIMIT 1");
    columnQuery.bindValue(":id", groupId);

    if (!columnQuery.exec()) {
        qDebug() << "Column query error:" << columnQuery.lastError().text();
        return false;
    }

    QStringList headers;
    if (columnQuery.next()) {
        QSqlRecord record = columnQuery.record();
        for (int i = 0; i < record.count(); ++i) {
            headers << record.fieldName(i);
        }
    }

    // 4. Проверяем путь для сохранения
    if (basePath.isEmpty()) {
        qDebug() << "Base path is empty";
        return false;
    }

    // Создаем директорию, если не существует
    QDir dir(basePath);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qDebug() << "Failed to create directory:" << basePath;
            return false;
        }
    }

    // 5. Создаем маппинг английских названий на русские
    QHash<QString, QString> columnNamesMap;
    columnNamesMap["id"] = "ID";
    columnNamesMap["vacancy"] = "Вакансия";
    columnNamesMap["status"] = "Статус публикации";
    columnNamesMap["vk_single_link"] = "Ссылка на публикацию в VK";
    columnNamesMap["publication_date"] = "Дата публикации (план)";
    columnNamesMap["depublication_date"] = "Дата завершения (план)";
    columnNamesMap["account_number"] = "Номер счета";
    columnNamesMap["account_date"] = "Дата счета";
    columnNamesMap["atribute"] = "Атрибут";
    columnNamesMap["inn"] = "ИНН";
    columnNamesMap["company"] = "Компания";
    columnNamesMap["phone"] = "Телефон";
    columnNamesMap["email"] = "Email";
    columnNamesMap["contact_man"] = "Контактное лицо";
    columnNamesMap["address"] = "Адрес";
    columnNamesMap["house_number"] = "Номер дома";
    columnNamesMap["responcibilities"] = "Обязанности";
    columnNamesMap["requirements"] = "Требования";
    columnNamesMap["conditions"] = "Условия";
    columnNamesMap["work_schedule"] = "График работы";
    columnNamesMap["salary"] = "Зарплата";
    columnNamesMap["vk_union_vacancy_link"] = "Ссылка на объединенную вакансию VK";
    columnNamesMap["vk_union_acc_number_link"] = "Ссылка на объединенный счет VK";
    columnNamesMap["real_publication_date"] = "Реальная дата публикации";
    columnNamesMap["real_depublication_date"] = "Реальная дата завершения";
    columnNamesMap["vk_id"] = "ID публикации в VK";
    columnNamesMap["vk_union_vacancy_id"] = "ID объединенной вакансии";
    columnNamesMap["vk_union_acc_number_id"] = "ID объединенного счета";
    columnNamesMap["real_publication_union_vacancy_date"] = "Дата публикации объединенной вакансии";
    columnNamesMap["real_publication_union_acc_number_date"] = "Дата публикации объединенного счета";

    // 6. Создаем список колонок с датами для преобразования формата
    QSet<QString> dateColumns;
    dateColumns << "publication_date" << "depublication_date" << "account_date"
                << "real_publication_date" << "real_depublication_date"
                << "real_publication_union_vacancy_date" << "real_publication_union_acc_number_date";

    // 7. Экспортируем данные частями
    for (int fileIndex = 0; fileIndex < fileCount; ++fileIndex) {
        int offset = fileIndex * RECORDS_PER_FILE;
        QString fileName = QString("%1/vk.com_%2_%3_%4.xlsx")
                          .arg(basePath)
                          .arg(groupId)
                          .arg(QDateTime::currentDateTime().toString("dd.MM.yyyy"))
                          .arg(fileIndex + 1);

        QXlsx::Document xlsx;
        QXlsx::Format headerFormat;
        headerFormat.setFontBold(true);
        headerFormat.setFillPattern(QXlsx::Format::PatternSolid);
        headerFormat.setPatternBackgroundColor(QColor(Qt::lightGray));

        // Записываем заголовки на русском
        for (int col = 0; col < headers.size(); ++col) {
            QString englishHeader = headers[col];
            QString russianHeader = columnNamesMap.value(englishHeader, englishHeader);
            xlsx.write(1, col + 1, russianHeader, headerFormat);
        }

        // Получаем данные для текущей части
        QString queryStr = dataQueryBase + QString(" LIMIT %1 OFFSET %2")
                          .arg(RECORDS_PER_FILE)
                          .arg(offset);

        QSqlQuery dataQuery(db);
        dataQuery.prepare(queryStr);
        dataQuery.bindValue(":id", groupId);

        if (!dataQuery.exec()) {
            qDebug() << "Data query error:" << dataQuery.lastError().text();
            qDebug() << "Query:" << queryStr;
            continue;
        }

        // Заполняем данные
        int row = 2;
        while (dataQuery.next()) {
            for (int col = 0; col < headers.size(); ++col) {
                QVariant value = dataQuery.value(col);
                QString columnName = headers[col];

                // Преобразуем даты в формат dd.MM.yyyy
                if (!value.isNull() && dateColumns.contains(columnName)) {
                    QString dateStr = value.toString();
                    // Проверяем формат yyyy-MM-dd
                    if (dateStr.length() == 10 && dateStr[4] == '-' && dateStr[7] == '-') {
                        QString year = dateStr.left(4);
                        QString month = dateStr.mid(5, 2);
                        QString day = dateStr.mid(8, 2);
                        value = day + "." + month + "." + year;
                    }
                    // Если есть время, преобразуем и его
                    else if (dateStr.length() > 10 && dateStr.contains(' ')) {
                        QString datePart = dateStr.left(10);
                        QString timePart = dateStr.mid(11);
                        if (datePart.length() == 10 && datePart[4] == '-' && datePart[7] == '-') {
                            QString year = datePart.left(4);
                            QString month = datePart.mid(5, 2);
                            QString day = datePart.mid(8, 2);
                            value = day + "." + month + "." + year + " " + timePart;
                        }
                    }
                }

                // Для NULL значений записываем пустую строку
                xlsx.write(row, col + 1, value.isNull() ? "" : value.toString());
            }
            row++;
        }

        // Сохраняем файл
        if (!xlsx.saveAs(fileName)) {
            qDebug() << "Failed to save file:" << fileName;
            return false;
        } else {
            qDebug() << "Successfully saved:" << fileName;
        }
    }

    return true;
}

bool updateAnnouncementDataValue(QSqlDatabase &db, int id, const QString &column, const QString &value)
{
    // Проверяем подключение к БД
    if (!db.isOpen()) {
        //qDebug() << "Database is not connected!";
        return false;
    }

    // Проверяем валидность параметров
    if (id <= 0 || column.isEmpty()) {
        //qDebug() << "Invalid parameters: id =" << id << ", column =" << column;
        return false;
    }

    // Создаем параметризованный запрос для защиты от SQL-инъекций
    QSqlQuery query(db);
    QString req = "UPDATE announcements SET " + column + " = :value WHERE id = :id";

    query.prepare(req);
    query.bindValue(":value", value);
    query.bindValue(":id", id);

    //qDebug() << "Executing query:" << query.lastQuery() << "with values: value =" << value << ", id =" << id;

    if (!query.exec()) {
        //qDebug() << "Failed to update row:" << query.lastError().text();
        return false;
    }

    // Проверяем, была ли действительно обновлена какая-либо строка
    if (query.numRowsAffected() <= 0) {
        //qDebug() << "No rows were updated. Check if ID" << id << "exists.";
        return false;
    }

    return true;
}

bool clearAnnouncementsTable(QSqlDatabase &db)
{
    // Проверяем подключение к БД
    if (!db.isOpen()) {
        //qDebug() << "Database is not connected!";
        return false;
    }
    QSqlQuery query(db);

    query.prepare("DELETE FROM announcement_publications");
    if (!query.exec()) {
        //qDebug() << "Failed to clear db:" << query.lastError().text();
        return false;
    }

    query.prepare("DELETE FROM announcements");
    if (!query.exec()) {
        //qDebug() << "Failed to clear db:" << query.lastError().text();
        return false;
    }

    return true;
}

bool insertGroupDB(const Group &groupData, QSqlDatabase &db)
{
    QSqlQuery query(db);
    query.prepare("INSERT INTO vk_groups (id, group_id, group_link, refresh_token, client_id, device_id, filter_file_path, name"
                 ") VALUES ("
                 ":id, :group_id, :group_link, :refresh_token, :client_id, :device_id, :filter_file_path, :name"
                 ")");

    query.bindValue(":id", groupData.id);
    query.bindValue(":group_id", groupData.groupId);
    query.bindValue(":group_link", groupData.groupLink);
    query.bindValue(":refresh_token", groupData.refreshToken);
    query.bindValue(":client_id", groupData.clientId);
    query.bindValue(":device_id", groupData.deviceId);
    query.bindValue(":filter_file_path", groupData.filterFilePath);
    query.bindValue(":name", groupData.name);

    if (!query.exec()) {
        qDebug() << query.lastError();
        return false;
    }

    QSqlQuery queryPublications(db);
    queryPublications.exec("INSERT OR IGNORE INTO announcement_publications (announcement_id, group_id, status)"
                               "SELECT id, " + QString::number(groupData.groupId) + ", 'Не опубликованное' FROM announcements;");
    return true;
}

void deleteGroupDB(int id, QSqlDatabase &db)
{
    // Формируем SQL-запрос для удаления данных (адаптировано для SQLite)
    QSqlQuery query(db);
    query.prepare("DELETE FROM vk_groups WHERE id=:id");

    // Привязываем значения (без изменений)
    query.bindValue(":id", id);

    if (!query.exec()) {
            //qDebug() << "Failed to delete group:" << query.lastError().text();
    }

    //qDebug() << "Successfully deleted user";
}

bool updateGroupDB(const Group &groupData, QSqlDatabase &db)
{
    QSqlQuery query(db);
    query.prepare("UPDATE vk_groups SET group_id=:group_id, group_link=:group_link, "
                  "refresh_token=:refresh_token, client_id=:client_id, device_id=:device_id, "
                  "filter_file_path=:filter_file_path, name=:name "
                  "WHERE id=:id");

    query.bindValue(":id", groupData.id);
    query.bindValue(":group_id", groupData.groupId);
    query.bindValue(":group_link", groupData.groupLink);
    query.bindValue(":refresh_token", groupData.refreshToken);
    query.bindValue(":client_id", groupData.clientId);
    query.bindValue(":device_id", groupData.deviceId);
    query.bindValue(":filter_file_path", groupData.filterFilePath);
    query.bindValue(":name", groupData.name);

    if (!query.exec()) {
            qDebug() << "Failed to change group:" << query.lastError().text();
        return false;
    }
    return true;
}

//QVector<int> returnAllGroupIds(QSqlDatabase &db)
//{
//    // Формируем SQL-запрос для просмотра данных (адаптировано для SQLite)
//    QSqlQuery query(db);
//    query.prepare("SELECT id FROM vk_groups");

//    QVector<int> vectorRow;
//    if (query.exec())
//    {
//        while (query.next()) {
//            vectorRow.append(query.value(0).toInt());
//        }
//    }
//    return vectorRow;
//}


QMap<int, Group> loadGroupsFromDb(QSqlDatabase &db)
{
    QMap<int, Group> groups;
    QSqlQuery query(db);
    query.prepare("SELECT id, group_id, group_link, refresh_token, client_id, device_id, filter_file_path, name FROM vk_groups ORDER BY id");

    if (!query.exec()) {
        return groups;
    }

    while (query.next()) {
        GroupDto dto;
        dto.id = query.value(0).toInt();
        dto.groupId = query.value(1).toInt();
        dto.groupLink = query.value(2).toString();
        dto.refreshToken = query.value(3).toString();
        dto.clientId = query.value(4).toString();
        dto.deviceId = query.value(5).toString();
        dto.filterFilePath = query.value(6).toString();
        dto.name = query.value(7).toString();

        groups.insert(dto.id, Group::fromDto(dto));
    }

    return groups;
}

int getNextGroupId(QSqlDatabase &db)
{
    QSqlQuery query(db);
    query.prepare("SELECT COALESCE(MAX(id), 0) + 1 FROM vk_groups");
    if (!query.exec() || !query.next()) {
        return 1;
    }
    return query.value(0).toInt();
}

bool loadGroupFromDb(int id, QSqlDatabase &db, Group &loadedGroup)
{
    QSqlQuery query(db);
    query.prepare("SELECT id, group_id, group_link, refresh_token, client_id, device_id, filter_file_path, name FROM vk_groups WHERE id=:id");
    query.bindValue(":id", id);
    if (!query.exec() || !query.next()) {
        return false;
    }

    GroupDto dto;
    dto.id = query.value(0).toInt();
    dto.groupId = query.value(1).toInt();
    dto.groupLink = query.value(2).toString();
    dto.refreshToken = query.value(3).toString();
    dto.clientId = query.value(4).toString();
    dto.deviceId = query.value(5).toString();
    dto.filterFilePath = query.value(6).toString();
    dto.name = query.value(6).toString();

    loadedGroup = Group::fromDto(dto);
    return true;
}


} // namespace

DatabaseManager::DatabaseManager()
{
    db = QSqlDatabase::addDatabase("QSQLITE");
}

DatabaseManager::~DatabaseManager()
{
    close();
}

QSqlDatabase &DatabaseManager::database()
{
    return db;
}

const QSqlDatabase &DatabaseManager::database() const
{
    return db;
}

bool DatabaseManager::open(const QString &dbPath)
{
    return connectWithDB(db, dbPath);
}

void DatabaseManager::close()
{
    if (db.isOpen()) {
        db.close();
    }
}

bool DatabaseManager::importAnnouncementsXml(const QString &xmlFilePath)
{
    return XmlImportService::importAnnouncementsToDatabase(xmlFilePath, db);
}

QVector<QVector<QVariant> > DatabaseManager::queryAnnouncements(int groupId,
                                                                int &numRecords,
                                                                const QMap<QString, QString> &columnsFilters,
                                                                int limit,
                                                                int page,
                                                                const QDate &dateStart,
                                                                const QDate &dateFinish,
                                                                const QString &status,
                                                                int id,
                                                                const QString &columnSort,
                                                                bool sortAsc,
                                                                bool showMergedVacancy,
                                                                bool showMergedAccount,
                                                                const QStringList &allowedFirstWords)
{
    return queryAnnouncementsData(db,
                                  groupId,
                                  numRecords,
                                  columnsFilters,
                                  limit,
                                  page,
                                  dateStart,
                                  dateFinish,
                                  status,
                                  id,
                                  columnSort,
                                  sortAsc,
                                  showMergedVacancy,
                                  showMergedAccount,
                                  allowedFirstWords);
}

bool DatabaseManager::exportAnnouncements(const QString &basePath, int groupId)
{
    return exportAnnouncementsToExcel(db, basePath, groupId);
}

bool DatabaseManager::updateAnnouncementValue(int id, const QString &column, const QString &value)
{
    return updateAnnouncementDataValue(db, id, column, value);
}

bool DatabaseManager::clearAnnouncements()
{
    return clearAnnouncementsTable(db);
}

bool DatabaseManager::insertGroup(const Group &groupData)
{
    return insertGroupDB(groupData, db);
}

bool DatabaseManager::updateGroup(const Group &groupData)
{
    return updateGroupDB(groupData, db);
}

void DatabaseManager::deleteGroup(int id)
{
    deleteGroupDB(id, db);
}

QMap<int, Group> DatabaseManager::loadGroups()
{
    return loadGroupsFromDb(db);
}

bool DatabaseManager::loadGroup(int id, Group &loadedGroup)
{
    return loadGroupFromDb(id, db, loadedGroup);
}

int DatabaseManager::nextGroupId()
{
    return getNextGroupId(db);
}

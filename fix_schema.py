import re
with open('src/BookDatabase.cpp', 'r') as f:
    text = f.read()

text = text.replace('constexpr int CURRENT_SCHEMA_VERSION = 18;', 'constexpr int CURRENT_SCHEMA_VERSION = 11;')

text = re.sub(r'''    if \(userVersion < 18\) \{
        sqlite3_exec\(\(sqlite3\*\)m_db, "INSERT OR REPLACE INTO schema_version \(version\) VALUES \(18\);", nullptr, nullptr,
                     nullptr\);
        sqlite3_exec\(\(sqlite3\*\)m_db, "PRAGMA user_version = 18;", nullptr, nullptr, nullptr\);
        userVersion = 18;
    \}

''', '', text)

with open('src/BookDatabase.cpp', 'w') as f:
    f.write(text)

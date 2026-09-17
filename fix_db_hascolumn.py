import re

content = open('src/db/Database.cpp').read()
content = content.replace(
    'bool Database::hasColumn',
    'bool db::Database::hasColumn'
)

with open('src/db/Database.cpp', 'w') as f:
    f.write(content)

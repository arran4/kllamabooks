import re

content = open('tests/test_Migrations.cpp').read()
content = content.replace(
    'return d.execute("ALTER TABLE documents ADD COLUMN folder_id_wrong INVALID_SYNTAX;");',
    'return d.execute("CREATE TABLE schema_version (id INTEGER);"); // Will fail because table already exists'
)

with open('tests/test_Migrations.cpp', 'w') as f:
    f.write(content)

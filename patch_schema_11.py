with open('src/BookDatabase.cpp', 'r') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    if "userVersion = 11;" in line:
        lines[i] = "        userVersion = 11;\n    }\n"
        break

with open('src/BookDatabase.cpp', 'w') as f:
    f.writelines(lines)

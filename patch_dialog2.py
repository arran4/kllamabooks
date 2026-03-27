with open("src/MainWindow.cpp", "r") as f:
    content = f.read()

content = content.replace(
    'doc.content + "\n" + textEdit->toPlainText()',
    'doc.content + "\\n" + textEdit->toPlainText()'
)

with open("src/MainWindow.cpp", "w") as f:
    f.write(content)
